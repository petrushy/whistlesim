#include "WhistleGeometry.h"

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

Voicing presetVoicing (size_t index)
{
    // The recorder's jet band is set higher than the whistles' so its second- and third-octave notes speak at
    // lower breath (top C at ~3 kPa, C6 at ~0.6 kPa; at 0.035 the low C goes sharp); the whistles' defaults give
    // their register changes at ~1 and ~4 kPa. One band-pass stage: no silent pressures between the registers.
    Voicing v;

    // Searched against the recorded whistles (first octave H2/H3 about -17 dB, second octave -32/-28) and for steady
    // playing. One band-pass stage is what steadies quick fingering changes: register jumps at soft breath went from
    // 7 of 14 to 1, settling from ~200 ms to ~6 ms. Jet spreading makes the second octave purer and louder, less
    // vortex loss gives the first octave back its odd harmonics, and slow-jet growth separates the two octaves.
    // (Their edge offsets are set in presets().) The low D keeps its own: spreading didn't help it.
    struct Fitted { const char* namePart; float spreading, slowGrowth, gain; };
    static constexpr Fitted fitted[] = { { "cylindrical metal", 3.0f, 0.25f, 1.0f },
                                         { "conical metal", 2.0f, 0.5f, 1.0f },
                                         { "wide head", 3.0f, 0.5f, 1.15f },
                                         { "traditional scale", 2.0f, 0.25f, 1.0f } };
    if (index < presets().size())
        for (const auto& f : fitted)
            if (presets()[index].name.contains (f.namePart))
            {
                v.jetSpreading = f.spreading;
                v.slowJetGrowth = f.slowGrowth;
                v.jetGain = f.gain;
                v.vortexLoss = 0.6f;
                v.jetStages = 1;
            }

    const juce::String generation ("D whistle, Generation style"), lof (juce::String::fromUTF8 ("D whistle, Löf style"));
    if (index < presets().size()
        && (presets()[index].name.startsWith ("D whistle, Golden") || presets()[index].name.contains ("whistle, wood")
            || presets()[index].name.startsWith (generation) || presets()[index].name.startsWith (lof)))
    {
        // Fitted to Real-examples/High-D-Golden.wav (WhistleCheck analyse): the thin windway wants a lower jet band,
        // one band-pass stage (no silent pressures between registers), and the jet settings that make a softly blown
        // low note swing far across the edge (rich, as the recording's bottom D) while the hard-blown second octave
        // stays nearly pure.
        v.jetStrouhal = 0.017f;
        v.jetStages = 1;
        v.slowJetGrowth = 0.25f;
        v.jetGain = 1.1f;  // 1.3 chirped through another register as a hole opened gradually (WhistleCheck glide)
        // The jet widens as it turns turbulent (the second octave's faster jet more): that octave comes out louder
        // and purer, as on the recorded tune (second-octave H2 -30 dB, was -17; tune -34). Less vortex loss gives
        // the softer first octave back its odd harmonics.
        v.jetSpreading = 3.0f;
        v.vortexLoss = 0.6f;
        // Breath noise as the RODE recordings of the Golden and Lof: -66 to -69 dB between the harmonics in both
        // octaves (0.05 gave -64 in the second octave as played).
        v.breathNoise = 0.04f;
    }
    if (index < presets().size() && presets()[index].name.startsWith ("D whistle, Golden style (bright)"))
    {
        // The Golden style voiced for the upper harmonics of the recorded Golden (Real-examples/Golden-High-D-RODE.wav,
        // WhistleCheck analyse): a jet of constant width (no spreading) and little vortex loss switches harder at the
        // edge, which is where the harmonics from the fourth up come from (H4-H8 within ~5 dB of the recording in
        // both octaves; the Golden style is 9-23 dB short of them). The jet bend, with the edge nearly centred, makes
        // the second octave purer instead of the spreading (H2 -27 dB there, -18 in the first). The jet band is a
        // little higher, which keeps the octave breaks where the Golden style has them. Still too strong: H3, by ~7 dB.
        // Played on 2026-11-10: good, if a little too bright.
        v.jetStrouhal = 0.018f;
        v.jetSpreading = 0.0f;
        v.vortexLoss = 0.18f;
        v.slowJetGrowth = 0.45f;
        v.jetBend = 4.5f;
        v.breathNoise = 0.055f;  // The recording's -66 dB between the harmonics (2-4 kHz) in both octaves
    }
    if (index < presets().size() && presets()[index].name.startsWith ("D whistle, Golden (measured)"))
    {
        // Searched against a recording of this whistle (Real-examples/Golden-High-D-RODE-measured.wav, fit_voicing.py
        // measured pernote=0.5). How bright a note is above H6 follows how far the jet swings across the edge, in its
        // half-widths (WhistleCheck inside high): ~2 gives H7-H10 near -55 dB, ~4 near -38; the recording wants
        // ~2.5-3. The Golden style's spreading swings 1.5-2 and less the harder it is blown (no response to breath);
        // vortex loss 0.10 let it swing 3.5-4 on every note (A and B 15-20 dB too bright above H7). 0.24 swings ~3
        // in the first octave and ~2 in the second. Little spreading keeps the second octave from going dull.
        v.jetStrouhal = 0.0185f;
        v.jetSpreading = 0.1f;
        v.vortexLoss = 0.24f;
        v.jetGain = 0.96f;
        v.slowJetGrowth = 0.52f;
        v.jetBend = 8.7f;
    }
    if (index < presets().size() && presets()[index].name.startsWith ("D whistle, Golden (measured), real pressure"))
    {
        // The same whistle played at the mouth pressures measured on it (Real-examples/Golden-pressure-1-swells-*,
        // WARBL's mouthpiece in the mouth beside the whistle's): the swells went from ~150 to ~310 Pa on D5, G5
        // ~190-700, B5 ~300-880, D6 ~420-1000, where the voicing above needs 1.3-3 x that. Searched against those
        // swells, pressure by pressure (level, pitch, H2, H3, H5, H7-H10): the jet's band higher (its notes speak and
        // break at lower pressure), a little less jet gain and bend. Its first octave is in tune within ~1.5 x of the
        // measured scale's pressures. The swells stopped short of the breaks: blown through them (Golden-pressure-3-
        // breaks-*), its breaks upward are close to the real ones, but its upper registers fall back at ~1.4 x the
        // real pressure (see "real pressure and breaks" below).
        v.jetStrouhal = 0.03f;
        v.jetGain = 0.76f;
        v.jetBend = 5.7f;
        v.slowJetGrowth = 0.42f;
        v.breathNoise = 0.055f;  // At these pressures 0.04 was 2-3 dB under the recording's breath noise
        // WARBL's breath straight through: the breath response's ~2 x (outputMax 4930 Pa at 2500 Pa in, curve 34,
        // exponent 0.648) undone, so the model gets the mouth pressure WARBL measures (less its 10 Pa threshold).
        v.breathScale = 0.505f;
        v.breathCurve = 1.543f;
    }
    if (index < presets().size() && presets()[index].name.startsWith ("D whistle, Golden (measured), real pressure and breaks"))
    {
        // The voicing above holds each upper register only down to ~1.6 x the pressure where the real whistle's
        // drops back (Real-examples/Golden-pressure-3-breaks-*: blown with the measured breath, D6 falls back to D5
        // at 440-530 Pa, the real one at ~335; D6 never reaches the third register, the real one does at ~1560 Pa).
        // The jet flattened above its band (Voicing::jetFlatAbove) gives the upper registers their gain; less of it
        // at low Reynolds numbers keeps the low notes from starting at a sigh. Searched against the swells (tone),
        // the breaks recording (where registers change, on the measured breath) and a tune played with measured
        // breath (Golden-pressure-4-tune-Whalley-Range-*: the note played, and its pitch): breaks up 0.99 x the real
        // pressure, third register 1.01 x, falling back 1.26 x (was 1.44 x); the tune's notes in the right register
        // 91 % of the time (was 85 %), 7 c sharp.
        v.jetFlatAbove = 1.0f;
        v.jetFlatTop = 2.75f;
        v.jetLowReynolds = 1050.0f;
        v.jetGain = 0.56f;
        v.vortexLoss = 0.132f;
        v.jetConvection = 0.575f;
        v.slowJetGrowth = 0.77f;
    }
    // Two more high D's on the Golden fipple, voiced by a search against RODE recordings of real whistles
    // (Real-examples/*-RODE.wav, WhistleCheck analyse, octave averages of H2-H5 note by note), then checked with
    // transition, glide and breath (no register jumps at 600 Pa; glides as clean as the Golden style's).
    if (index < presets().size() && presets()[index].name.startsWith (generation))
    {
        // The recorded Generation: strong even, weak odd harmonics in the first octave (H2/H3 -13/-22 dB). Less
        // spreading and more vortex loss keep its swing short of the square, odd-rich tone.
        v.jetSpreading = 0.5f;
        v.vortexLoss = 0.84f;
        v.jetGain = 1.0f;
        // The quietest of the recorded whistles between its harmonics (-75 and -68 dB in the two octaves): with the
        // Golden fipple's 0.04 the model was 7-11 dB noisier.
        v.breathNoise = 0.016f;
    }
    if (index < presets().size() && presets()[index].name.startsWith (lof))
    {
        // The recorded Löf: a rich first octave (H2/H3 -11/-13 dB) and a pure second (-31/-27): less vortex
        // loss for the first, spreading for the second.
        v.jetSpreading = 2.0f;
        v.vortexLoss = 0.42f;
        v.jetGain = 1.1f;
    }
    if (index < presets().size() && presets()[index].name.startsWith ("Low D"))
    {
        // The low whistle (see its geometry): a jet band low enough for its registers to sit where a low D is
        // blown (low D ~120-670 Pa, the octave from ~670 Pa), one band-pass stage (the pitch follows the breath
        // half as much as with two), and a lower vortex loss, which lets the low notes swing further across the edge:
        // the reedy odd harmonics of a real low D (WhistleCheck analyse with Real-examples/Low-D-Golden.wav).
        v.jetStrouhal = 0.0095f;
        v.jetStages = 1;
        v.vortexLoss = 0.4f;
        v.jetGain = 0.75f;  // With the octave in tune, more let quick fingering changes flip into it on the way
    }
    if (index < presets().size() && presets()[index].name.startsWith ("Low D whistle (bright)"))
    {
        // Voiced after the recorded low D (Real-examples/Golden-Low-D-RODE.wav), whose low register is far richer than
        // the Low D whistle preset's: with the edge 0.25 mm into the pipe, less vortex loss and the jet's full gain
        // the first octave's H2 is -7 dB (recorded -7) and H4, H6 and H8 within 1 dB of the recording (H5 and H7 6-7 dB
        // short). Its H3 stays ~10 dB short (-17 against -8). Quick fingering changes are as steady as the Low D
        // whistle's, now that the check times the fundamental only (WhistleCheck transition, glide). Played on
        // 2026-11-10: good, if a little too bright.
        v.vortexLoss = 0.25f;
        v.jetGain = 1.05f;
        v.slowJetGrowth = 0.08f;
        v.breathNoise = 0.07f;  // The recording's -61 dB between the harmonics
    }
    if (index < presets().size() && presets()[index].chart == FingeringChart::baroqueRecorder)
    {
        v.jetStrouhal = 0.03f;
        v.jetStages = 1;
        // Fitted to a Moeck soprano (Real-examples/Moeck-Soprano-RODE.wav, with the edge offset in presets()): its
        // first octave is fuller than assumed before (H2/H3/H4 -15/-19/-27 dB; the model had -29/-23/-40), and less
        // vortex loss, more jet gain and some spreading match both octaves within ~2 dB. The low register now
        // reaches the octave with no silent gap; quick changes still settle within ~70 ms.
        v.vortexLoss = 0.5f;
        v.jetGain = 1.2f;
        v.jetSpreading = 1.0f;
        // A slow jet grows more (low notes speak from the softest breath, no gap before the octave), a fast one less
        // (a purer second octave).
        v.slowJetGrowth = 0.67f;
        v.breathNoise = 0.025f;  // A recorder's narrow, smooth windway: a clean tone with little hiss
        // Its scale is in tune from low C at 172 Pa (a third of the Golden's low D, 528) up to top C at 3.7 kPa: 21x,
        // where a whistle's spans 8x. Scaled by 0.7 alone, the low C sounded at 34 Pa of WARBL breath (~20 sensor
        // counts, on the steepest part of the response, so every count of jitter moved it ~4 %) and jumped to the
        // octave at 92 Pa, the breath of the whistle's low D: weak and flaky. With the curve, low C is at 80 Pa and
        // breaks at 172 (the Golden's low D: 89 and 190), and top C needs 1.8 kPa (was 2.8).
        v.breathScale = 1.0f;
        v.breathCurve = 1.45f;
    }
    // Every preset at about the same loudness: the measured Golden's, -14.6 dBFS over its test scale (WhistleCheck
    // scale: the mean in dB of the sounding 50 ms blocks, so soft low notes count as much as loud top ones). The
    // presets spread over 11 dB before (a tenor recorder -6.9, the low D -18.4). First match wins.
    if (index < presets().size())
    {
        struct Level { const char* nameStart; float gainDb; };
        static constexpr Level levels[] = {
            { "D whistle, cylindrical metal", -6.2f }, { "D whistle, conical metal", -2.5f }, { "D whistle, wide head", -4.9f },
            { "D whistle, wood", -2.8f }, { "D whistle, Golden style (bright)", 0.3f }, { "D whistle, Golden style", -2.2f },
            { "D whistle, Generation style", 3.7f }, { "D whistle, L", -3.5f } /* Löf */, { "D whistle, Golden (measured)", 0.0f },
            { "Low D whistle (bright)", 2.0f }, { "Low D whistle", 3.8f }, { "H", -4.1f } /* Härjedalspipa */,
            { "Soprano recorder in C (baroque fingering), real pressure", -1.0f }, { "Soprano recorder", 0.8f }, { "Alto recorder", -4.4f }, { "Tenor recorder", -7.7f },
        };
        for (const auto& l : levels)
            if (presets()[index].name.startsWith (l.nameStart))
            {
                v.outputGainDb = l.gainDb;
                break;
            }
    }
    if (index < presets().size() && presets()[index].name.startsWith ("Soprano recorder in C (baroque fingering), real pressure"))
    {
        // Played at the mouth pressures measured on a Moeck soprano (Real-examples/Moeck-pressure-2-swells-*, WARBL's
        // mouthpiece in the mouth beside it): the scale spans only ~2.5 x in pressure, low C ~60-190 Pa, the pinched
        // E6-A6 ~200-430, where the voicing above needs ~8 x (E6 at 690 Pa, so it spoke only at the edge of its range).
        // Searched against the swells, pressure by pressure (as the measured Golden's real-pressure voicing; the
        // Moeck's own ~20 c below A440 left out): the jet's band twice as high, less vortex loss and slow-jet growth.
        // The search put the band at Strouhal 0.06-0.075 (with jet thinning) so that the pinched notes speak at the
        // real pressures; played, that overblew far too easily (G5 to the octave at ~300 Pa, played clean at 359 on
        // the Moeck; low C to its twelfth at ~400, a normal second-octave breath). The pinched notes' trouble is in
        // the bore and holes (typical, not the Moeck's), which the jet can't make up for. At 0.04 the first octave
        // keeps the Moeck's ranges (low C ~70-240 Pa, G5 ~120 to beyond 500); the pinched notes need ~400 Pa
        // (the Moeck's from ~210).
        v.jetStrouhal = 0.04f;
        v.vortexLoss = 0.27f;
        v.jetGain = 1.15f;
        v.slowJetGrowth = 0.47f;
        // Breath noise grows with the jet's speed: at the real pressures 0.025 left it 8-10 dB under the Moeck's
        // (-71..-73 dB between the harmonics at 2-4 kHz).
        v.breathNoise = 0.07f;
        // WARBL's breath straight through, as for the measured Golden's real-pressure voicing.
        v.breathScale = 0.505f;
        v.breathCurve = 1.543f;
    }
    // A finger landing: the air it pushes in, and the body's resonances by material (first guesses until
    // measured: wood low and short, thin metal higher and ringing).
    if (index < presets().size())
    {
        const auto& name = presets()[index].name;
        const bool wooden = name.contains ("wood") || name.contains ("recorder") || name.contains ("traditional scale");
        v.fingerTap = 0.2f;  // Set by ear: a faint touch under the playing, well below the hole's own area x speed
        v.bodyKnock = 0.1f;
        v.bodyPitch = wooden ? 900.0f : 2300.0f;
        v.bodyRing = wooden ? 12.0f : 50.0f;
    }
    return v;
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

const std::vector<Geometry>& presets()
{
    auto make = [] (juce::String name, double length, double top, double foot, std::array<ToneHole, kNumHoles> holes,
                    double wall, double loss, double h, double w, double width)
    {
        Geometry g;
        g.name = std::move (name);
        g.boreLength = length;
        g.boreDiameterTop = top;
        g.boreDiameterFoot = foot;
        g.headDiameter = top;
        g.holes = holes;
        g.wallThickness = wall;
        g.wallLoss = loss;
        g.windwayHeight = h;
        g.windowLength = w;
        g.windowWidth = width;
        g.edgeOffset = 0.0001;
        return g;
    };

    // Bore lengths, hole positions, the cross-fingering hole sizes (whistles: L1 for C natural; recorder: R1 for the
    // F# fork, R3 for E'' and L3 for A'') and the whistles' head sections come from the tuner, tuned to the pitch the
    // model plays (PlayedTuning: each note at the middle of the breath it sounds at): `WhistleCheck code` prints
    // them. PlayedTuningTest checks they still play in tune. The upper octaves are judged near the
    // soft end of their breath (TuningGoal::breathPosition, 10 %), where a player blows them; judged at the middle,
    // the heads had come out 1.5-2 mm wider than the bore. Now they are within about 0.6 mm of it.
    // Holes: LT, L1, L2, L3, R1, R2, R3, R4 as { used, position, diameter }.
    static const std::vector<Geometry> list = [&]
    {
        std::vector<Geometry> instruments {
            [&]
            {
                // A cylindrical body; the tuner's head section (12.3 mm over the top 30 mm) puts its octaves in tune.
                auto g = make ("D whistle, cylindrical metal", 0.26710, 0.0120, 0.0120,
                               { { { false, 0.0, 0.0 }, { true, 0.12084, 0.00495 }, { true, 0.14501, 0.00700 }, { true, 0.16433, 0.00650 },
                                   { true, 0.19053, 0.00720 }, { true, 0.20180, 0.00680 }, { true, 0.22664, 0.00580 }, { false, 0.0, 0.0 } } },
                               0.0005, 1.0, 0.0010, 0.0050, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.01231;
                return g;
            }(),
            [&]
            {
                // A conical body (13 mm at the top, 10.5 at the foot), with the tuner's head section (13.9 mm over 30 mm).
                auto g = make ("D whistle, conical metal", 0.26441, 0.0130, 0.0105,
                               { { { false, 0.0, 0.0 }, { true, 0.11796, 0.00472 }, { true, 0.14293, 0.00700 }, { true, 0.16233, 0.00650 },
                                   { true, 0.18861, 0.00720 }, { true, 0.20044, 0.00680 }, { true, 0.22595, 0.00580 }, { false, 0.0, 0.0 } } },
                               0.0005, 1.0, 0.0010, 0.0050, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.01395;
                return g;
            }(),
            [&]
            {
                // Started as a narrower bore around the fipple (10.5 mm, widening to the 12 mm bore over 30 mm), which
                // widens the octaves: played at the middle of each note's breath its second octave was 30 c sharp. Tuned
                // to the played pitch, the head came out a little wider than the bore (12.3 mm): hence "wide head" (until 2026-11-22 "narrow head").
                auto g = make ("D whistle, wide head", 0.26706, 0.0120, 0.0120,
                               { { { false, 0.0, 0.0 }, { true, 0.12274, 0.00530 }, { true, 0.14467, 0.00700 }, { true, 0.16437, 0.00650 },
                                   { true, 0.19050, 0.00720 }, { true, 0.20177, 0.00680 }, { true, 0.22647, 0.00580 }, { false, 0.0, 0.0 } } },
                               0.0005, 1.0, 0.0010, 0.0050, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.01226;
                return g;
            }(),
            [&]
            {
                // The Golden style's fipple (the easiest to play) on a thick-walled wooden tube, with the tuner's head
                // section (12.4 mm over 30 mm).
                auto g = make ("D whistle, wood", 0.25784, 0.0125, 0.0125,
                               { { { false, 0.0, 0.0 }, { true, 0.11759, 0.00736 }, { true, 0.13834, 0.00850 }, { true, 0.15595, 0.00800 },
                                   { true, 0.17861, 0.00700 }, { true, 0.19335, 0.00920 }, { true, 0.21417, 0.00720 }, { false, 0.0, 0.0 } } },
                               0.0040, 1.3, 0.00087, 0.0042, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.01243;
                return g;
            }(),
            [&]
            {
                // In the style of a Golden high D (not measured): a thin aluminium tube, a low windway ceiling (Golden's
                // "medium" is ~0.87 mm) and a short window. The head was narrow (11.5 mm, as a real Golden's); tuned to the
                // played pitch it came out at 12.4 mm (the real one's octaves are voiced in ways the model doesn't have).
                auto g = make ("D whistle, Golden style", 0.26480, 0.0120, 0.0120,
                               { { { false, 0.0, 0.0 }, { true, 0.12001, 0.00551 }, { true, 0.14268, 0.00700 }, { true, 0.16154, 0.00650 },
                                   { true, 0.18789, 0.00720 }, { true, 0.19888, 0.00680 }, { true, 0.22330, 0.00580 }, { false, 0.0, 0.0 } } },
                               0.0010, 1.0, 0.00087, 0.0042, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.01236;
                g.edgeOffset = 0.00015;   // Into the pipe: the second harmonic of the recorded Golden's low notes
                return g;
            }(),
            [&]
            {
                // A Golden high D as measured (docs/Golden.md): a 12.5 mm bore in a 1.8 mm wall, a 0.97 x 7.7 mm windway
                // 24 mm long and a 5.8 mm window. The head is the tube pressed flat at the sides to 9 mm wide (top and
                // bottom still round): 101.8 mm2, as a round bore of 11.4 mm. It runs 14 mm from the flue exit, i.e.
                // 8.2 mm past the edge, then widens to the bore over 4 mm.
                // The model has one width for the jet and the window: the windway's 7.7 mm, which sets the jet's flow
                // (the window is 9 mm; its end correction is a little short for it). The bore (265 mm, edge to foot) and
                // the holes are the real whistle's, not the tuner's: measured from the block bottom, 7.0 mm above the
                // edge (labium to H6's centre measured 223 mm). The recording of it (Golden-High-D-RODE.wav) plays its bottom D 12 c flat.
                auto g = make ("D whistle, Golden (measured)", 0.2650, 0.0125, 0.0125,
                               { { { false, 0.0, 0.0 }, { true, 0.1153, 0.0058 }, { true, 0.1352, 0.0065 }, { true, 0.1530, 0.0059 },
                                   { true, 0.1800, 0.0055 }, { true, 0.1970, 0.0080 }, { true, 0.2230, 0.0076 }, { false, 0.0, 0.0 } } },
                               0.0018, 1.0, 0.00097, 0.0058, 0.0077);
                g.windwayLength = 0.024;
                g.headLength = 0.0122;
                g.headStraightLength = 0.0082;
                g.headDiameter = 0.0114;
                // Nearly centred, for the voicing below (searched with this edge).
                g.edgeOffset = 0.00005;
                return g;
            }(),
            [&]
            {
                // Low D whistle: a 22 mm cylindrical bore (a Golden low D measures ~21.8), a 1.0 mm windway (Golden
                // medium blowers 0.9-1.0 mm) and a 13 mm wide window, as forum measurements of low whistles give. The
                // window is short (5.2 mm, Overton style): a longer one let the jet grow so much that quick fingering
                // changes flipped into the octave on the way (WhistleCheck transition).
                auto g = make ("Low D whistle", 0.53159, 0.0220, 0.0220,
                               { { { false, 0.0, 0.0 }, { true, 0.23449, 0.00774 }, { true, 0.28175, 0.01100 }, { true, 0.32084, 0.01050 },
                                   { true, 0.37353, 0.01150 }, { true, 0.39635, 0.01100 }, { true, 0.44690, 0.00950 }, { false, 0.0, 0.0 } } },
                               0.0010, 1.0, 0.0010, 0.0052, 0.0130);
                g.windwayLength = 0.028;  // A bigger fipple
                // A slightly wider head (23.8 mm over the top 60 mm) flattens the second octave into tune: with the
                // plain bore it played 10-18 cents sharp. The diameter is the played-pitch tuner's.
                g.headLength = 0.060;
                g.headDiameter = 0.02382;
                return g;
            }(),
            [&]
            {
                // Härjedalspipa (Härjedalen, Sweden): a wooden duct flute with a cylindrical bore and six finger holes,
                // in the traditional scale on A# with its neutral thirds and sevenths. It is fingered as a whistle (holes
                // opened from the bottom; the whistle chart a major third down) and tuned to its scale as Swedish
                // Wikipedia gives it (Temperament::harjedalSwedish); the high tonic overblown with every hole covered,
                // no cross-fingered seventh. Typical dimensions, scaled from the wooden whistle, not a measured
                // instrument; holes placed by the tuner.
                // R1 small and R2 large (4 and 10 mm) keep D+35 and D#, only 65 cents apart, about 20 mm apart.
                auto g = make (juce::String::fromUTF8 ("Härjedalspipa (A#, traditional scale)"), 0.33299, 0.0130, 0.0130,
                               { { { false, 0.0, 0.0 }, { true, 0.15300, 0.00700 }, { true, 0.17914, 0.00700 }, { true, 0.20008, 0.00700 },
                                   { true, 0.22515, 0.00400 }, { true, 0.24636, 0.01000 }, { true, 0.27164, 0.00500 }, { false, 0.0, 0.0 } } },
                               0.0040, 1.3, 0.0010, 0.0055, 0.0090);
                g.chartTranspose = -4;  // A#4: the whistle chart's D5 a major third down
                g.headLength = 0.0300;  // The tuner's head section, slightly narrower than the bore
                g.headDiameter = 0.01125;
                g.tuning = harjedalGoal();
                g.windwayLength = 0.022;
                return g;
            }(),
            [&]
            {
                // Soprano recorder in C with baroque fingering: reverse-conical bore (wide at the top), thumb hole on
                // the back, all seven front holes, thick walls and a wide windway. Typical dimensions, not a measured
                // instrument. The sizes of the thumb hole, L3, R1 and R3 come from the tuner (high Bb, high A, the F#
                // fork and high B); L2's (5 mm) puts the Bb fork in tune; L1's, R2's and R4's are first guesses.
                auto g = make ("Soprano recorder in C (baroque fingering)", 0.28695, 0.0130, 0.0095,
                               { { { true, 0.10073, 0.00503 }, { true, 0.11979, 0.00600 }, { true, 0.12579, 0.00503 }, { true, 0.14569, 0.00493 },
                                   { true, 0.16438, 0.00397 }, { true, 0.19793, 0.00650 }, { true, 0.21154, 0.00535 }, { true, 0.23749, 0.00450 } } },
                               0.0035, 1.1, 0.0010, 0.0037, 0.0088);  // Window 3.7 x 8.8 mm, as a measured soprano (NMMSD)
                g.chart = FingeringChart::baroqueRecorder;
                // A short window for the jet's thickness (W/h 3.7, a whistle's ~5) grows the jet less on its way to the
                // edge: it switches less fully, a purer tone, and the top notes speak at lower breath.
                // The edge 0.2 mm into the pipe: the recorded Moeck soprano's strong second harmonic (first octave
                // H2 -15 dB). (At 0.05 mm, nearly on the jet's centre line, even harmonics were 14 dB weaker.)
                g.edgeOffset = 0.0002;
                g.windwayLength = 0.030;  // The block is longer than a whistle's fipple plug
                g.bellClosesFoot = true;  // Top C# closes the foot (on the knee); without a bell sensor it stays open.
                return g;
            }(),
        };

        // The Generation and Löf styles: the Golden style's tube and fipple (the most playable), voiced and edged
        // after RODE recordings of those whistles (presetVoicing). Their tubes and windways are not measured.
        for (size_t i = 0; i < instruments.size(); ++i)
            if (instruments[i].name == "D whistle, Golden style")
            {
                // Each voicing pulls the notes a little differently: each has its own tuning (played-pitch tuner).
                auto retuned = [] (Geometry g, double length, double head, std::array<ToneHole, kNumHoles> holes)
                {
                    g.boreLength = length;
                    g.headDiameter = head;
                    g.holes = holes;
                    return g;
                };
                auto generation = retuned (instruments[i], 0.26544, 0.01246,
                                           { { { false, 0.0, 0.0 }, { true, 0.12006, 0.00522 }, { true, 0.14281, 0.00700 }, { true, 0.16175, 0.00650 },
                                             { true, 0.18808, 0.00720 }, { true, 0.19906, 0.00680 }, { true, 0.22346, 0.00580 }, { false, 0.0, 0.0 } } });
                auto lof = retuned (instruments[i], 0.26556, 0.01236,
                                    { { { false, 0.0, 0.0 }, { true, 0.12119, 0.00559 }, { true, 0.14285, 0.00700 }, { true, 0.16173, 0.00650 },
                                             { true, 0.18801, 0.00720 }, { true, 0.19900, 0.00680 }, { true, 0.22376, 0.00580 }, { false, 0.0, 0.0 } } });
                generation.name = "D whistle, Generation style";
                generation.edgeOffset = 0.00015;
                lof.name = juce::String::fromUTF8 ("D whistle, Löf style");
                lof.edgeOffset = 0.0002;  // Its first octave's strong second harmonic (H2 -11 dB)
                // The same whistle voiced after the RODE recording of the Golden (presetVoicing): the edge nearer the
                // jet's centre line, since its jet is bent off it at a soft breath (Voicing::jetBend).
                auto bright = retuned (instruments[i], 0.26568, 0.01258,
                                       { { { false, 0.0, 0.0 }, { true, 0.12089, 0.00534 }, { true, 0.14319, 0.00700 }, { true, 0.16218, 0.00650 },
                                             { true, 0.18846, 0.00720 }, { true, 0.19982, 0.00680 }, { true, 0.22437, 0.00580 }, { false, 0.0, 0.0 } } });
                bright.name = "D whistle, Golden style (bright)";
                bright.edgeOffset = 0.00005;
                instruments.insert (instruments.begin() + (long) i + 1, { bright, generation, lof });
                break;
            }
        // The measured Golden again, voiced for the mouth pressures measured on it (presetVoicing), with the edge
        // on the jet's centre line (searched with it).
        for (size_t i = 0; i < instruments.size(); ++i)
            if (instruments[i].name == "D whistle, Golden (measured)")
            {
                auto real = instruments[i];
                real.name = "D whistle, Golden (measured), real pressure";
                real.edgeOffset = 0.0;
                // And once more with a jet that also matches where the registers change (presetVoicing).
                auto breaks = real;
                breaks.name = "D whistle, Golden (measured), real pressure and breaks";
                instruments.insert (instruments.begin() + (long) i + 1, { real, breaks });
                break;
            }
        // The low D voiced after its RODE recording: the edge further into the pipe for the strong second harmonic
        // of its low register (recorded -7 dB).
        for (size_t i = 0; i < instruments.size(); ++i)
            if (instruments[i].name == "Low D whistle")
            {
                auto bright = instruments[i];
                bright.name = "Low D whistle (bright)";
                bright.edgeOffset = 0.00025;
                bright.boreLength = 0.53169;  // Tuned to its own played pitch
                bright.headDiameter = 0.02389;
                bright.holes = { { { false, 0.0, 0.0 }, { true, 0.23449, 0.00769 }, { true, 0.28172, 0.01100 }, { true, 0.32094, 0.01050 },
                    { true, 0.37343, 0.01150 }, { true, 0.39634, 0.01100 }, { true, 0.44694, 0.00950 }, { false, 0.0, 0.0 } } };
                instruments.insert (instruments.begin() + (long) i + 1, bright);
                break;
            }

        // Larger recorders start from geometrically scaled soprano dimensions, not measurements of a maker's
        // instrument. Scale the mouth too: the same W/h and f*h/U keep the recorder's jet behaviour as pitch falls.
        const auto soprano = instruments.back();
        for (const int transpose : { -7, -12 })  // Alto F4 and tenor C4, relative to soprano C5
        {
            auto g = soprano;
            const double scale = std::pow (2.0, -transpose / 12.0);
            g.name = transpose == -7 ? "Alto recorder in F (baroque fingering)" : "Tenor recorder in C (baroque fingering)";
            g.chartTranspose = transpose;
            g.boreLength *= scale;
            g.boreDiameterTop *= scale;
            g.boreDiameterFoot *= scale;
            g.headDiameter *= scale;
            g.wallThickness *= scale;
            g.windwayHeight *= scale;
            g.windwayLength *= scale;
            g.windowLength *= scale;
            g.windowWidth *= scale;
            g.edgeOffset *= scale;
            for (auto& hole : g.holes)
            {
                hole.position *= scale;
                hole.diameter *= scale;
            }
            // Retuned offline with WhistleCheck code: losses and the window's fixed end correction do not
            // scale exactly with the instrument, so simply multiplying the soprano leaves these notes sharp.
            if (transpose == -7)
            {
                g.boreLength = 0.43488;
                g.holes = { { { true, 0.15154, 0.00764 }, { true, 0.18279, 0.00899 }, { true, 0.19196, 0.00754 }, { true, 0.22103, 0.00731 },
                              { true, 0.25021, 0.00596 }, { true, 0.29946, 0.00974 }, { true, 0.32118, 0.00812 }, { true, 0.36089, 0.00674 } } };
            }
            else
            {
                g.boreLength = 0.58267;
                g.holes = { { { true, 0.20329, 0.01020 }, { true, 0.24547, 0.01200 }, { true, 0.25747, 0.01006 }, { true, 0.29769, 0.00996 },
                              { true, 0.33442, 0.00792 }, { true, 0.40182, 0.01300 }, { true, 0.43065, 0.01115 }, { true, 0.48444, 0.00900 } } };
            }
            instruments.push_back (g);
        }
        // The soprano again, voiced for the mouth pressures measured on a Moeck soprano (presetVoicing), the edge
        // further into the pipe (searched with it).
        {
            auto real = soprano;
            real.name = "Soprano recorder in C (baroque fingering), real pressure";
            real.edgeOffset = 0.0003;
            real.boreLength = 0.28780;  // Tuned to its own played pitch
            real.holes = { { { true, 0.10091, 0.00503 }, { true, 0.11991, 0.00600 }, { true, 0.12591, 0.00503 }, { true, 0.14563, 0.00496 },
                             { true, 0.16558, 0.00401 }, { true, 0.19786, 0.00650 }, { true, 0.21206, 0.00548 }, { true, 0.23819, 0.00450 } } };
            instruments.push_back (real);
        }

        // Whistles are tuned for the keys they are mostly played in, D, G, Em and Bm (on a D whistle), their chords
        // halfway from equal temperament to as pure as can be, with L1's size for high D (TuningGoal); recorders to
        // equal temperament, the Härjedalspipa to its own scale.
        for (auto& g : instruments)
            if (g.chart == FingeringChart::whistle && g.tuning.temperament == Temperament::equal)  // Not the Härjedalspipa
            {
                g.tuning.temperament = Temperament::bestForKeys;
                g.tuning.towardEqual = 0.5;
                g.tuning.topHoleForHighD = true;  // L1's size for high D; C natural is half-holed
                g.tuning.keys = {};
                g.tuning.keys[0] = g.tuning.keys[5] = 1.0;                                                  // I, IV
                g.tuning.keys[TuningGoal::kMinor + 2] = g.tuning.keys[TuningGoal::kMinor + 9] = 1.0;       // ii, vi (minor)
            }

        // Edge offsets found with the voicings in presetVoicing() (m, into the pipe).
        for (auto& g : instruments)
        {
            if (g.name.contains ("cylindrical metal") || g.name.contains ("traditional scale"))
                g.edgeOffset = 0.00015;
            else if (g.name.contains ("conical metal") || g.name.contains ("wide head"))
                g.edgeOffset = 0.00025;
            else if (g.name.contains ("whistle, wood"))
                g.edgeOffset = 0.00015;  // As the Golden style, whose fipple it shares
        }

        // Each whistle comes twice: on WARBL's L1-R3 sensors and on L2-R4 (the same instrument, played with
        // the lower six fingers).
        std::vector<Geometry> withSensors;
        for (const auto& g : instruments)
        {
            if (g.chart == FingeringChart::baroqueRecorder)  // Uses all eight holes
            {
                withSensors.push_back (g);
                continue;
            }
            auto upper = g, lower = g;
            upper.name << ", holes L1-R3";
            lower.name << ", holes L2-R4";
            moveFrontHoles (lower, 2);
            withSensors.push_back (upper);
            withSensors.push_back (lower);
        }
        return withSensors;
    }();
    return list;
}

} // namespace whistle
