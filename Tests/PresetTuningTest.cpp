// The presets' charts and keys, the L1-R3 / L2-R4 copies, and the tuning slide. Whether they play in tune is
// PlayedTuningTest's (it plays the model).

#include "Whistle/Acoustics.h"

#include <cmath>
#include <cstdio>

using namespace whistle;

int main()
{
    int failures = 0;
    bool foundAlto = false, foundTenor = false;
    for (const auto& g : presets())
    {
        const double lowest = presetLowestNote (g);
        const bool alto = g.name.startsWith ("Alto recorder"), tenor = g.name.startsWith ("Tenor recorder");
        foundAlto = foundAlto || alto;
        foundTenor = foundTenor || tenor;
        if ((alto || tenor) && (g.chart != FingeringChart::baroqueRecorder || ! g.bellClosesFoot
                               || std::abs (lowest - (alto ? 349.228 : 261.626)) > 0.01))
        {
            std::printf ("FAILED recorder key/chart: %s, %.2f Hz\n", g.name.toRawUTF8(), lowest);
            ++failures;
        }
    }
    if (! foundAlto || ! foundTenor)
    {
        std::printf ("FAILED missing alto or tenor recorder preset\n");
        ++failures;
    }
    // A whistle moved from WARBL's L1-R3 sensors to L2-R4 is the same instrument: identical predicted tuning.
    for (const auto& preset : presets())
    {
        if (preset.chart == FingeringChart::baroqueRecorder)
            continue;
        auto moved = preset;
        if (! moveFrontHoles (moved, 2) || firstFrontHole (moved) != 2 || moved.holes[1].enabled || ! moved.holes[7].enabled)
        {
            std::printf ("FAILED moving %s to L2-R4\n", preset.name.toRawUTF8());
            ++failures;
            continue;
        }
        for (const auto& c : chartFor (preset.chart))
        {
            const double before = predictedCents (preset, c, 587.33), after = predictedCents (moved, c, 587.33);
            if (std::abs (before - after) > 0.01)
            {
                std::printf ("FAILED %s on L2-R4, %s: %+.2f vs %+.2f cents\n", preset.name.toRawUTF8(), c.name, after, before);
                ++failures;
            }
        }
    }

    // The tuning slide: pulling it out moves the tube below the joint (holes and taper with it) and flattens every
    // note; pushing it back gives the same instrument; and pulled out, its best setting is found where it was.
    for (const auto& preset : presets())
    {
        const double lowest = presetLowestNote (preset);
        auto out = preset;
        setSlide (out, 0.004);
        auto back = out;
        setSlide (back, 0.0);
        for (int i = 0; i < kNumHoles; ++i)
        {
            const auto& hole = preset.holes[(size_t) i];
            if (hole.enabled && std::abs (out.radiusAt (out.holes[(size_t) i].position) - preset.radiusAt (hole.position)) > 1.0e-9)
            {
                std::printf ("FAILED %s: the bore at %s changes width with the slide\n", preset.name.toRawUTF8(), kHoleNames[(size_t) i]);
                ++failures;
            }
        }
        for (const auto& c : chartFor (preset.chart))
        {
            const double made = predictedCents (preset, c, lowest);
            if (std::abs (made) >= kCentsNotReachable)
                continue;
            const double pulled = predictedCents (out, c, lowest), returned = predictedCents (back, c, lowest);
            if (pulled > made - 1.0 || std::abs (returned - made) > 0.01)
            {
                std::printf ("FAILED %s slide, %s: %+.2f made, %+.2f pulled out 4 mm, %+.2f pushed back\n", preset.name.toRawUTF8(),
                             c.name, made, pulled, returned);
                ++failures;
            }
        }
        const double made = bestSlide (preset, lowest), pulled = bestSlide (out, lowest);
        if (std::abs (pulled - made) > 0.0002)
        {
            std::printf ("FAILED %s: best slide %.2f mm, pulled out 4 mm %.2f mm\n", preset.name.toRawUTF8(), made * 1000.0, pulled * 1000.0);
            ++failures;
        }
    }

    std::printf (failures == 0 ? "All preset checks passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
