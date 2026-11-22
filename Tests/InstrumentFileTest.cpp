// Whistle files: every preset survives a save/load round trip, partial files fill in defaults, and broken or
// meaningless files are rejected with a message.

#include "Whistle/Acoustics.h"
#include "Whistle/InstrumentFile.h"

#include <cmath>
#include <cstdio>

using namespace whistle;

static int failures = 0;

#define CHECK(condition)                                                \
    do                                                                  \
    {                                                                   \
        if (! (condition))                                              \
        {                                                               \
            std::printf ("FAILED line %d: %s\n", __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static bool near (double a, double b) { return std::abs (a - b) < 1.0e-6; }  // Files keep 0.001 mm

int main()
{
    // Round trip of every preset, with non-default voicing.
    for (const auto& preset : presets())
    {
        Instrument in;
        in.geometry = preset;
        setSlide (in.geometry, 0.0025);  // Pulled out, so the slide is saved too
        in.geometry.tuning.temperament = Temperament::meantone;
        in.geometry.tuning.towardEqual = 0.25;
        in.geometry.tuning.keys[5] = 0.5;
        in.geometry.tuning.keys[TuningGoal::kMinor + 9] = 0.75;
        in.geometry.tuning.fingerings["high D oxxxxx"] = false;
        in.geometry.tuning.topHoleForHighD = true;
        in.geometry.tuning.breathPosition = { { 0.4, 0.3 } };
        in.geometry.tuning.useLearnedBreath = true;
        in.geometry.tuning.learnedBreath["high A xxoooo"] = 1234.5;
        in.geometry.tuning.weights["B xooooo"] = 0.25;
        in.geometry.tuning.lowestNote = 66;  // F#4
        in.geometry.tuning.tuneHoleSizes = false;
        in.geometry.tuning.shapeHead = false;
        in.voicing = { 0.031f, 0.55f, 0.4f, -3.5f, 0.7f, 0.3f, 0.6f, 0.45f, 2.5f, 0.8f };
        in.voicing.jetBend = 7.5f;
        in.voicing.breathCurve = 1.45f;
        in.voicing.jetLowReynolds = 1050.0f;
        in.voicing.jetFlatAbove = 0.9f;
        in.voicing.jetFlatTop = 2.75f;

        juce::String error;
        const auto out = fromJson (toJson (in), error);
        CHECK (out.has_value());
        CHECK (error.isEmpty());
        if (! out)
            continue;

        const auto& a = in.geometry;
        const auto& b = out->geometry;
        CHECK (b.name == a.name);
        CHECK (near (a.boreLength, b.boreLength) && near (a.boreDiameterTop, b.boreDiameterTop) && near (a.boreDiameterFoot, b.boreDiameterFoot));
        CHECK (near (a.wallThickness, b.wallThickness) && near (a.wallLoss, b.wallLoss));
        for (int i = 0; i < kNumHoles; ++i)
        {
            CHECK (a.holes[(size_t) i].enabled == b.holes[(size_t) i].enabled);
            if (a.holes[(size_t) i].enabled)
                CHECK (near (a.holes[(size_t) i].position, b.holes[(size_t) i].position) && near (a.holes[(size_t) i].diameter, b.holes[(size_t) i].diameter));
        }
        CHECK (a.chart == b.chart && a.bellClosesFoot == b.bellClosesFoot);
        CHECK (a.chartTranspose == b.chartTranspose);
        CHECK (near (presetLowestNote (a), presetLowestNote (b)));
        CHECK (near (a.headLength, b.headLength) && near (a.headDiameter, b.headDiameter) && near (a.headStraightLength, b.headStraightLength));
        CHECK (near (a.slide, b.slide));
        CHECK (a.tuning.temperament == b.tuning.temperament && near (a.tuning.towardEqual, b.tuning.towardEqual));
        for (size_t k = 0; k < a.tuning.keys.size(); ++k)
            CHECK (near (a.tuning.keys[k], b.tuning.keys[k]));
        CHECK (a.tuning.fingerings == b.tuning.fingerings && a.tuning.topHoleForHighD == b.tuning.topHoleForHighD);
        CHECK (near (a.tuning.breathPosition[0], b.tuning.breathPosition[0]) && near (a.tuning.breathPosition[1], b.tuning.breathPosition[1]));
        CHECK (a.tuning.useLearnedBreath == b.tuning.useLearnedBreath && b.tuning.learnedBreath.size() == 1
               && near (b.tuning.learnedBreath.at ("high A xxoooo"), 1234.5));
        CHECK (b.tuning.weights.size() == 1 && near (b.tuning.weights.at ("B xooooo"), 0.25));
        CHECK (b.tuning.lowestNote == 66 && ! b.tuning.tuneHoleSizes && ! b.tuning.shapeHead);
        CHECK (near (a.windwayHeight, b.windwayHeight) && near (a.windwayLength, b.windwayLength) && near (a.windowLength, b.windowLength) && near (a.windowWidth, b.windowWidth)
               && near (a.edgeOffset, b.edgeOffset));
        CHECK (near (out->voicing.jetStrouhal, 0.031f) && near (out->voicing.jetConvection, 0.55f) && near (out->voicing.breathNoise, 0.4f)
               && near (out->voicing.outputGainDb, -3.5f) && near (out->voicing.directNoise, 0.7f)
               && near (out->voicing.windwayMass, 0.3f) && near (out->voicing.windwayFriction, 0.6f)
               && near (out->voicing.vortexLoss, 0.45f) && near (out->voicing.attackChiff, 2.5f)
               && near (out->voicing.fingerGapDamping, 0.8f));
        CHECK (near (out->voicing.jetBend, 7.5f) && near (out->voicing.breathCurve, 1.45f));
        CHECK (near (out->voicing.jetLowReynolds, 1050.0f) && near (out->voicing.jetFlatAbove, 0.9f) && near (out->voicing.jetFlatTop, 2.75f));
    }

    // An older Härjedalspipa file, with the chart of its own on A#: now the whistle chart a major third down, tuned to
    // its traditional scale.
    {
        juce::String error;
        const auto old = fromJson (R"({ "format": "warbl-whistle", "version": 1, "name": "Old pipa", "fingeringChart": "harjedalspipa", "chartTranspose": 0 })", error);
        CHECK (old.has_value());
        if (old)
        {
            CHECK (old->geometry.chart == FingeringChart::whistle && old->geometry.chartTranspose == -4);
            CHECK (old->geometry.tuning.temperament == Temperament::harjedalSwedish);
            CHECK (std::abs (presetLowestNote (old->geometry) - 466.16) < 0.05);
        }
    }

    // A file with only some fields: the rest come from the first preset.
    {
        juce::String error;
        const auto partial = fromJson (R"({ "format": "warbl-whistle", "version": 1, "name": "Short", "bore": { "length_mm": 250 } })", error);
        CHECK (partial.has_value());
        if (partial)
        {
            CHECK (partial->geometry.name == "Short");
            CHECK (partial->geometry.chartTranspose == 0);
            CHECK (near (partial->geometry.boreLength, 0.25));
            CHECK (near (partial->geometry.boreDiameterTop, presets().front().boreDiameterTop));
            CHECK (near (partial->geometry.holes[3].position, presets().front().holes[3].position));
            CHECK (near (partial->voicing.jetBend, 0.0f));  // Files from before the jet bend play as they did
            CHECK (near (partial->voicing.jetFlatAbove, 0.0f) && near (partial->voicing.jetLowReynolds, 0.0f));  // ... and the flattened jet
        }
    }

    // A file from before the thumb and R4 holes: six named holes, the others plugged.
    {
        juce::String error;
        const auto old = fromJson (R"({ "format": "warbl-whistle", "version": 1, "holes": [
            { "name": "L1", "position_mm": 120, "diameter_mm": 6 }, { "name": "L2", "position_mm": 140, "diameter_mm": 7 },
            { "name": "L3", "position_mm": 160, "diameter_mm": 6.5 }, { "name": "R1", "position_mm": 190, "diameter_mm": 7 },
            { "name": "R2", "position_mm": 200, "diameter_mm": 7 }, { "name": "R3", "position_mm": 225, "diameter_mm": 6 } ] })", error);
        CHECK (old.has_value());
        if (old)
        {
            CHECK (! old->geometry.holes[0].enabled && ! old->geometry.holes[7].enabled);
            CHECK (old->geometry.holes[1].enabled && near (old->geometry.holes[1].position, 0.120));
            CHECK (old->geometry.chart == FingeringChart::whistle);
        }
    }

    // Broken or meaningless files are rejected with a reason.
    auto rejected = [] (const char* json)
    {
        juce::String error;
        const auto result = fromJson (json, error);
        return ! result.has_value() && error.isNotEmpty();
    };
    CHECK (rejected ("{ not json"));
    CHECK (rejected (R"({ "format": "something else" })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 99 })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "chartTranspose": -25 })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "bore": { "length_mm": 10 } })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "holes": [ { "name": "L1", "position_mm": 900, "diameter_mm": 6 } ] })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "fipple": { "windwayHeight_mm": 0 } })"));

    std::printf (failures == 0 ? "All instrument file tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
