#include "StreamReceiver.h"

StreamReceiver::StreamReceiver() = default;

StreamReceiver::~StreamReceiver()
{
    stopTimer();
    disconnect();
}

bool StreamReceiver::connect()
{
    disconnect();
    if (leaveDevicesAlone)
        return false;

    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto in = juce::MidiInput::openDevice (d.identifier, this))
                inputs.push_back (std::move (in));

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto out = juce::MidiOutput::openDevice (d.identifier))
                outputs.push_back (std::move (out));

    if (! isConnected())
    {
        disconnect();
        return false;
    }

    resetStats();
    for (auto& in : inputs)
        in->start();
    return true;
}

void StreamReceiver::disconnect()
{
    for (auto& in : inputs)
        in->stop();
    inputs.clear();
    outputs.clear();
    streamingInput = nullptr;
}

juce::String StreamReceiver::getPortNames() const
{
    juce::StringArray names;
    for (const auto& in : inputs)
        names.add (in->getName());
    return names.joinIntoString (", ");
}

juce::String StreamReceiver::getStreamingPortName() const
{
    if (simulating)
        return "built-in simulator";

    const auto* in = streamingInput.load();
    return in != nullptr ? in->getName() : juce::String();
}

// Each command goes out in both formats: the official firmware (4.8 on) only answers its registered ID, the earlier
// experimental firmware only 7D 57, and each ignores the other's.
void StreamReceiver::setStream (bool on, bool suppressNotes)
{
    sendSysEx (warbl::makeConfig (on, suppressNotes, warbl::Format::official));
    sendSysEx (warbl::makeConfig (on, suppressNotes, warbl::Format::legacy));
}

void StreamReceiver::requestInfo()
{
    sendSysEx (warbl::makeRequestInfo (warbl::Format::official));
    sendSysEx (warbl::makeRequestInfo (warbl::Format::legacy));
}

void StreamReceiver::sendSysEx (const std::vector<uint8_t>& data)
{
    const auto message = juce::MidiMessage::createSysExMessage (data.data(), (int) data.size());
    for (auto& out : outputs)
        out->sendMessageNow (message);
}

void StreamReceiver::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (! message.isSysEx())
        return;

    const auto* data = message.getSysExData();
    const auto size = (size_t) message.getSysExDataSize();
    const auto type = warbl::messageType (data, size);
    if (! type)
        return;

    if (*type == warbl::kInfo)
    {
        if (auto info = warbl::decodeInfo (data, size))
            if (! simulating)
                postInfo (*info);
        return;
    }

    if (simulating || (*type != warbl::kFrame && *type != warbl::kBatch))
        return;

    // Stay with the first port that streams, so two streaming devices don't mix.
    juce::MidiInput* expected = nullptr;
    if (! streamingInput.compare_exchange_strong (expected, source) && expected != source)
        return;

    if (*type == warbl::kBatch)
        acceptBatch (data, size, nowMs);
    else if (auto frame = warbl::decodeFrame (data, size))
        acceptFrame (*frame, nowMs);
}

void StreamReceiver::acceptBatch (const uint8_t* data, size_t size, double nowMs)
{
    const auto frames = warbl::decodeBatch (data, size);
    if (frames.empty())
        return;
    {
        const juce::SpinLock::ScopedLockType lock (statsLock);
        stats.framesPerBatch += 0.05 * ((double) frames.size() - stats.framesPerBatch);
        if (lastBatchMs > 0.0 && nowMs - lastBatchMs < 200.0)
            stats.batchIntervalMs += 0.05 * ((nowMs - lastBatchMs) - stats.batchIntervalMs);
        lastBatchMs = nowMs;
    }
    for (const auto& f : frames)
        acceptFrame (f, nowMs, true);
}

void StreamReceiver::postInfo (const warbl::DeviceInfo& info)
{
    juce::MessageManager::callAsync ([this, weakAlive = std::weak_ptr<bool> (alive), info]
    {
        if (weakAlive.lock() != nullptr && onInfo)  // The receiver may be gone by the time this runs.
            onInfo (info);
    });
}

void StreamReceiver::setSimulating (bool shouldSimulate)
{
    if (shouldSimulate == simulating.load())
        return;

    if (shouldSimulate)
    {
        simulatedTick = 0;
        player = {};
        simulating = true;
        resetStats();
        postInfo (warbl::SimulatedPlayer::info());
        startTimer (2);  // Like the firmware's 2 ms loop over USB.
    }
    else
    {
        stopTimer();
        simulating = false;
        resetStats();
        requestInfo();
    }
}

void StreamReceiver::hiResTimerCallback()
{
    const auto tick = simulatedTick++;
    if (! simulateBluetooth)
    {
        acceptFrame (player.frame ((double) tick * 0.002), juce::Time::getMillisecondCounterHiRes());
        return;
    }
    // As over Bluetooth: a sample every 3 ms (the firmware's loop with BLE), and the 5 samples of a 15 ms connection
    // interval in one batch, through the real encoder and decoder.
    const auto sample = tick * 2 / 3, previous = tick > 0 ? (tick - 1) * 2 / 3 : (uint64_t) -1;
    if (sample == previous)
        return;
    simulatedBatch.push_back (player.frame ((double) sample * 0.003));
    if (simulatedBatch.size() >= 5)
    {
        auto data = warbl::encodeBatchPayload (simulatedBatch);
        const auto header = warbl::makeHeader (warbl::kBatch);
        data.insert (data.begin(), header.begin(), header.end());
        acceptBatch (data.data(), data.size(), juce::Time::getMillisecondCounterHiRes());
        simulatedBatch.clear();
    }
}

void StreamReceiver::acceptFrame (const warbl::SensorFrame& frame, double nowMs, bool batched)
{
    // The MIDI thread and the simulator timer can both get here around a switch; the queues allow one producer at a time.
    const juce::SpinLock::ScopedLockType producer (producerLock);

    if (resetRequested.exchange (false))
    {
        const juce::SpinLock::ScopedLockType lock (statsLock);
        stats = {};
        lastSeq = -1;
        windowMin[0] = windowMin[1] = 1.0e12;
        windowStartMs = rateWindowStartMs = nowMs;
        rateWindowFrames = 0;
        timestampUnwrapper.reset();
    }

    ReceivedFrame received;
    received.frame = frame;
    received.arrivalMs = nowMs;
    received.deviceMs = (double) timestampUnwrapper.next (frame.timestamp) * 1000.0 / warbl::kDeviceTicksPerSecond;
    received.batched = batched;
    {
        const juce::SpinLock::ScopedLockType lock (statsLock);
        stats.bluetooth = batched;
    }

    audioQueue.push (received);
    uiQueue.push (received);

    // Timing. The device and computer clocks have an unknown offset, so measure each frame's delay relative
    // to the fastest frame of the last 1-2 s. That leaves only the variable part (jitter), and slow clock drift
    // between the two clocks drops out.
    const double offset = nowMs - received.deviceMs;
    if (nowMs - windowStartMs > 1000.0)
    {
        windowMin[1] = windowMin[0];
        windowMin[0] = 1.0e12;
        windowStartMs = nowMs;
    }
    windowMin[0] = std::min (windowMin[0], offset);
    const double extraDelay = offset - std::min (windowMin[0], windowMin[1]);

    const juce::SpinLock::ScopedLockType lock (statsLock);

    if (lastSeq >= 0)
        stats.lostFrames += (uint64_t) ((frame.seq - lastSeq - 1) & ((1 << warbl::kSeqBits) - 1));
    lastSeq = frame.seq;

    stats.frames++;
    const auto bin = (int) (extraDelay / kHistogramBinMs);
    if (bin >= 0 && bin < kHistogramBins)
        stats.histogram[(size_t) bin]++;
    else
        stats.histogramOverflow++;

    rateWindowFrames++;
    if (frame.holesUpdated())
        rateWindowHoleUpdates++;
    if (frame.holesRejected())
        stats.rejectedHoleReadings++;
    if (nowMs - rateWindowStartMs >= 500.0)
    {
        stats.framesPerSecond = (double) rateWindowFrames * 1000.0 / (nowMs - rateWindowStartMs);
        stats.freshHoles = (double) rateWindowHoleUpdates / (double) std::max<uint64_t> (1, rateWindowFrames);
        rateWindowFrames = 0;
        rateWindowHoleUpdates = 0;
        rateWindowStartMs = nowMs;
    }
}

StreamReceiver::Stats StreamReceiver::getStats() const
{
    const juce::SpinLock::ScopedLockType lock (statsLock);
    return stats;
}

void StreamReceiver::resetStats()
{
    resetRequested = true;
}
