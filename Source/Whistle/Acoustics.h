// Frequency-domain acoustics of a fipple flute geometry (transfer-matrix method): the impedance the jet sees at
// the window, and from it the resonances that a given set of hole openings gives. Used for the tuning table
// and for placing holes; the sound itself comes from WhistleModel, a time-domain simulation of the same
// geometry. Nothing here decides which note is played.

#pragma once

#include "WhistleGeometry.h"

#include <complex>
#include <string>

namespace whistle
{

// How open each hole and the foot are: 0 = closed, 1 = fully open. Disabled (plugged) holes are ignored.
struct Openness
{
    std::array<double, kNumHoles> holes {};
    double foot = 1.0;

    static Openness allClosed() { return {}; }

    // From a pattern in hole order (LT L1 L2 L3 R1 R2 R3 R4, then optionally the bell): x = covered,
    // o = open, h = half covered, p = pinched (a small crack open, 25 %), - = not used (open).
    static Openness fromPattern (const char* pattern);
};

// Impedance seen at the window: the bore with its holes plus the window opening itself (Pa s / m^3).
std::complex<double> windowImpedance (const Geometry&, const Openness&, double hz);

// Resonance frequencies (impedance minima at the window) between fMin and fMax, lowest first.
// For a flute-like instrument these are where it can sound: the first is the low register,
// the second the overblown register.
std::vector<double> resonances (const Geometry&, const Openness&, double fMin = 150.0, double fMax = 5000.0);

// The holes a chart's fingers refer to, in order. The recorder chart uses all eight holes by name (LT first);
// the whistle chart's six fingers are the instrument's enabled front holes from the top, so the same whistle
// fingerings work whether the whistle is on WARBL's L1-R3 or L2-R4 sensors.
std::vector<int> chartHoles (const Geometry&);

// A fingering chart entry: the note a fingering should give, and which part of the instrument the tuner
// starts from to get it (the final values come from a fit over all the notes, see fitTuning). Patterns and tuned holes are in chart fingers (see chartHoles()).
struct ChartFingering
{
    static constexpr int kTunesBore = -2;
    static constexpr int kTunesNothing = -1;

    const char* name;     // In the chart's own key (D for whistles, C for recorders).
    const char* pattern;  // One character per chart finger: x covered, o open, h half, p pinched (25 %).
    double semitones;     // Above the lowest note (with a fraction for untempered scales: 4.35 = 435 cents).
    int reg;              // 1 = low register, 2 = second octave, 3 = third octave.
    int tunes = kTunesNothing;          // Finger whose hole's position is set by this note, or the bore length.
    int tunesDiameter = kTunesNothing;  // Optional: finger whose hole's diameter is set by this note (cross-fingerings).
    bool counts = true;  // False: shown, but the tuner and the error figures leave it out (a fingering players avoid)

    Openness openness (const Geometry&) const;

    // The hole a finger maps to on this instrument, or -1.
    static int holeFor (const Geometry&, int finger);

    // The pattern in hole order (LT L1 L2 L3 R1 R2 R3 R4), '-' for holes the chart doesn't use.
    std::string holePattern (const Geometry&) const;
};

const std::vector<ChartFingering>& chartFor (FingeringChart);

// A fingering's name in the tuning goal (TuningGoal::fingerings): note name and pattern, e.g. "high D xxxxxx".
std::string fingeringId (const ChartFingering&);

// Whether the tuner and the error figures count this fingering: the chart's default unless the goal says otherwise.
bool fingeringCounts (const Geometry&, const ChartFingering&);

// The temperament's offsets from equal temperament (cents) for each degree above the lowest note, pulled toward
// equal by TuningGoal::towardEqual. Degree 0 is always 0 (the slide sets the overall pitch).
std::array<double, 12> temperamentCents (const TuningGoal&);

// The pitch the tuner aims a fingering at: its chart note, moved by the temperament. In cents from the
// equal-tempered note, and in Hz.
double targetCents (const Geometry&, const ChartFingering&);
double targetHz (const Geometry&, const ChartFingering&, double lowestHz);

// Cents from the target to the nearest resonance the geometry gives for this fingering.
double predictedCents (const Geometry&, const ChartFingering&, double lowestHz);

// How much a note counts when the tuner shares the error out (its squared error is multiplied by this): from 0.3
// for a note in none of the keys that count (TuningGoal::keys) to 1 for one in all of them, as much in the third
// octave; 0 for a fingering that doesn't count; a whistle's C natural a tenth with TuningGoal::topHoleForHighD. A note with two fingerings that count counts once in all. (With the
// default goal: the scale 1, accidentals 0.3.)
double tuningWeight (const Geometry&, const ChartFingering&);

// Further than this from its target a note isn't this instrument's (a plugged hole): the tuner leaves it out.
constexpr double kCentsNotReachable = 80.0;

struct TuningError
{
    double rms = 0.0;             // Cents, weighted as in tuningWeight, over every note in the chart
    double worstScaleNote = 0.0;  // Cents
};
TuningError tuningError (const Geometry&, double lowestHz);

// Moves the tuning slide to `slide` (m from the made position, positive = pulled out): the tube below the joint,
// with its holes, moves down by the change, as on a real whistle or recorder.
void setSlide (Geometry&, double slide);

// The slide setting (m, as Geometry::slide) that brings the chart best in tune on `lowestHz`, the notes weighted as
// for the tuner (tuningWeight), as a player tunes to the others' A. Within -15 to +30 mm. With `pullCents` (one per
// chartFor entry: how far each note plays from its resonance; NaN for a note that doesn't sound) it tunes the
// played notes instead of the resonances.
double bestSlide (const Geometry&, double lowestHz, const std::vector<double>* pullCents = nullptr);
constexpr double kSlideMin = -0.015, kSlideMax = 0.030;

// The weighted least-squares fit on its own, from the instrument as it stands: moves the bore length and hole
// positions, and with `fitSizes` the hole sizes the chart marks (tunesDiameter), to minimise the squared cents
// error over every chart note, both octaves alike. With `aimCents` (one per chartFor entry, cents from the note) the
// resonances are aimed there instead of at the notes: see PlayedTuning, which aims them so the notes play in tune.
// With `fitHead`, an instrument with a head section also gets its diameter fitted: the one dimension here that moves
// every fingering's second register against its first.
void fitTuning (Geometry&, double lowestHz, bool fitSizes, const std::vector<double>* aimCents = nullptr, bool fitHead = false);

// Tunes the instrument to equal temperament on `lowestHz` (or the chart's own scale): places the bore and holes
// note by note, then fitTuning shares the remaining error over all the notes rather than leaving it in the upper
// octave. Keeps hole diameters, except that with `tuneCrossFingerings` the fit may change the sizes the chart
// marks; with `resizeHoles`, holes that can't reach their note where they are allowed to sit also get new sizes
// (smaller to sit higher, larger to sit lower). Returns false if the note-by-note placement couldn't converge.
bool tuneToScale (Geometry&, double lowestHz, bool tuneCrossFingerings = false, bool resizeHoles = false);

} // namespace whistle
