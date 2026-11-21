// The physical description of a fipple flute (whistle, recorder). Both the real-time model (WhistleModel) and
// the tuning calculator (Acoustics) are built from this, so changing the instrument means changing these numbers.
// All lengths are in metres.

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace whistle
{

// Tone holes, one per WARBL finger sensor, in this order: left thumb (on the back), L1-L3, R1-R4.
// An instrument uses some of them (e.g. a whistle L1-R3); the others are plugged (disabled).
constexpr int kNumHoles = 8;
constexpr std::array<const char*, kNumHoles> kHoleNames { "LT", "L1", "L2", "L3", "R1", "R2", "R3", "R4" };
constexpr double kSpeedOfSound = 345.0;  // Air inside a played whistle is a little warmer than the room.
constexpr double kAirDensity = 1.2;
constexpr double kAirViscosity = 1.85e-5;

// WARBL sensor index for each hole (see warbl::holeName()), and for the bell sensor at the foot.
constexpr std::array<int, kNumHoles> kSensorForHole { 8, 7, 6, 5, 4, 3, 2, 1 };
constexpr int kBellSensor = 0;

struct ToneHole
{
    bool enabled = false;   // A disabled hole is plugged: as if it weren't drilled.
    double position = 0.0;  // Centre of the hole, measured from the window edge (labium) towards the foot.
    double diameter = 0.0;
};

// Which fingering chart the tuning table, tuner and checks use (the sound never uses it).
enum class FingeringChart
{
    whistle,          // Six holes, L1-R3
    baroqueRecorder,   // Thumb and seven holes, baroque fingering
    harjedalspipa      // Six holes like a whistle, the traditional Härjedalspipa scale (not tempered)
};

struct Geometry
{
    juce::String name;

    // Bore: from the window to the open foot. The diameter changes linearly from top to foot.
    double boreLength = 0.27;
    double boreDiameterTop = 0.012;
    double boreDiameterFoot = 0.012;

    // Optional head section (e.g. a narrower bore around the fipple, as in Golden whistles): from the window
    // down to headLength the diameter goes linearly from headDiameter to the bore's own diameter there.
    // headLength = 0 means no head section. headStraightLength: the head keeps headDiameter this far from the
    // window and only then blends into the bore (a measured Golden: 14 mm straight, then a 4 mm change);
    // 0 = it blends all the way from the window.
    double headLength = 0.0;
    double headDiameter = 0.012;
    double headStraightLength = 0.0;

    std::array<ToneHole, kNumHoles> holes {};
    double wallThickness = 0.0005;  // Sets the height of the tone hole chimneys.
    bool bellClosesFoot = false;    // The bell sensor covers the open foot (recorder technique).
    FingeringChart chart = FingeringChart::whistle;
    int chartTranspose = 0;  // Semitones from the chart's base pitch (recorder: C5); does not shift the audio.
    double referenceA = 440.0;  // Hz: the pitch standard the instrument is tuned to (A4)

    // Fipple (mouthpiece). The windway ends in a flat jet that crosses the window and hits the edge (labium).
    double windwayHeight = 0.001;    // Jet thickness at the flue exit (h).
    double windwayLength = 0.018;    // Mouth end to flue exit: the air the breath accelerates, and its friction.
    double windowLength = 0.005;     // Flue exit to the edge (W): sets how long the jet takes to cross.
    double windowWidth = 0.008;      // Width of the jet and window.
    double edgeOffset = 0.0001;      // Edge position relative to the jet centre line (y0); positive = into the pipe.

    double wallLoss = 1.0;  // Viscous and thermal losses at the bore wall: 1 = smooth metal, more for rough walls.

    double radiusAt (double x) const
    {
        const auto body = [this] (double at)
        {
            const double t = juce::jlimit (0.0, 1.0, at / boreLength);
            return 0.5 * (boreDiameterTop + t * (boreDiameterFoot - boreDiameterTop));
        };
        if (headLength > 0.0 && x < headLength)
        {
            const double straight = juce::jlimit (0.0, headLength, headStraightLength);
            if (x <= straight)
                return 0.5 * headDiameter;
            const double t = juce::jlimit (0.0, 1.0, (x - straight) / (headLength - straight));
            return 0.5 * headDiameter + t * (body (headLength) - 0.5 * headDiameter);
        }
        return body (x);
    }

    double windowArea() const { return windowLength * windowWidth; }

    // Effective acoustic length of the window opening (its "plug" of air plus end corrections on both sides).
    double windowEffectiveLength() const
    {
        const double equivalentRadius = std::sqrt (windowArea() / juce::MathConstants<double>::pi);
        return 0.001 + 1.7 * equivalentRadius;
    }

    // Effective length of an open tone hole: chimney plus the inner correction, the matching volume and the
    // radiation of a hole drilled through a thick wall (Lefebvre & Scavone 2012, Eqs. 31, 8 and 11; d = b / a).
    // Keefe's (1990) t + b (1.4 - 0.58 d^2), used before, is ~0.2 mm longer for holes as large as a whistle's.
    // Against finger taps on a measured Golden these and the series lengths below brought the open-hole notes'
    // resonances from 7-16 c to within ~10 c of the real tube's (rms 8.6 -> 6.0 c).
    double holeEffectiveLength (int hole) const
    {
        const double b = 0.5 * holes[(size_t) hole].diameter;
        const double a = radiusAt (holes[(size_t) hole].position);
        const double t = std::max (wallThickness, 0.0003), d = b / a;
        const double inner = b * (0.822 - 0.095 * d - 1.566 * d * d + 2.138 * d * d * d - 1.640 * d * d * d * d + 0.502 * d * d * d * d * d);
        const double matching = b * d * (1.0 + 0.207 * d * d * d) / 8.0;
        const double radiation = b * (0.822 - 0.47 * std::pow (b / (a + t), 0.8));
        return t + inner + matching + radiation;
    }

    // Series length of a tone hole, between closed (0) and open (1): negative, the bore acts a little shorter
    // across a hole (~0.4 mm for a whistle's). Lefebvre & Scavone (2012), Eqs. 33 and 34.
    double holeSeriesLength (int hole, double openness) const
    {
        const double b = 0.5 * holes[(size_t) hole].diameter;
        const double a = radiusAt (holes[(size_t) hole].position);
        const double t = std::max (wallThickness, 0.0003), d = b / a;
        const double open = b * d * d * (-0.35 + 0.06 * std::tanh (2.7 * t / b));
        const double closed = b * d * d * (-0.12 - 0.17 * std::tanh (2.4 * t / b));
        return openness * open + (1.0 - openness) * closed;
    }
};

struct Material
{
    juce::String name;
    double wallThickness;
    double wallLoss;
};

// Material mostly matters through wall thickness (tone hole chimney height) and surface roughness.
const std::vector<Material>& materials();

// How the jet behaves (see WhistleModel) and the output level: part of an instrument's character.
struct Voicing
{
    float jetStrouhal = 0.02f;   // Frequency the jet amplifies most, as f h / U
    float jetConvection = 0.6f;  // Speed of disturbances along the jet, relative to the jet speed
    float breathNoise = 0.05f;   // Turbulence at the jet and edge (grows with jet speed)
    float outputGainDb = 0.0f;
    float directNoise = 0.0f;    // How much of the edge noise escapes straight from the window
    // The windway's physics, 1 = as computed from its size, 0 = off (jet speed follows the pressure at once,
    // no friction): kept adjustable because WARBL's own mouthpiece already smooths the breath a little.
    float windwayMass = 1.0f;      // The air in the windway has to be accelerated (~1 ms)
    float windwayFriction = 1.0f;  // Boundary layers on the windway walls carry less flow than the jet core
    // Loss from vortices shed where the sound flows round the edge, as a multiple of Verge's 1/2 rho (v / 0.6)^2.
    // It is what limits the low notes' loudness: lower = fuller, louder low notes (with more odd harmonics).
    float vortexLoss = 1.0f;
    // Extra breath noise while the jet is starting and not yet steered by the sound (the attack "chiff"), as a
    // multiple of the breath noise: 0 = none.
    float attackChiff = 0.0f;
    // The viscous resistance of the thin gap under a finger just above a hole, as a multiple of the computed one:
    // 0 = off (a barely lifted finger already lets the hole act), 1 = physical (it leaks almost nothing at first).
    float fingerGapDamping = 0.0f;
    // The jet's amplification of the sound's push on its way across the window, as a multiple of the computed one
    // (Rayleigh growth, e^(0.38 W / h)): higher = the jet swings further across the edge, a richer, squarer tone.
    float jetGain = 1.0f;
    // How much more a slow jet amplifies (more disturbance wavelengths fit across the window): the growth's
    // exponent scales as (U_ref / U)^this, U_ref being the jet speed whose band centre is the lowest note. 0 = the
    // same growth at any speed; 2/3 = a thin jet's long-wave theory. Soft notes come out richer, hard-blown ones
    // (the second octave) purer.
    float slowJetGrowth = 0.0f;
    // Band-pass stages in the jet's response (its preference for frequencies near the Strouhal number): each adds
    // selectivity but also phase shift, which narrows each register's pressure range.
    int jetStages = 2;
    // The jet widens on its way across the window, much more once it is turbulent (Reynolds number U h / nu above
    // ~1000, fully above ~3000): a turbulent plane jet's half-width grows by ~0.1 x the distance. As a multiple of
    // that, 0 = a jet of constant width (0.4 h). A wider jet at the edge switches less abruptly and carries more
    // flow: fast-blown notes (the second octave) come out louder and purer, as on real whistles.
    float jetSpreading = 0.0f;
    // Which jet: 0 = a band-pass around the Strouhal number above, a delay at jetConvection and a growth that
    // falls for fast jets (slowJetGrowth); 1 = the jet's linear stability theory for the windway's velocity
    // profile (JetStability: receptivity, growth and phase speed per omega h / U, dispersion included), as an
    // impulse response in units of h / U. With 1, jetGain scales the theory's deflection and jetStrouhal,
    // jetConvection, jetStages and slowJetGrowth are not used.
    int jetModel = 0;
    // The jet thins as it speeds up (the windway's boundary layers, ~ U^-1/2), so the band it amplifies moves up
    // faster than its speed: the band's centre goes as U (U / U_ref)^jetThinning, U_ref the speed whose band is the
    // lowest note. 0 = a jet of fixed thickness (centre in proportion to U). Real recorders play their second
    // octave at ~2 x the first octave's pressure, not the ~4 x a fixed thickness gives.
    float jetThinning = 0.0f;
    // jetModel 1: the omega h / U above which the jet's waves stop growing (they saturate within the window
    // rather than grow without limit as linear theory has them); the response fades out by 1.6 x this.
    float jetCutoff = 0.3f;
    // A slow jet drives the pipe less than jet theory says: real whistles start each note at a Reynolds number
    // U h / nu of ~1000-1700 (Real-examples/Golden-pressure-1-swells-*: D5 ~1000, B5 ~1400), where the standard
    // model, and the theory's jet, sound the low notes at half that breath (Auvray et al. 2012 find the same: its
    // onsets are off, its register changes right). The jet's amplification is scaled by 1 / (1 + (Re_low / Re)^4),
    // Re_low being this. It lifts the low notes' start most (they speak at the lowest jet speeds), and lets a high
    // note speak with more of a jet wavelength across the window than a low one, as recorded. 0 = off.
    float jetLowReynolds = 0.0f;
    // The jet's band-pass flattened above its centre: plus this x a high-pass at the centre (1 = as much gain above
    // the centre as at it), rolled off from jetFlatTop x the centre up (2 poles; 0 = not rolled off). A real jet
    // amplifies the pipe's higher resonances at least as much as the one near its band (jet theory: growth rises
    // with frequency up to its cutoff), which is what lets an upper register hold on down to ~0.6 x the pressure
    // where it took over (Real-examples/Golden-pressure-3-breaks-*: D6 back to D5 at ~335 Pa after breaking up at
    // ~560). The plain band-pass dropped back at ~1.6 x that. The roll-off keeps the second octave's own harmonics
    // (2 x the centre and up) from brightening it. 0 = the plain band-pass.
    float jetFlatAbove = 0.0f;
    float jetFlatTop = 0.0f;
    // A steady sideways push on the jet as it crosses the window, as the pressure difference across it that would
    // give it (Pa; positive bends the jet out of the pipe, which is the same as moving the edge further in). A thin
    // jet under a pressure difference dp is displaced by dp W^2 / (2 rho h U^2) at the edge, so the bend is large
    // for a slow jet and nearly gone for a fast one: the low register gets the even harmonics of an off-centre
    // edge and the second register comes out purer, as on recorded whistles (their second harmonic drops 8-21 dB
    // between the octaves; a fixed edge offset gives ~7). Below the jet speed the lowest note is played at (the
    // model's reference speed) the bend stays what it is there, and it levels off at the jet's half-width, so
    // notes still start at a soft breath. 0 = none.
    // A fitted law: what bends a real whistle's jet, and by how much, has not been measured.
    float jetBend = 0.0f;
    // How hard this instrument is blown, as a share of WARBL's breath response (Sensors tab): a recorder is blown
    // about half as hard as a whistle, but WARBL's mouthpiece feels the same for both. Only WARBL's breath is
    // scaled (not Play scale or playing from the screen, which give the model's own pressures).
    float breathScale = 1.0f;
    // And how it is spread: the instrument's pressure is breathScale x full x (WARBL's mapped breath / full)^breathCurve,
    // full being the breath response's full output (Sensors tab). 1 = as WARBL's response. Above 1, gentle breath
    // gives less pressure while full breath still gives breathScale x full: for an instrument whose low notes need
    // far less pressure than a whistle's but whose top notes need as much (a recorder's scale spans ~21x in
    // pressure, a whistle's ~8x).
    float breathCurve = 1.0f;
    // A finger landing on a hole, with or without breath. fingerTap: the air it pushes into the bore, as a multiple
    // of the physical amount (the hole's area x the finger's speed); the tube rings at its current resonances.
    // bodyKnock: how strongly the impact shakes the instrument's body, whose resonances (pitch in Hz, ring as a Q
    // factor) are what tells wood (low, short) from thin metal (high, ringing). 0 = off.
    float fingerTap = 0.0f;
    float fingerSoftness = 4.0f;  // ms: how long a finger's push builds up, at an everyday tap (0.3 m/s); it then stops in a third of that
    float bodyKnock = 0.0f;
    float bodyPitch = 1000.0f;
    float bodyRing = 15.0f;
};

// Starting points, tuned (see WhistleGeometry.cpp).
const std::vector<Geometry>& presets();

// The lowest note a preset is tuned to (Hz, at its reference A): recorders C5 plus chartTranspose,
// whistles D5 (low whistles D4),
// the Härjedalspipa A#4.
double presetLowestNote (const Geometry&);

// The voicing that goes with each preset (same order as presets()).
Voicing presetVoicing (size_t index);

// Puts the instrument's front holes (in order, keeping each hole's position and size) on WARBL's sensors
// starting at `firstHole` (1 = L1, 2 = L2, ...), e.g. a six-hole whistle on L2-R4 instead of L1-R3. The thumb
// hole is left as it is. Returns false (and changes nothing) if they don't fit.
bool moveFrontHoles (Geometry&, int firstHole);

// The first front hole in use (1 = L1 ...), or 0 if none.
int firstFrontHole (const Geometry&);

} // namespace whistle
