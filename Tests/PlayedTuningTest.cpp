// The presets are tuned to the pitch the model plays (PlayedTuning, from `WhistleCheck code`). This plays them and
// checks they still are: a change to the model or the acoustics that moves the played pitch shows here, and the
// presets then need retuning. The measured instruments keep a real whistle's bore and holes, however they play.

#include "Whistle/PlayedTuning.h"

#include <cstdio>

using namespace whistle;

int main()
{
    // Cents rms, weighted as the tuner weights notes. After tuning (2026-11-22) the whistles played 2.7-10.8 c, the
    // recorders 7.7-9.3 and the Härjedalspipa 6.8; the old resonance-tuned presets played 6-32 c.
    constexpr double kLimit = 11.0;
    int failures = 0, checked = 0;
    for (size_t i = 0; i < presets().size(); ++i)
    {
        const auto& g = presets()[i];
        if (g.name.contains ("(measured)") || g.name.contains ("holes L2-R4"))  // The L2-R4 copies are the same instruments
            continue;
        const auto notes = measurePlayed (g, presetVoicing (i), presetLowestNote (g));
        const double rms = playedRms (g, notes);
        int silent = 0;
        for (const auto& n : notes)
            silent += chartFor (g.chart)[n.chartIndex].counts && std::abs (n.resonance) < kCentsNotReachable && ! n.sounds ? 1 : 0;
        std::printf ("%-60s played %4.1f c rms%s\n", g.name.toRawUTF8(), rms, silent > 0 ? " (some notes don't sound)" : "");
        if (rms > kLimit || silent > 0)
        {
            std::printf ("FAILED %s: retune with WhistleCheck code\n", g.name.toRawUTF8());
            ++failures;
        }
        ++checked;
    }
    // The player's breath for notes not heard: interpolated by pitch between the heard ones (log pressure), carried on
    // beyond them with their trend.
    {
        auto g = presets().front();
        g.tuning.learnedBreath = { { "D xxxxxx", 200.0 }, { "A xxoooo", 400.0 } };
        auto breathOf = [&] (const char* name, const char* pattern, bool& estimated)
        {
            for (const auto& c : chartFor (g.chart))
                if (std::string (c.name) == name && std::string (c.pattern) == pattern)
                    return playerBreath (g, c, estimated);
            return -1.0;
        };
        bool estimatedD = true, estimatedE = false, estimatedB = false;
        const double d = breathOf ("D", "xxxxxx", estimatedD), e = breathOf ("E", "xxxxxo", estimatedE), b = breathOf ("high B", "xooooo", estimatedB);
        if (std::abs (d - 200.0) > 1.0e-6 || estimatedD || std::abs (e - 200.0 * std::pow (2.0, 2.0 / 7.0)) > 0.1 || ! estimatedE
            || std::abs (b - 1600.0) > 0.5 || ! estimatedB)
        {
            std::printf ("FAILED player breath: D %.1f, E %.1f (expected 243.8), high B %.1f (expected 1600)\n", d, e, b);
            ++failures;
        }
    }

    if (checked < 15)
    {
        std::printf ("FAILED only %d presets found\n", checked);
        ++failures;
    }
    std::printf (failures == 0 ? "All presets play in tune\n" : "%d presets out of tune\n", failures);
    return failures == 0 ? 0 : 1;
}
