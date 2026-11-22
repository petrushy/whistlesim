#include "RoomReverb.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kFadeSeconds = 0.05;
constexpr double kWetRampSeconds = 0.05;
constexpr double kMaxPreDelaySeconds = 0.2;

float decibelsToGain (float db)  // -60 dB and below: silent, as juce::Decibels with that floor
{
    return db > -60.0f ? std::pow (10.0f, db * 0.05f) : 0.0f;
}
} // namespace

RoomReverb::RoomReverb() : worker ([this] { runJobs(); }) {}

RoomReverb::~RoomReverb()
{
    {
        const std::lock_guard<std::mutex> lock (jobLock);
        quitting = true;
        job = nullptr;
    }
    jobWaiting.notify_all();
    worker.join();
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete active;
    delete fading;
}

void RoomReverb::runJobs()
{
    for (;;)
    {
        std::function<void()> next;
        {
            std::unique_lock<std::mutex> lock (jobLock);
            jobWaiting.wait (lock, [this] { return quitting || job != nullptr; });
            if (quitting)
                return;
            next = std::move (job);
            job = nullptr;
        }
        next();
    }
}

void RoomReverb::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& line : delayLines)
        line.assign ((size_t) (sampleRate * kMaxPreDelaySeconds) + 2, 0.0f);
    delayWrite = 0;
    for (auto* buffers : { &work, &faded })
        for (auto& b : *buffers)
            b.assign ((size_t) maxBlock, 0.0f);
    fadeLength = std::max (1, (int) (kFadeSeconds * sampleRate));
    wetGain = wetTarget = decibelsToGain (wetDb.load());
    wetStepsLeft = 0;
    for (auto* engine : { active, fading })
        if (engine != nullptr)
            for (auto& c : engine->convolvers)
                c->reset();

    // The responses depend on the sample rate.
    if (presetIndex >= 0)
        startGenerating (presetIndex);
    else if (fileResponse.getNumSamples() > 0)
    {
        const auto wet = reverb::resample (fileResponse, fileResponseRate, sampleRate);
        install (makeEngine (wet, wet, maxBlock), fileName);
    }
}

void RoomReverb::process (const float* windowLeft, const float* windowRight, const float* bodyLeft, const float* bodyRight,
                          float* left, float* right, int numSamples)
{
    // A finished fade whose old engine couldn't be handed back yet: try again.
    if (fading != nullptr && fadePosition >= fadeLength)
    {
        Engine* expected = nullptr;
        if (retired.compare_exchange_strong (expected, fading))
            fading = nullptr;
    }
    // A new engine waiting, and no fade going on: fade over to it.
    if (fading == nullptr)
    {
        if (auto* next = pending.exchange (nullptr))
        {
            fading = active;
            active = next;
            fadePosition = 0;
        }
    }

    const float target = decibelsToGain (wetDb.load());
    if (std::abs (target - wetTarget) > 0.0f)
    {
        wetTarget = target;
        wetStepsLeft = std::max (1, (int) (kWetRampSeconds * sampleRate));
        wetStep = (wetTarget - wetGain) / (float) wetStepsLeft;
    }
    const double delay = std::clamp ((double) preDelayMs.load() * 0.001 * sampleRate, 0.0, sampleRate * kMaxPreDelaySeconds);
    const int whole = (int) delay;
    const auto fraction = (float) (delay - whole);
    const int lineLength = (int) delayLines[0].size();

    const float* inputs[4] = { windowLeft, windowRight, bodyLeft, bodyRight };
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, maxBlock);

        // Pre-delay.
        for (int i = 0; i < n; ++i)
        {
            for (size_t ch = 0; ch < 4; ++ch)
            {
                auto& line = delayLines[ch];
                line[(size_t) delayWrite] = inputs[ch][done + i];
                const int a = (delayWrite - whole + lineLength) % lineLength, b = (a - 1 + lineLength) % lineLength;
                work[ch][(size_t) i] = line[(size_t) a] + fraction * (line[(size_t) b] - line[(size_t) a]);
            }
            delayWrite = (delayWrite + 1) % lineLength;
        }

        // The old engine, while it fades out, on its own copy of the input.
        if (fading != nullptr)
            for (size_t ch = 0; ch < 4; ++ch)
                fading->convolvers[ch]->process (work[ch].data(), faded[ch].data(), n);

        if (active != nullptr)
        {
            for (size_t ch = 0; ch < 4; ++ch)
                active->convolvers[ch]->process (work[ch].data(), work[ch].data(), n);
        }
        else
        {
            for (auto& w : work)
                std::fill (w.begin(), w.begin() + n, 0.0f);
        }

        for (int i = 0; i < n; ++i)
        {
            if (wetStepsLeft > 0)
            {
                wetGain = --wetStepsLeft == 0 ? wetTarget : wetGain + wetStep;
            }
            float wl = work[0][(size_t) i] + work[2][(size_t) i];
            float wr = work[1][(size_t) i] + work[3][(size_t) i];
            if (fading != nullptr)
            {
                // Equal-gain crossfade: both responses are of the same room level, and correlated at the start.
                const float in = (float) fadePosition / (float) fadeLength;
                wl = in * wl + (1.0f - in) * (faded[0][(size_t) i] + faded[2][(size_t) i]);
                wr = in * wr + (1.0f - in) * (faded[1][(size_t) i] + faded[3][(size_t) i]);
                if (++fadePosition >= fadeLength)
                {
                    // Done: hand the old engine back for deleting off this thread. If the last one hasn't been
                    // collected yet, this one stays silent until the next call tries again.
                    Engine* expected = nullptr;
                    if (retired.compare_exchange_strong (expected, fading))
                        fading = nullptr;
                }
            }
            left[done + i] += wl * wetGain;
            right[done + i] += wr * wetGain;
        }
        done += n;
    }
}

std::unique_ptr<RoomReverb::Engine> RoomReverb::makeEngine (const reverb::Buffer& window, const reverb::Buffer& body, int blockSize)
{
    // Short head partitions for the audio block size (the head's transforms run on every call), the tail from 1024.
    int head = 64;
    while (head < blockSize && head < 512)
        head *= 2;
    auto engine = std::make_unique<Engine>();
    const reverb::Buffer* sources[2] = { &window, &body };
    for (size_t s = 0; s < 2; ++s)
        for (size_t ear = 0; ear < 2; ++ear)
        {
            const auto& ir = *sources[s];
            const int channel = std::min ((int) ear, ir.getNumChannels() - 1);
            engine->convolvers[2 * s + ear] = std::make_unique<reverb::Convolver> (channel >= 0 ? ir.getReadPointer (channel) : nullptr,
                                                                                   ir.getNumSamples(), head, std::max (head, 1024));
        }
    return engine;
}

void RoomReverb::install (std::unique_ptr<Engine> engine, const std::string& name)
{
    delete retired.exchange (nullptr);
    delete pending.exchange (engine.release());  // One still waiting was never used: replace it
    const std::lock_guard<std::mutex> lock (nameLock);
    currentName = name;
}

void RoomReverb::setSpread (float degrees)
{
    spreadDegrees = degrees;
    if (presetIndex >= 0)
        startGenerating (presetIndex);
}

void RoomReverb::loadPreset (int index)
{
    presetIndex = index;
    currentFile.clear();
    startGenerating (index);
}

void RoomReverb::startGenerating (int index)
{
    const auto& rooms = reverb::presets();
    if (index < 0 || index >= (int) rooms.size())
        return;

    const auto room = rooms[(size_t) index];
    const double rate = sampleRate;
    const int blockSize = maxBlock;
    // The two sources sideways from straight ahead, at the room's listening distance.
    const double offset = room.distance * std::tan (0.5 * spreadDegrees.load() * 3.14159265358979 / 180.0);

    {
        const std::lock_guard<std::mutex> lock (jobLock);
        // Replaces a generation that hasn't started, if the user is clicking through rooms.
        job = [this, room, rate, offset, blockSize]
        {
            // Each source its own place and its own (independent) diffuse tail. The generator already sets the
            // level (direct sound = 1), so no normalising.
            const auto window = reverb::generate (room, rate, 1, -offset);
            const auto body = reverb::generate (room, rate, 2, offset);
            install (makeEngine (window, body, blockSize), room.name);
        };
    }
    jobWaiting.notify_one();
}

std::string RoomReverb::loadImpulseResponse (const reverb::Buffer& measured, double fileRate, const std::string& name, const std::string& path)
{
    if (measured.getNumChannels() == 0 || measured.getNumSamples() == 0)
        return "The IR is empty";
    if (measured.getNumSamples() > (int) (fileRate * 20.0))
        return "IR is longer than 20 s";

    // Measured IRs contain the direct sound (the dry signal already provides it) and have an arbitrary level, so
    // remove it and scale by it. One measured position only, so both sources share it.
    presetIndex = -1;
    currentFile = path;
    fileName = name;
    fileResponse = reverb::removeDirectSound (measured, fileRate);
    fileResponseRate = fileRate;
    const auto wet = reverb::resample (fileResponse, fileRate, sampleRate);
    install (makeEngine (wet, wet, maxBlock), name);
    return {};
}

std::string RoomReverb::getCurrentName() const
{
    const std::lock_guard<std::mutex> lock (nameLock);
    return currentName;
}
