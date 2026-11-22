// The presets are tuned to the pitch the model plays (PlayedTuning, from `WhistleCheck code`). This plays them and
// checks they still are: a change to the model or the acoustics that moves the played pitch shows here, and the
// presets then need retuning. The measured instruments keep a real whistle's bore and holes, however they play.

#include "Whistle/PitchDetect.h"
#include "Whistle/PlayedTuning.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace whistle;

int main()
{
    // Cents rms, weighted as the tuner weights notes. After tuning (2026-11-22: notes judged from where they speak,
    // the low octave at 35 % of its breath, the upper at 10 %) the whistles played 2.4-5.2 c, the low D 1.9-2.3, the
    // recorders 7.3-9.9 and the Härjedalspipa 4.0; the old resonance-tuned presets played 6-32 c.
    constexpr double kLimit = 11.0;
    int failures = 0, checked = 0;

    // The pitch detector (the tuner's, and the scale's): a whistle-like tone (fundamental and weaker harmonics, a little noise) within 1 c,
    // also when the second harmonic is the strongest; silence gives no pitch.
    {
        const double rate = 48000.0;
        for (const double hz : { 293.66, 587.33, 1108.73, 2349.3 })
            for (const double h2 : { 0.3, 1.5 })
            {
                std::vector<float> x (6000);
                unsigned noise = 1;
                for (size_t n = 0; n < x.size(); ++n)
                {
                    const double t = 2.0 * std::numbers::pi * hz * (double) n / rate;
                    noise = noise * 1664525u + 1013904223u;
                    x[n] = (float) (0.3 * std::sin (t) + 0.3 * h2 * std::sin (2.0 * t + 0.4) + 0.05 * std::sin (3.0 * t)
                                    + 0.01 * ((double) (noise >> 8) / (1 << 24) - 0.5));
                }
                double clarity = 0.0;
                const double found = measurePitch (x, rate, clarity);
                const double error = found > 0.0 ? 1200.0 * std::log2 (found / hz) : 1.0e9;
                if (std::abs (error) > 1.0)
                {
                    std::printf ("FAILED pitch detector: %.2f Hz (H2 %.1f) read as %.2f Hz\n", hz, h2, found);
                    ++failures;
                }
            }
        std::vector<float> silence (6000, 0.0f);
        double clarity = 0.0;
        if (measurePitch (silence, rate, clarity) > 0.0)
        {
            std::printf ("FAILED pitch detector: silence has a pitch\n");
            ++failures;
        }
    }
    for (size_t i = 0; i < presets().size(); ++i)
    {
        const auto& g = presets()[i];
        if (g.name.find ("(measured)") != std::string::npos || g.name.find ("holes L2-R4") != std::string::npos)  // The L2-R4 copies are the same instruments
            continue;
        const auto notes = measurePlayed (g, presetVoicing (i), presetLowestNote (g));
        const double rms = playedRms (g, notes);
        int silent = 0;
        for (const auto& n : notes)
            silent += chartFor (g.chart)[n.chartIndex].counts && std::abs (n.resonance) < kCentsNotReachable && ! n.sounds ? 1 : 0;
        std::printf ("%-60s played %4.1f c rms%s\n", g.name.c_str(), rms, silent > 0 ? " (some notes don't sound)" : "");
        if (rms > kLimit || silent > 0)
        {
            std::printf ("FAILED %s: retune with WhistleCheck code\n", g.name.c_str());
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
