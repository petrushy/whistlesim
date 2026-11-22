#include "InstrumentFile.h"
#include "Acoustics.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace whistle
{

namespace
{
constexpr const char* kFormat = "warbl-whistle";
constexpr int kVersion = 1;

const char* temperamentName (Temperament t)
{
    switch (t)
    {
        case Temperament::meantone: return "quarter-comma meantone";
        case Temperament::just: return "just";
        case Temperament::bestForKeys: return "best for the keys";
        case Temperament::harjedalSwedish: return "harjedalspipa (Swedish Wikipedia)";
        case Temperament::harjedalEnglish: return "harjedalspipa (English Wikipedia)";
        case Temperament::custom: return "custom";
        case Temperament::equal: break;
    }
    return "equal";
}

json::Value object()
{
    return json::Value::object();
}

void set (json::Value& o, const char* key, double value)
{
    o.set (key, value);
}

// Reads o[key] into value if it's there; lengths are stored in mm.
void read (const json::Value& o, const char* key, double& value, double scale = 1.0)
{
    if (o.has (key))
        value = o[key].toDouble() * scale;
}

void read (const json::Value& o, const char* key, float& value)
{
    if (o.has (key))
        value = (float) o[key].toDouble();
}

bool startsWith (const std::string& s, std::string_view prefix)
{
    return s.compare (0, prefix.size(), prefix) == 0;
}

std::string trimmed (const std::string& s)
{
    const auto first = s.find_first_not_of (" \t\r\n");
    if (first == std::string::npos)
        return {};
    return s.substr (first, s.find_last_not_of (" \t\r\n") - first + 1);
}

bool equalsIgnoringCase (const std::string& a, const char* b)
{
    const std::string_view other (b);
    if (a.size() != other.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower ((unsigned char) a[i]) != std::tolower ((unsigned char) other[i]))
            return false;
    return true;
}
} // namespace

json::Value toValue (const Instrument& instrument)
{
    const auto& g = instrument.geometry;
    auto root = object();
    root.set ("format", kFormat);
    root.set ("version", kVersion);
    root.set ("name", g.name);

    auto bore = object();
    set (bore, "length_mm", g.boreLength * 1000.0);
    set (bore, "diameterTop_mm", g.boreDiameterTop * 1000.0);
    set (bore, "diameterFoot_mm", g.boreDiameterFoot * 1000.0);
    set (bore, "wallThickness_mm", g.wallThickness * 1000.0);
    set (bore, "wallLoss", g.wallLoss);
    set (bore, "headLength_mm", g.headLength * 1000.0);
    set (bore, "headDiameter_mm", g.headDiameter * 1000.0);
    set (bore, "headStraightLength_mm", g.headStraightLength * 1000.0);
    set (bore, "slide_mm", g.slide * 1000.0);  // How far the tuning slide is out; length and holes include it
    root.set ("bore", bore);

    // Only the holes that are used; the others are plugged.
    auto holes = json::Value::array();
    for (int i = 0; i < kNumHoles; ++i)
    {
        if (! g.holes[(size_t) i].enabled)
            continue;
        auto hole = object();
        hole.set ("name", kHoleNames[(size_t) i]);
        set (hole, "position_mm", g.holes[(size_t) i].position * 1000.0);
        set (hole, "diameter_mm", g.holes[(size_t) i].diameter * 1000.0);
        holes.add (std::move (hole));
    }
    root.set ("holes", holes);
    root.set ("bellClosesFoot", g.bellClosesFoot);
    root.set ("fingeringChart", g.chart == FingeringChart::baroqueRecorder ? "recorder (baroque)" : "whistle");
    root.set ("referenceA_Hz", g.referenceA);
    root.set ("chartTranspose", g.chartTranspose);

    // What the tuner aims for (the Tuning workshop tab): keys by their tonic's semitones above the lowest note.
    auto tuning = object();
    tuning.set ("temperament", temperamentName (g.tuning.temperament));
    set (tuning, "towardEqual", g.tuning.towardEqual);
    auto major = json::Value::array(), minor = json::Value::array();
    for (int k = 0; k < 12; ++k)
    {
        major.add (g.tuning.keys[(size_t) k]);
        minor.add (g.tuning.keys[(size_t) (k + TuningGoal::kMinor)]);
    }
    tuning.set ("majorKeys", major);
    tuning.set ("minorKeys", minor);
    auto fingerings = object();
    for (const auto& [id, counts] : g.tuning.fingerings)
        fingerings.set (id, counts);
    tuning.set ("fingerings", fingerings);
    tuning.set ("topHoleFor", g.tuning.topHoleForHighD ? "high D" : "C natural");
    if (g.tuning.lowestNote >= 0)  // As a note name, e.g. "D5"
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        tuning.set ("lowestNote", std::string (names[g.tuning.lowestNote % 12]) + std::to_string (g.tuning.lowestNote / 12 - 1));
    }
    tuning.set ("tuneHoleSizes", g.tuning.tuneHoleSizes);
    tuning.set ("shapeHead", g.tuning.shapeHead);
    auto position = json::Value::array();
    for (double p : g.tuning.breathPosition)
        position.add (p);
    tuning.set ("breathPosition", position);
    tuning.set ("useLearnedBreath", g.tuning.useLearnedBreath);
    auto custom = json::Value::array();
    for (double c : g.tuning.custom)
        custom.add (c);
    tuning.set ("customCents", custom);
    auto learned = object();
    for (const auto& [id, pressure] : g.tuning.learnedBreath)
        learned.set (id, pressure);
    tuning.set ("learnedBreath_Pa", learned);
    auto weights = object();
    for (const auto& [id, weight] : g.tuning.weights)
        weights.set (id, weight);
    tuning.set ("weights", weights);
    root.set ("tuning", tuning);

    auto fipple = object();
    set (fipple, "windwayHeight_mm", g.windwayHeight * 1000.0);
    set (fipple, "windwayLength_mm", g.windwayLength * 1000.0);
    set (fipple, "windowLength_mm", g.windowLength * 1000.0);
    set (fipple, "windowWidth_mm", g.windowWidth * 1000.0);
    set (fipple, "edgeOffset_mm", g.edgeOffset * 1000.0);
    root.set ("fipple", fipple);

    const auto& v = instrument.voicing;
    auto voicing = object();
    set (voicing, "jetStrouhal", v.jetStrouhal);
    set (voicing, "jetConvection", v.jetConvection);
    set (voicing, "breathNoise", v.breathNoise);
    set (voicing, "outputGain_dB", v.outputGainDb);
    set (voicing, "directNoise", v.directNoise);
    set (voicing, "windwayMass", v.windwayMass);
    set (voicing, "windwayFriction", v.windwayFriction);
    set (voicing, "vortexLoss", v.vortexLoss);
    set (voicing, "attackChiff", v.attackChiff);
    set (voicing, "fingerGapDamping", v.fingerGapDamping);
    set (voicing, "jetGain", v.jetGain);
    set (voicing, "slowJetGrowth", v.slowJetGrowth);
    voicing.set ("jetStages", v.jetStages);
    set (voicing, "breathScale", v.breathScale);
    set (voicing, "breathCurve", v.breathCurve);
    set (voicing, "jetLowReynolds", v.jetLowReynolds);
    set (voicing, "jetFlatAbove", v.jetFlatAbove);
    set (voicing, "jetFlatTop", v.jetFlatTop);
    set (voicing, "jetSpreading", v.jetSpreading);
    voicing.set ("jetModel", v.jetModel);
    set (voicing, "jetThinning", v.jetThinning);
    set (voicing, "jetCutoff", v.jetCutoff);
    set (voicing, "jetBend_Pa", v.jetBend);
    set (voicing, "fingerTap", v.fingerTap);
    set (voicing, "fingerSoftness_ms", v.fingerSoftness);
    set (voicing, "bodyKnock", v.bodyKnock);
    set (voicing, "bodyPitch_Hz", v.bodyPitch);
    set (voicing, "bodyRing", v.bodyRing);
    root.set ("voicing", voicing);

    return root;
}

std::optional<Instrument> fromValue (const json::Value& root, std::string& error, const Instrument* base)
{
    if (! root.isObject() || root["format"].toString() != kFormat)
    {
        error = "Not a WARBL whistle file";
        return std::nullopt;
    }
    if (root["version"].toInt() > kVersion)
    {
        error = "Made by a newer version of WhistleSim";
        return std::nullopt;
    }

    Instrument instrument;
    if (base != nullptr)
        instrument = *base;
    else
        instrument.geometry = presets().front();
    auto& g = instrument.geometry;
    if (root.has ("name"))
        g.name = root["name"].toString();

    const auto& bore = root["bore"];
    read (bore, "length_mm", g.boreLength, 0.001);
    read (bore, "diameterTop_mm", g.boreDiameterTop, 0.001);
    read (bore, "diameterFoot_mm", g.boreDiameterFoot, 0.001);
    read (bore, "wallThickness_mm", g.wallThickness, 0.001);
    read (bore, "wallLoss", g.wallLoss);
    read (bore, "headLength_mm", g.headLength, 0.001);
    read (bore, "headDiameter_mm", g.headDiameter, 0.001);
    read (bore, "headStraightLength_mm", g.headStraightLength, 0.001);  // Missing in older files: 0, as before
    read (bore, "slide_mm", g.slide, 0.001);                            // Missing in older files: 0 (pushed in)

    // Holes are listed by name; the ones not listed are plugged.
    if (const auto& holes = root["holes"]; holes.isArray())
    {
        for (auto& h : g.holes)
            h.enabled = false;
        for (const auto& entry : holes.items())
        {
            const auto name = entry["name"].toString();
            for (int i = 0; i < kNumHoles; ++i)
            {
                if (name != kHoleNames[(size_t) i])
                    continue;
                auto& hole = g.holes[(size_t) i];
                hole.enabled = true;
                read (entry, "position_mm", hole.position, 0.001);
                read (entry, "diameter_mm", hole.diameter, 0.001);
            }
        }
    }
    if (root.has ("bellClosesFoot"))
        g.bellClosesFoot = root["bellClosesFoot"].toBool();
    // Older files had a chart of the Härjedalspipa's own, on A#: now the whistle chart a major third down, with its
    // scale as a tuning (converted below).
    bool oldHarjedalChart = false;
    if (root.has ("fingeringChart"))
    {
        const auto chart = root["fingeringChart"].toString();
        oldHarjedalChart = startsWith (chart, "harjedalspipa");
        g.chart = startsWith (chart, "recorder") ? FingeringChart::baroqueRecorder : FingeringChart::whistle;
    }
    if (root.has ("referenceA_Hz"))
        g.referenceA = root["referenceA_Hz"].toDouble();
    if (root.has ("chartTranspose"))
        g.chartTranspose = root["chartTranspose"].toInt();

    // Missing in older files: equal temperament on the home key, as they were tuned.
    g.tuning = {};
    if (const auto& tuning = root["tuning"]; tuning.isObject())
    {
        const auto name = tuning["temperament"].toString();
        for (auto t : { Temperament::equal, Temperament::meantone, Temperament::just, Temperament::bestForKeys, Temperament::harjedalSwedish,
                        Temperament::harjedalEnglish, Temperament::custom })
            if (name == temperamentName (t))
                g.tuning.temperament = t;
        read (tuning, "towardEqual", g.tuning.towardEqual);
        for (const auto& [property, offset] : { std::pair { "majorKeys", 0 }, std::pair { "minorKeys", TuningGoal::kMinor } })
            if (const auto& keys = tuning[property]; keys.isArray())
                for (size_t k = 0; k < 12 && k < keys.items().size(); ++k)
                    g.tuning.keys[k + (size_t) offset] = keys.items()[k].toDouble();
        g.tuning.topHoleForHighD = tuning["topHoleFor"].toString() == "high D";
        if (tuning.has ("lowestNote"))
        {
            // "D5", "F#4", "Bb3": the note name, then the octave.
            const auto text = trimmed (tuning["lowestNote"].toString());
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
            const auto letters = text.substr (0, std::min (text.size(), text.find_first_of ("-0123456789")));
            const int octave = (int) std::strtol (text.c_str() + letters.size(), nullptr, 10);
            for (int i = 0; i < 12; ++i)
                if (equalsIgnoringCase (letters, names[i]) || equalsIgnoringCase (letters, flats[i]))
                    g.tuning.lowestNote = limit (0, 127, (octave + 1) * 12 + i);
        }
        if (tuning.has ("tuneHoleSizes"))
            g.tuning.tuneHoleSizes = tuning["tuneHoleSizes"].toBool();
        if (tuning.has ("shapeHead"))
            g.tuning.shapeHead = tuning["shapeHead"].toBool();
        if (const auto& position = tuning["breathPosition"]; position.isArray())
            for (size_t i = 0; i < 2 && i < position.items().size(); ++i)
                g.tuning.breathPosition[i] = limit (0.0, 1.0, position.items()[i].toDouble());
        g.tuning.useLearnedBreath = tuning["useLearnedBreath"].toBool();
        if (const auto& custom = tuning["customCents"]; custom.isArray())
            for (size_t d = 0; d < 12 && d < custom.items().size(); ++d)
                g.tuning.custom[d] = custom.items()[d].toDouble();
        for (const auto& [id, value] : tuning["learnedBreath_Pa"].members())
            g.tuning.learnedBreath[id] = value.toDouble();
        for (const auto& [id, value] : tuning["weights"].members())
            g.tuning.weights[id] = limit (0.0, 1.0, value.toDouble());
        for (const auto& [id, value] : tuning["fingerings"].members())
            g.tuning.fingerings[id] = value.toBool();
    }

    if (oldHarjedalChart)
    {
        g.chartTranspose -= 4;  // Its chart was on A#, the whistle chart's on D
        if (! root["tuning"].isObject())
            g.tuning = harjedalGoal();
    }

    const auto& fipple = root["fipple"];
    read (fipple, "windwayHeight_mm", g.windwayHeight, 0.001);
    read (fipple, "windwayLength_mm", g.windwayLength, 0.001);
    read (fipple, "windowLength_mm", g.windowLength, 0.001);
    read (fipple, "windowWidth_mm", g.windowWidth, 0.001);
    read (fipple, "edgeOffset_mm", g.edgeOffset, 0.001);

    const auto& voicing = root["voicing"];
    read (voicing, "jetStrouhal", instrument.voicing.jetStrouhal);
    read (voicing, "jetConvection", instrument.voicing.jetConvection);
    read (voicing, "breathNoise", instrument.voicing.breathNoise);
    read (voicing, "outputGain_dB", instrument.voicing.outputGainDb);
    read (voicing, "directNoise", instrument.voicing.directNoise);
    read (voicing, "windwayMass", instrument.voicing.windwayMass);
    read (voicing, "windwayFriction", instrument.voicing.windwayFriction);
    read (voicing, "vortexLoss", instrument.voicing.vortexLoss);
    read (voicing, "attackChiff", instrument.voicing.attackChiff);
    read (voicing, "fingerGapDamping", instrument.voicing.fingerGapDamping);
    // Files from before these existed keep the jet they were made with (the Voicing defaults).
    read (voicing, "jetGain", instrument.voicing.jetGain);
    read (voicing, "slowJetGrowth", instrument.voicing.slowJetGrowth);
    read (voicing, "breathScale", instrument.voicing.breathScale);
    read (voicing, "breathCurve", instrument.voicing.breathCurve);
    read (voicing, "jetLowReynolds", instrument.voicing.jetLowReynolds);
    read (voicing, "jetFlatAbove", instrument.voicing.jetFlatAbove);
    read (voicing, "jetFlatTop", instrument.voicing.jetFlatTop);
    read (voicing, "jetSpreading", instrument.voicing.jetSpreading);
    read (voicing, "jetBend_Pa", instrument.voicing.jetBend);
    read (voicing, "fingerTap", instrument.voicing.fingerTap);
    read (voicing, "fingerSoftness_ms", instrument.voicing.fingerSoftness);
    read (voicing, "bodyKnock", instrument.voicing.bodyKnock);
    read (voicing, "bodyPitch_Hz", instrument.voicing.bodyPitch);
    read (voicing, "bodyRing", instrument.voicing.bodyRing);
    if (voicing.has ("jetStages"))
        instrument.voicing.jetStages = limit (1, 2, voicing["jetStages"].toInt());
    read (voicing, "jetThinning", instrument.voicing.jetThinning);
    read (voicing, "jetCutoff", instrument.voicing.jetCutoff);
    if (voicing.has ("jetModel"))
        instrument.voicing.jetModel = limit (0, 1, voicing["jetModel"].toInt());

    // Sanity checks: anything that would make the model meaningless.
    auto bad = [&] (bool condition, const char* message)
    {
        if (condition && error.empty())
            error = message;
        return condition;
    };
    bad (g.boreLength < 0.05 || g.boreLength > 2.0, "Bore length must be between 50 and 2000 mm");
    bad (g.boreDiameterTop < 0.002 || g.boreDiameterFoot < 0.002 || g.headDiameter < 0.002, "Bore diameters must be at least 2 mm");
    bad (g.headLength < 0.0 || g.headLength >= g.boreLength, "The head section must be shorter than the bore");
    bad (g.headStraightLength < 0.0 || (g.headStraightLength > 0.0 && g.headStraightLength >= g.headLength),
         "The head's straight part must be shorter than the head section");
    bad (g.tuning.towardEqual < 0.0 || g.tuning.towardEqual > 1.0, "Toward equal must be between 0 and 1");
    for (double w : g.tuning.keys)
        bad (w < 0.0 || w > 1.0, "Key weights must be between 0 and 1");
    bad (g.slide < kSlideMin - 1.0e-9 || g.slide > kSlideMax + 1.0e-9, "The tuning slide must be between -15 and 30 mm");
    bad (g.referenceA < 380.0 || g.referenceA > 500.0, "The reference A must be between 380 and 500 Hz");
    bad (g.chartTranspose < -24 || g.chartTranspose > 24, "Chart transposition must be between -24 and 24 semitones");
    bad (g.windwayHeight <= 0.0 || g.windwayLength <= 0.0 || g.windowLength <= 0.0 || g.windowWidth <= 0.0, "Fipple sizes must be positive");
    for (const auto& h : g.holes)
        if (h.enabled)
            bad (h.diameter <= 0.0 || h.position <= 0.0 || h.position >= g.boreLength, "Every hole needs a positive size and a position inside the bore");
    if (! error.empty())
        return std::nullopt;

    return instrument;
}

std::string toJson (const Instrument& instrument, bool oneLine)
{
    if (oneLine)
        return json::write (toValue (instrument), true);
    return json::write (toValue (instrument), false, 6);  // Lengths to 1 nm: presets keep their exact values
}

std::optional<Instrument> fromJson (std::string_view text, std::string& error, const Instrument* base)
{
    std::string reason;
    const auto parsed = json::parse (text, reason);
    if (! parsed)
    {
        error = "Not valid JSON: " + reason;
        return std::nullopt;
    }
    return fromValue (*parsed, error, base);
}

} // namespace whistle
