// What plays, without JUCE (Source/Player), driven as an app drives it: the simulated WARBL into the stream core,
// the player rendering blocks against the same clock. Checks that it plays and stays under the limiter's ceiling,
// rests when the stream stops, plays low D from the screen, and plans and plays the test scale.

#include "Player/Player.h"
#include "Whistle/PitchDetect.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

static int failures = 0;

static void check (bool condition, const char* what, double value)
{
    std::printf ("%s %s (%g)\n", condition ? "  ok    " : "  FAILED", what, value);
    if (! condition)
        ++failures;
}

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 128;

struct Rig
{
    StreamCore stream;
    Player player { stream.audioQueue };
    double nowMs = 0.0, nextTickMs = 0.0;
    std::vector<float> left, right;  // Everything rendered

    Rig()
    {
        stream.onInfoReceived = [this] (const warbl::DeviceInfo& info) { player.setCalibration (info); };
        player.prepare (kRate, kBlock);
    }

    // Renders `seconds`, the simulator ticking every 2 ms of the same clock (as the app's timer would).
    void run (double seconds)
    {
        std::vector<float> l (kBlock), r (kBlock);
        for (int b = 0; b < (int) (seconds * kRate / kBlock); ++b)
        {
            const double blockMs = kBlock * 1000.0 / kRate;
            for (; nextTickMs < nowMs + blockMs; nextTickMs += 2.0)
                stream.simulatorTick (nextTickMs);
            player.process (l.data(), r.data(), kBlock, nowMs);
            left.insert (left.end(), l.begin(), l.end());
            right.insert (right.end(), r.begin(), r.end());
            nowMs += blockMs;
        }
    }

    double rms (double fromSeconds, double toSeconds) const
    {
        double sum = 0.0;
        const auto from = (size_t) (fromSeconds * kRate), to = std::min (left.size(), (size_t) (toSeconds * kRate));
        for (size_t i = from; i < to; ++i)
            sum += 0.5 * ((double) left[i] * left[i] + (double) right[i] * right[i]);
        return std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    }

    float peak() const
    {
        float p = 0.0f;
        for (size_t i = 0; i < left.size(); ++i)
            p = std::max ({ p, std::abs (left[i]), std::abs (right[i]) });
        return p;
    }
};
} // namespace

int main()
{
    std::printf ("Simulated WARBL through the stream and the player:\n");
    {
        auto rigOnHeap = std::make_unique<Rig>();  // ~1 MB: more than a Windows thread's stack
        auto& rig = *rigOnHeap;
        rig.player.whistleModel.setGeometry (whistle::presets().front());
        rig.player.whistleModel.setVoicing (whistle::presetVoicing (0));
        rig.stream.setSimulating (true);
        rig.run (6.0);
        const auto stats = rig.stream.getStats();
        check (stats.frames > 2900 && stats.lostFrames == 0, "frames arrive every 2 ms, none lost", (double) stats.frames);
        check (rig.player.blocksProcessed.load() > 2000, "blocks processed", (double) rig.player.blocksProcessed.load());
        check (rig.rms (0.2, 1.0) > 0.01, "it plays (rms in the first note)", rig.rms (0.2, 1.0));
        check (rig.peak() <= 0.891f, "the limiter keeps it under -1 dBFS", rig.peak());
        check (rig.player.measuredPressure.load() >= 0.0f && rig.player.measuredCoverage[1].load() > -0.5f, "sensor readings shown", rig.player.measuredPressure.load());

        rig.stream.setSimulating (false);
        rig.run (3.0);
        check (rig.rms (8.0, 9.0) < 1.0e-5, "silent once the stream stops", rig.rms (8.0, 9.0));
        check (rig.player.resting.load(), "resting (model and room skipped)", rig.player.resting.load() ? 1.0 : 0.0);
    }

    std::printf ("Played from the screen, all holes closed (the reverb off):\n");
    {
        auto rigOnHeap = std::make_unique<Rig>();  // ~1 MB: more than a Windows thread's stack
        auto& rig = *rigOnHeap;
        rig.player.reverb.wetDb = -100.0f;
        rig.player.whistleModel.setGeometry (whistle::presets().front());
        rig.player.whistleModel.setVoicing (whistle::presetVoicing (0));
        for (auto& o : rig.player.manualOpenness)
            o = 0.0f;
        rig.player.manualPressure = 350.0f;
        rig.player.manualPlay = true;
        rig.run (1.5);
        std::vector<float> steady (rig.left.begin() + (long) (0.8 * kRate), rig.left.begin() + (long) (1.4 * kRate));
        double clarity = 0.0;
        const double hz = whistle::measurePitch (steady, kRate, clarity);
        const double cents = 1200.0 * std::log2 (hz / 587.33);
        check (std::abs (cents) < 50.0, "plays D5 (cents from 587.33 Hz)", cents);
    }

    std::printf ("Test scale:\n");
    {
        auto rigOnHeap = std::make_unique<Rig>();  // ~1 MB: more than a Windows thread's stack
        auto& rig = *rigOnHeap;
        const auto& g = whistle::presets().front();
        rig.player.whistleModel.setGeometry (g);
        rig.player.whistleModel.setVoicing (whistle::presetVoicing (0));
        rig.player.setScaleInstrument (g, 587.33);
        rig.player.playScale();
        for (int wait = 0; wait < 600 && ! rig.player.isScaleActive(); ++wait)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        for (int wait = 0; wait < 6000 && rig.player.getScaleStatus().rfind ("Scale:", 0) != 0; ++wait)
        {
            rig.run (0.01);  // The audio keeps running while it plans
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        const auto status = rig.player.getScaleStatus();
        check (status.rfind ("Scale:", 0) == 0, ("plays: \"" + status + "\"").c_str(), 0.0);
        const size_t before = rig.left.size();
        rig.run (2.0);
        check (rig.rms ((double) before / kRate + 0.5, (double) before / kRate + 2.0) > 0.01, "the scale sounds", rig.rms ((double) before / kRate + 0.5, (double) before / kRate + 2.0));
        rig.player.playScale();  // Stops it
        check (! rig.player.isScaleActive(), "stops", 0.0);
    }

    std::printf (failures == 0 ? "All player tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
