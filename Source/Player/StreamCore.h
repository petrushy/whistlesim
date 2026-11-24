// The WARBL sensor stream, without the MIDI ports: decodes the SysEx messages the app's MIDI layer hands it (USB or
// Bluetooth, on the MIDI thread), passes the frames to the audio thread and the UI through lock-free queues, and
// measures frame rate, lost frames and timing jitter. Also the scripted player (Simulate WARBL), stepped by the
// app's 2 ms timer. Standard library only: the desktop app's MIDI is JUCE, the iPhone app's CoreMIDI.

#pragma once

#include "SimulatedPlayer.h"
#include "SpscQueue.h"
#include "StreamProtocol.h"
#include "Whistle/Common.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// A frame plus when it reached the app.
struct ReceivedFrame
{
    warbl::SensorFrame frame;
    double arrivalMs = 0.0;  // The app's clock (milliseconds, as given to handleSysEx) when the MIDI layer got it.
    double deviceMs = 0.0;   // Unwrapped device timestamp.
    bool batched = false;    // Came in a Bluetooth batch (several frames per message, arriving together)
};

// About 0.7 MB (the queues): keep it on the heap, or in an object that is (a Windows thread's stack is 1 MB).
class StreamCore
{
public:
    // Histogram of extra transport delay: how much later than the fastest recent frame each frame arrived.
    static constexpr int kHistogramBins = 100;
    static constexpr double kHistogramBinMs = 0.1;

    virtual ~StreamCore() = default;

    // MIDI thread: one SysEx message (data without F0 and F7) from an input; `source` tells inputs apart. Frames are
    // taken from the first input that streams, so two streaming devices don't mix. nowMs: the app's clock.
    void handleSysEx (const uint8_t* data, size_t size, double nowMs, const void* source);
    // The input frames come from (null until one streams); forget it when the inputs close.
    const void* getStreamingSource() const { return streamingSource.load(); }
    void forgetStreamingSource() { streamingSource = nullptr; }

    // The commands to send, each a SysEx message without F0/F7.
    static std::vector<std::vector<uint8_t>> streamCommands (bool on, bool suppressNotes);
    static std::vector<std::vector<uint8_t>> requestInfoCommands();

    // Generate frames from a scripted player instead of WARBL (frames from WARBL are ignored meanwhile). The app
    // calls simulatorTick every 2 ms meanwhile, from a timer thread.
    virtual void setSimulating (bool shouldSimulate);
    void simulatorTick (double nowMs);
    std::atomic<bool> simulateBluetooth { false };  // The simulator sends Bluetooth batches every 15 ms instead of frames
    bool isSimulating() const { return simulating; }

    // Consumers. The audio queue must only be read by the audio thread, the UI queue by the UI thread.
    SpscQueue<ReceivedFrame, 1024> audioQueue;
    SpscQueue<ReceivedFrame, 8192> uiQueue;

    // WARBL's info message (calibration, firmware), or the simulator's: called on the MIDI or timer thread.
    std::function<void (const warbl::DeviceInfo&)> onInfoReceived;

    struct Stats
    {
        uint64_t frames = 0;
        uint64_t lostFrames = 0;
        double framesPerSecond = 0.0;
        double freshHoles = 0.0;            // Share of recent frames with a new tone hole reading (WARBL's ATmega kept up)
        uint64_t rejectedHoleReadings = 0;  // Garbled tone hole transfers WARBL caught (see SensorFrame::holesRejected)
        bool bluetooth = false;             // The recent frames came in Bluetooth batches
        double framesPerBatch = 0.0, batchIntervalMs = 0.0;  // Smoothed
        std::array<uint32_t, kHistogramBins> histogram {};
        uint32_t histogramOverflow = 0;
        // WARBL's loop period, from its timestamps between consecutive frames over the last 0.5 s: the mean and the
        // longest. Within a Bluetooth batch the timestamps are WARBL's estimates, so there the longest is less exact.
        double loopMs = 0.0, loopMaxMs = 0.0;

        // The arrival jitter below which this share (0-1) of frames came; negative before any frame, and the
        // histogram's end (kHistogramBins * kHistogramBinMs) when beyond it.
        double jitterPercentileMs (double share) const;
    };

    Stats getStats() const;
    void resetStats();

private:
    void acceptFrame (const warbl::SensorFrame&, double nowMs, bool batched = false);
    void acceptBatch (const uint8_t* data, size_t size, double nowMs);
    void deliverInfo (const warbl::DeviceInfo& info)
    {
        if (onInfoReceived)
            onInfoReceived (info);
    }

    std::atomic<const void*> streamingSource { nullptr };
    std::vector<warbl::SensorFrame> simulatedBatch;  // Simulator timer thread only
    double lastBatchMs = 0.0;

    // Written only on the MIDI thread (statistics are copied under a lock that is held only briefly).
    warbl::Unwrapper timestampUnwrapper { warbl::kTimestampBits };
    int lastSeq = -1;
    double windowMin[2] = { 1.0e12, 1.0e12 };  // Minimum (arrival - device) in the current and previous second.
    double windowStartMs = 0.0;
    double rateWindowStartMs = 0.0;
    uint64_t rateWindowFrames = 0, rateWindowHoleUpdates = 0;
    double lastDeviceMs = -1.0;  // The previous frame's device time
    double rateWindowLoopSumMs = 0.0, rateWindowLoopMaxMs = 0.0;
    uint64_t rateWindowLoops = 0;

    std::atomic<bool> simulating { false };
    warbl::SimulatedPlayer player;  // Simulator timer thread only.
    uint64_t simulatedTick = 0;
    whistle::SpinLock producerLock;

    mutable whistle::SpinLock statsLock;
    Stats stats;
    std::atomic<bool> resetRequested { false };
};
