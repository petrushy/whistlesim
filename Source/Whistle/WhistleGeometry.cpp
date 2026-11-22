#include "WhistleGeometry.h"

#include "InstrumentFile.h"

#include <PresetData.h>

#include <algorithm>

namespace whistle
{

const std::vector<Material>& materials()
{
    static const std::vector<Material> list {
        { "Metal (brass, nickel)", 0.0005, 1.0 },
        { "Aluminium tube", 0.0010, 1.0 },
        { "Plastic (ABS, Delrin)", 0.0020, 1.05 },
        { "Wood", 0.0040, 1.3 },
    };
    return list;
}

bool moveFrontHoles (Geometry& g, int firstHole)
{
    std::vector<ToneHole> used;
    for (int i = 1; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            used.push_back (g.holes[(size_t) i]);
    if (firstHole < 1 || firstHole + (int) used.size() > kNumHoles)
        return false;

    for (int i = 1; i < kNumHoles; ++i)
        g.holes[(size_t) i] = {};
    for (size_t k = 0; k < used.size(); ++k)
        g.holes[(size_t) firstHole + k] = used[k];
    return true;
}

TuningGoal harjedalGoal()
{
    TuningGoal goal;
    goal.temperament = Temperament::harjedalSwedish;
    goal.fingerings["high D xxxxxx"] = true;   // Its high tonic: overblown, every hole covered
    goal.fingerings["high D oxxxxx"] = false;
    goal.fingerings["C nat oxxooo"] = false;   // No cross-fingered seventh in its scale
    return goal;
}

double presetLowestNote (const Geometry& g)
{
    if (g.tuning.lowestNote >= 0)  // The key and octave it was given
        return g.referenceA * std::pow (2.0, (g.tuning.lowestNote - 69) / 12.0);
    const double at440 = g.chart == FingeringChart::baroqueRecorder ? 523.25
                       : g.boreLength > 0.4                        ? 293.66
                                                                   : 587.33;
    return at440 * std::pow (2.0, g.chartTranspose / 12.0) * g.referenceA / 440.0;
}

int firstFrontHole (const Geometry& g)
{
    for (int i = 1; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            return i;
    return 0;
}

namespace
{
// The built-in presets: the .whistle files in Presets/ (built in as PresetData), in file-name order. Each whistle
// comes twice: on WARBL's L1-R3 sensors and on L2-R4 (the same instrument, played with the lower six fingers).
struct PresetList
{
    std::vector<Geometry> geometries;
    std::vector<Voicing> voicings;
};

const PresetList& presetList()
{
    static const PresetList list = []
    {
        std::vector<std::pair<juce::String, juce::String>> files;  // (file name, text)
        for (int i = 0; i < PresetData::namedResourceListSize; ++i)
        {
            int size = 0;
            if (const char* data = PresetData::getNamedResource (PresetData::namedResourceList[i], size))
                files.push_back ({ PresetData::originalFilenames[i], juce::String::fromUTF8 (data, size) });
        }
        std::sort (files.begin(), files.end());

        PresetList out;
        const Instrument base;  // The files give every value; anything missing stays at the defaults
        for (const auto& [file, text] : files)
        {
            juce::String error;
            const auto instrument = fromJson (text, error, &base);
            jassert (instrument.has_value());  // A broken preset file
            if (! instrument)
                continue;
            if (instrument->geometry.chart == FingeringChart::baroqueRecorder)  // Uses all eight holes
            {
                out.geometries.push_back (instrument->geometry);
                out.voicings.push_back (instrument->voicing);
                continue;
            }
            auto upper = instrument->geometry, lower = instrument->geometry;
            upper.name << ", holes L1-R3";
            lower.name << ", holes L2-R4";
            moveFrontHoles (lower, 2);
            for (const auto& g : { upper, lower })
            {
                out.geometries.push_back (g);
                out.voicings.push_back (instrument->voicing);
            }
        }
        return out;
    }();
    return list;
}
} // namespace

const std::vector<Geometry>& presets()
{
    return presetList().geometries;
}

Voicing presetVoicing (size_t index)
{
    return index < presetList().voicings.size() ? presetList().voicings[index] : Voicing();
}

} // namespace whistle
