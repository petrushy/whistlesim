// The reverb without JUCE (Source/Reverb): the FFT against a plain DFT, the zero-latency convolver against direct
// convolution for many response lengths and block sizes, the whole reverb against the generated room response,
// and how fast it runs.

#include "Reverb/Convolver.h"
#include "Reverb/Fft.h"
#include "Reverb/RoomReverb.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <random>
#include <thread>
#include <vector>

using namespace reverb;

static int failures = 0;

static void check (bool condition, const char* what, double value)
{
    if (! condition)
    {
        std::printf ("FAILED: %s (%g)\n", what, value);
        ++failures;
    }
}

static void testFft()
{
    std::mt19937 random (1);
    std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);
    double worst = 0.0, worstRoundTrip = 0.0;
    for (int order = 1; order <= 12; ++order)
    {
        const int n = 1 << order;
        std::vector<float> x ((size_t) n), data ((size_t) (2 * n));
        for (auto& v : x)
            v = uniform (random);
        std::copy (x.begin(), x.end(), data.begin());
        const Fft fft (order);
        fft.forward (data.data());
        for (int k = 0; k <= n / 2; ++k)
        {
            std::complex<double> dft;
            for (int i = 0; i < n; ++i)
                dft += (double) x[(size_t) i] * std::polar (1.0, -2.0 * std::numbers::pi * k * i / n);
            worst = std::max (worst, std::abs (dft - std::complex<double> (data[(size_t) (2 * k)], data[(size_t) (2 * k + 1)])) / std::sqrt ((double) n));
        }
        fft.inverse (data.data());
        for (int i = 0; i < n; ++i)
            worstRoundTrip = std::max (worstRoundTrip, (double) std::abs (data[(size_t) i] - x[(size_t) i]));
    }
    check (worst < 1.0e-5, "FFT against the DFT", worst);
    check (worstRoundTrip < 1.0e-5, "FFT round trip", worstRoundTrip);
    std::printf ("FFT: largest error against the DFT %.2g, round trip %.2g\n", worst, worstRoundTrip);
}

static void testConvolver()
{
    std::mt19937 random (2);
    std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);
    double worstAll = 0.0;
    for (const int length : { 1, 7, 64, 100, 1023, 1024, 1025, 3000, 20000 })
    {
        for (const int headPartition : { 64, 128, 512 })
        {
            std::vector<float> ir ((size_t) length);
            for (size_t i = 0; i < ir.size(); ++i)
                ir[i] = uniform (random) * std::exp (-3.0f * (float) i / (float) length);
            const int total = length + 6000;
            std::vector<float> x ((size_t) total);
            for (auto& v : x)
                v = uniform (random);

            // Direct convolution.
            std::vector<double> expected ((size_t) total, 0.0);
            for (int n = 0; n < total; ++n)
                for (int k = 0; k < length && k <= n; ++k)
                    expected[(size_t) n] += (double) ir[(size_t) k] * x[(size_t) (n - k)];

            // In blocks of random sizes, in place.
            Convolver convolver (ir.data(), length, headPartition, 1024);
            std::vector<float> y = x;
            std::uniform_int_distribution<int> blockSize (1, 700);
            for (int done = 0; done < total;)
            {
                const int n = std::min (total - done, blockSize (random));
                convolver.process (y.data() + done, y.data() + done, n);
                done += n;
            }
            double worst = 0.0, scale = 0.0;
            for (int n = 0; n < total; ++n)
            {
                worst = std::max (worst, std::abs (y[(size_t) n] - expected[(size_t) n]));
                scale = std::max (scale, std::abs (expected[(size_t) n]));
            }
            worstAll = std::max (worstAll, worst / scale);
            if (worst / scale > 1.0e-5)
                std::printf ("  length %d, head %d: error %.2g of the peak\n", length, headPartition, worst / scale);
        }
    }
    check (worstAll < 1.0e-5, "convolver against direct convolution", worstAll);
    std::printf ("Convolver: largest error against direct convolution %.2g of the peak (no latency)\n", worstAll);
}

static void testRoomReverb()
{
    constexpr double rate = 48000.0;
    constexpr int block = 128;
    const auto& room = presets()[0];

    RoomReverb reverb;
    reverb.preDelayMs = 0.0f;
    reverb.wetDb = 0.0f;
    reverb.prepare (rate, block);
    reverb.loadPreset (0);
    for (int wait = 0; wait < 500 && reverb.getCurrentName().empty(); ++wait)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    check (reverb.getCurrentName() == room.name, "room generated", 0.0);

    // What it should be: the window's response (seed 1, to the left by the spread) for an impulse at the window.
    const double offset = room.distance * std::tan (0.5 * reverb.getSpread() * std::numbers::pi / 180.0);
    const auto expected = generate (room, rate, 1, -offset);

    for (const float preDelayMs : { 0.0f, 10.0f })
    {
        reverb.preDelayMs = preDelayMs;
        const int delay = (int) std::lround (preDelayMs * 0.001 * rate);
        const int total = expected.getNumSamples() + delay + 1000;
        std::vector<float> in ((size_t) total, 0.0f), silence ((size_t) total, 0.0f), left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);
        // Clear what the last run left ringing, then the impulse.
        for (int done = 0; done < 8 * (int) rate; done += block)
        {
            std::vector<float> scratchL (block), scratchR (block);
            reverb.process (silence.data(), silence.data(), silence.data(), silence.data(), scratchL.data(), scratchR.data(), block);
        }
        in[0] = 1.0f;
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            reverb.process (in.data() + done, in.data() + done, silence.data() + done, silence.data() + done, left.data() + done, right.data() + done, n);
        }
        // The impulse came in at both ears, so each ear hears its own response.
        double worst = 0.0, peak = 0.0;
        for (int n = 0; n < total; ++n)
        {
            const int k = n - delay;
            const float wantL = k >= 0 && k < expected.getNumSamples() ? expected.getReadPointer (0)[k] : 0.0f;
            const float wantR = k >= 0 && k < expected.getNumSamples() ? expected.getReadPointer (1)[k] : 0.0f;
            worst = std::max ({ worst, (double) std::abs (left[(size_t) n] - wantL), (double) std::abs (right[(size_t) n] - wantR) });
            peak = std::max ({ peak, (double) std::abs (wantL), (double) std::abs (wantR) });
        }
        check (worst / peak < 1.0e-4, preDelayMs > 0.0f ? "reverb with pre-delay against the room's response" : "reverb against the room's response", worst / peak);
        std::printf ("Room reverb, pre-delay %.0f ms: largest error against the generated response %.2g of the peak\n", preDelayMs, worst / peak);
    }
}

// A measured IR at 44.1 kHz played at 48 kHz: the direct sound (0.5 at 50 ms) is removed and sets the level, so the
// reflection (0.25, 20 ms later) becomes 0.5, moved to the new rate with the same frequency response: its energy
// there is 0.5^2 * 44.1 / 48 (the same band spread over more samples, each smaller).
static void testLoadedResponse()
{
    RoomReverb reverb;
    reverb.preDelayMs = 0.0f;
    reverb.wetDb = 0.0f;
    reverb.prepare (48000.0, 256);
    Buffer measured (1, 44100);
    measured.getWritePointer (0)[2205] = 0.5f;
    measured.getWritePointer (0)[3087] = 0.25f;
    const auto error = reverb.loadImpulseResponse (measured, 44100.0, "test", "/nowhere/test.wav");
    check (error.empty() && reverb.getCurrentName() == "test" && reverb.getPresetIndex() == -1, "IR loaded", 0.0);

    const int total = 48000;
    std::vector<float> in ((size_t) total, 0.0f), silence ((size_t) total, 0.0f), left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);
    in[0] = 1.0f;
    for (int done = 0; done < total; done += 256)
        reverb.process (in.data() + done, silence.data() + done, silence.data() + done, silence.data() + done, left.data() + done, right.data() + done,
                        std::min (256, total - done));
    double energy = 0.0, rightEnergy = 0.0;
    int peakAt = 0;
    for (int n = 0; n < total; ++n)
    {
        energy += (double) left[(size_t) n] * left[(size_t) n];
        rightEnergy += (double) right[(size_t) n] * right[(size_t) n];
        if (std::abs (left[(size_t) n]) > std::abs (left[(size_t) peakAt]))
            peakAt = n;
    }
    // The IR starts 0.5 ms before the direct sound: the reflection 20.5 ms in, at 48 kHz.
    const double expectedAt = (3087 - 2205 + 22) * 48000.0 / 44100.0;
    const double expectedEnergy = 0.25 * 44100.0 / 48000.0;
    check (std::abs (energy / expectedEnergy - 1.0) < 0.02, "loaded IR: the reflection's energy", energy);
    check (std::abs (peakAt - expectedAt) < 1.5, "loaded IR: the reflection's time at the new rate", peakAt);
    check (rightEnergy < 1.0e-12, "loaded IR: nothing at the other ear for one ear's input", rightEnergy);
    std::printf ("Loaded IR (44.1 kHz at 48 kHz): reflection energy %.4f (%.4f), at sample %d (%.1f)\n", energy, expectedEnergy, peakAt, expectedAt);
}

// A new response while the last fade is still going (as when the app starts: settings, spread, room, one after
// another): the level must stay that of one response. A fade that finished while the old engine before it still
// waited to be deleted once kept raising its gain past 1, and the reverb grew louder and louder. Loaded responses go
// in at once, so the order is exact: A; B (fading A to B); C during that fade; then nothing more.
static void testResponseDuringFade()
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    RoomReverb reverb;
    reverb.wetDb = 0.0f;
    reverb.prepare (rate, block);

    // Each response: its direct sound (removed, and setting the level) and one reflection half its size, a
    // different time later in each, so their outputs differ: the input, delayed, at 0.5.
    auto response = [] (int reflectionAt)
    {
        Buffer ir (2, 4800);
        for (int ch = 0; ch < 2; ++ch)
        {
            ir.getWritePointer (ch)[100] = 0.5f;
            ir.getWritePointer (ch)[reflectionAt] = 0.25f;
        }
        return ir;
    };
    std::mt19937 random (5);
    std::uniform_real_distribution<float> noise (-0.1f, 0.1f);
    std::vector<float> in ((size_t) block), silence ((size_t) block, 0.0f), left ((size_t) block), right ((size_t) block);
    auto run = [&] (int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (auto& v : in)
                v = noise (random);
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            reverb.process (in.data(), in.data(), silence.data(), silence.data(), left.data(), right.data(), block);
            for (int i = 0; i < block; ++i)
                peak = std::max ({ peak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]) });
        }
        return peak;
    };
    reverb.loadImpulseResponse (response (580), rate, "A", "");
    const float steady = run (40);   // A alone: noise up to 0.1 in, up to 0.05 out
    reverb.loadImpulseResponse (response (1060), rate, "B", "");
    run (2);                         // Fading from A to B
    reverb.loadImpulseResponse (response (1540), rate, "C", "");
    const float after = run (400);   // The A-to-B fade ends, then B to C, then C alone for 2 s
    check (steady > 0.045f && steady <= 0.0501f, "one response: its level", steady);
    check (after < 1.1f * steady, "a new response during a fade: the level stays", after / steady);
    std::printf ("New response during a fade: peak %.4f against %.4f with one response\n", after, steady);
}

static void benchmark()
{
    constexpr double rate = 48000.0;
    for (const int block : { 64, 256 })
    {
        RoomReverb reverb;
        reverb.prepare (rate, block);
        reverb.loadPreset ((int) presets().size() - 1);  // The stone church: the longest response
        for (int wait = 0; wait < 500 && reverb.getCurrentName().empty(); ++wait)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));

        std::mt19937 random (3);
        std::uniform_real_distribution<float> uniform (-0.1f, 0.1f);
        std::vector<float> in ((size_t) block), left ((size_t) block), right ((size_t) block);
        const int blocks = (int) (10.0 * rate / block);  // 10 s of audio
        double slowest = 0.0;
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            for (auto& v : in)
                v = uniform (random);
            const auto t0 = std::chrono::steady_clock::now();
            reverb.process (in.data(), in.data(), in.data(), in.data(), left.data(), right.data(), block);
            slowest = std::max (slowest, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::printf ("Speed, %s, blocks of %d: %.1f %% of real time; slowest block %.2f ms of %.2f ms\n", reverb.getCurrentName().c_str(), block,
                     100.0 * seconds / 10.0, 1000.0 * slowest, 1000.0 * block / rate);
    }
}

int main (int argc, char* argv[])
{
    testFft();
    testConvolver();
    testRoomReverb();
    testLoadedResponse();
    testResponseDuringFade();
    if (argc > 1 && std::string (argv[1]) == "speed")
        benchmark();
    std::printf (failures == 0 ? "All reverb tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
