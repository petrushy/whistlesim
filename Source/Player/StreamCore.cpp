#include "StreamCore.h"

#include <cmath>

void StreamCore::handleSysEx (const uint8_t* data, size_t size, double nowMs, const void* source)
{
    const auto type = warbl::messageType (data, size);
    if (! type)
        return;

    if (*type == warbl::kInfo)
    {
        if (auto info = warbl::decodeInfo (data, size))
            if (! simulating)
                deliverInfo (*info);
        return;
    }

    if (simulating || (*type != warbl::kFrame && *type != warbl::kBatch))
        return;

    // Stay with the first port that streams, so two streaming devices don't mix.
    const void* expected = nullptr;
    if (! streamingSource.compare_exchange_strong (expected, source) && expected != source)
        return;

    if (*type == warbl::kBatch)
        acceptBatch (data, size, nowMs);
    else if (auto frame = warbl::decodeFrame (data, size))
        acceptFrame (*frame, nowMs);
}

std::vector<std::vector<uint8_t>> StreamCore::streamCommands (bool on, bool suppressNotes)
{
    return { warbl::makeConfig (on, suppressNotes, warbl::Format::official), warbl::makeConfig (on, suppressNotes, warbl::Format::legacy) };
}

std::vector<std::vector<uint8_t>> StreamCore::requestInfoCommands()
{
    return { warbl::makeRequestInfo (warbl::Format::official), warbl::makeRequestInfo (warbl::Format::legacy) };
}

void StreamCore::acceptBatch (const uint8_t* data, size_t size, double nowMs)
{
    const auto frames = warbl::decodeBatch (data, size);
    if (frames.empty())
        return;
    {
        const whistle::SpinLock::ScopedLock lock (statsLock);
        stats.framesPerBatch += 0.05 * ((double) frames.size() - stats.framesPerBatch);
        if (lastBatchMs > 0.0 && nowMs - lastBatchMs < 200.0)
            stats.batchIntervalMs += 0.05 * ((nowMs - lastBatchMs) - stats.batchIntervalMs);
        lastBatchMs = nowMs;
    }
    for (const auto& f : frames)
        acceptFrame (f, nowMs, true);
}

void StreamCore::setSimulating (bool shouldSimulate)
{
    if (shouldSimulate == simulating.load())
        return;

    if (shouldSimulate)
    {
        simulatedTick = 0;
        player = {};
        simulating = true;
        resetStats();
        deliverInfo (warbl::SimulatedPlayer::info());
    }
    else
    {
        simulating = false;
        resetStats();
    }
}

void StreamCore::simulatorTick (double nowMs)
{
    if (! simulating)
        return;
    const auto tick = simulatedTick++;
    if (! simulateBluetooth)
    {
        acceptFrame (player.frame ((double) tick * 0.002), nowMs);
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
        acceptBatch (data.data(), data.size(), nowMs);
        simulatedBatch.clear();
    }
}

void StreamCore::acceptFrame (const warbl::SensorFrame& frame, double nowMs, bool batched)
{
    // The MIDI thread and the simulator timer can both get here around a switch; the queues allow one producer at a time.
    const whistle::SpinLock::ScopedLock producer (producerLock);

    if (resetRequested.exchange (false))
    {
        const whistle::SpinLock::ScopedLock lock (statsLock);
        stats = {};
        lastSeq = -1;
        windowMin[0] = windowMin[1] = 1.0e12;
        windowStartMs = rateWindowStartMs = nowMs;
        rateWindowFrames = 0;
        lastDeviceMs = -1.0;
        rateWindowLoopSumMs = rateWindowLoopMaxMs = 0.0;
        rateWindowLoops = 0;
        timestampUnwrapper.reset();
    }

    ReceivedFrame received;
    received.frame = frame;
    received.arrivalMs = nowMs;
    received.deviceMs = (double) timestampUnwrapper.next (frame.timestamp) * 1000.0 / warbl::kDeviceTicksPerSecond;
    received.batched = batched;
    {
        const whistle::SpinLock::ScopedLock lock (statsLock);
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

    const whistle::SpinLock::ScopedLock lock (statsLock);

    const bool consecutive = lastSeq >= 0 && ((frame.seq - lastSeq) & ((1 << warbl::kSeqBits) - 1)) == 1;
    if (lastSeq >= 0)
        stats.lostFrames += (uint64_t) ((frame.seq - lastSeq - 1) & ((1 << warbl::kSeqBits) - 1));
    lastSeq = frame.seq;

    // The loop period: device time between consecutive frames (one frame per WARBL loop).
    if (consecutive && lastDeviceMs >= 0.0)
    {
        const double loop = received.deviceMs - lastDeviceMs;
        rateWindowLoopSumMs += loop;
        rateWindowLoopMaxMs = std::max (rateWindowLoopMaxMs, loop);
        rateWindowLoops++;
    }
    lastDeviceMs = received.deviceMs;

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
        stats.loopMs = rateWindowLoops > 0 ? rateWindowLoopSumMs / (double) rateWindowLoops : 0.0;
        stats.loopMaxMs = rateWindowLoopMaxMs;
        rateWindowFrames = 0;
        rateWindowHoleUpdates = 0;
        rateWindowLoopSumMs = rateWindowLoopMaxMs = 0.0;
        rateWindowLoops = 0;
        rateWindowStartMs = nowMs;
    }
}

double StreamCore::Stats::jitterPercentileMs (double share) const
{
    uint64_t total = histogramOverflow;
    for (auto count : histogram)
        total += count;
    if (total == 0)
        return -1.0;
    const auto target = (uint64_t) std::ceil (share * (double) total);
    uint64_t sum = 0;
    for (size_t i = 0; i < histogram.size(); ++i)
    {
        sum += histogram[i];
        if (sum >= target)
            return (double) (i + 1) * kHistogramBinMs;
    }
    return kHistogramBins * kHistogramBinMs;
}

StreamCore::Stats StreamCore::getStats() const
{
    const whistle::SpinLock::ScopedLock lock (statsLock);
    return stats;
}

void StreamCore::resetStats()
{
    resetRequested = true;
}
