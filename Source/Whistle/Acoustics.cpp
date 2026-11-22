#include "Acoustics.h"

#include <algorithm>
#include <cmath>

namespace whistle
{

namespace
{
using Complex = std::complex<double>;
constexpr double kPi = juce::MathConstants<double>::pi;
const Complex j (0.0, 1.0);

// Wavenumber in a tube of radius a, with viscous and thermal wall losses (Keefe's large-tube approximation).
Complex waveNumber (double omega, double a, double wallLoss)
{
    const double rv = a * std::sqrt (kAirDensity * omega / kAirViscosity);
    const double d = 1.045 / rv * wallLoss;
    return { omega / kSpeedOfSound * (1.0 + d), -omega / kSpeedOfSound * d };
}

// Impedance at the input of a cylinder of length l terminated by zLoad.
Complex throughCylinder (Complex zLoad, double z0, Complex k, double l)
{
    const Complex c = std::cos (k * l), s = std::sin (k * l);
    return z0 * (zLoad * c + j * z0 * s) / (z0 * c + j * zLoad * s);
}

// Shunt admittance of a tone hole that is partly open: the open part behaves like a short tube radiating
// outside (an inertance), the covered part like a closed chimney (a small compliance).
Complex holeAdmittance (const Geometry& g, int hole, double openness, double omega)
{
    const double b = 0.5 * g.holes[(size_t) hole].diameter;
    const double area = kPi * b * b;
    const double z0h = kAirDensity * kSpeedOfSound / area;
    const double k = omega / kSpeedOfSound;
    const double chimney = std::max (g.wallThickness, 0.0003);

    const Complex zOpen = j * omega * kAirDensity * g.holeEffectiveLength (hole) / area
                          + z0h * 0.25 * (k * b) * (k * b);  // Radiation resistance.
    const Complex yClosed = j * std::tan (k * chimney) / z0h;

    return openness / zOpen + (1.0 - openness) * yClosed;
}
} // namespace

Openness Openness::fromPattern (const char* pattern)
{
    Openness open;
    for (int i = 0; i < kNumHoles + 1 && pattern[i] != 0; ++i)
    {
        const char c = pattern[i];
        const double value = c == 'x' ? 0.0 : c == 'h' ? 0.5 : c == 'p' ? 0.25 : 1.0;
        if (i < kNumHoles)
            open.holes[(size_t) i] = value;
        else
            open.foot = value;
    }
    return open;
}

Complex windowImpedance (const Geometry& g, const Openness& open, double hz)
{
    const double omega = 2.0 * kPi * hz;

    // Start at the foot (unflanged pipe radiation, or closed by the bell sensor) and work up to the window.
    const double aFoot = g.radiusAt (g.boreLength);
    const double z0Foot = kAirDensity * kSpeedOfSound / (kPi * aFoot * aFoot);
    const double kaFoot = omega / kSpeedOfSound * aFoot;
    const double footOpen = g.bellClosesFoot ? juce::jlimit (1.0e-4, 1.0, open.foot) : 1.0;
    Complex z = z0Foot * (0.25 * kaFoot * kaFoot + j * 0.6133 * kaFoot) / footOpen;

    // Points where something happens, from the foot up: the enabled holes, then the window at x = 0.
    std::vector<int> order;
    for (int i = 0; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            order.push_back (i);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return g.holes[(size_t) a].position > g.holes[(size_t) b].position; });

    double x = g.boreLength;
    auto travelTo = [&] (double target)
    {
        // Short cylindrical slices follow a tapered bore.
        while (x > target + 1.0e-9)
        {
            const double step = std::min (0.005, x - target);
            const double a = g.radiusAt (x - 0.5 * step);
            const double z0 = kAirDensity * kSpeedOfSound / (kPi * a * a);
            z = throughCylinder (z, z0, waveNumber (omega, a, g.wallLoss), step);
            x -= step;
        }
    };

    for (int hole : order)
    {
        const double position = juce::jlimit (0.0, g.boreLength, g.holes[(size_t) hole].position);
        travelTo (position);
        const double openness = juce::jlimit (0.0, 1.0, open.holes[(size_t) hole]);
        const double aHole = g.radiusAt (position);
        const Complex halfSeries = j * omega * kAirDensity * 0.5 * g.holeSeriesLength (hole, openness) / (kPi * aHole * aHole);
        z = halfSeries + 1.0 / (1.0 / (z + halfSeries) + holeAdmittance (g, hole, openness, omega));
    }
    travelTo (0.0);

    // The window: a short opening the air moves through, in series as seen by the jet.
    // Its outer part radiates (as in WhistleModel): that air mass in parallel with rho c / Aw.
    const double windowRadius = std::sqrt (g.windowArea() / kPi);
    const double outerMass = kAirDensity * 0.5 * windowRadius / g.windowArea(), windowR = kAirDensity * kSpeedOfSound / g.windowArea();
    const Complex outer = j * omega * outerMass * windowR / (windowR + j * omega * outerMass);
    const Complex zWindow = j * omega * (kAirDensity * g.windowEffectiveLength() / g.windowArea() - outerMass) + outer;
    return z + zWindow;
}

std::vector<double> resonances (const Geometry& g, const Openness& open, double fMin, double fMax)
{
    // Scan in 2-cent steps, then refine each minimum of |Z| with a parabola through its neighbours (in log |Z|).
    const double step = std::pow (2.0, 2.0 / 1200.0);
    std::vector<double> freqs, mags;
    for (double f = fMin; f <= fMax; f *= step)
    {
        freqs.push_back (f);
        mags.push_back (std::log (std::abs (windowImpedance (g, open, f))));
    }

    std::vector<double> found;
    for (size_t i = 1; i + 1 < mags.size(); ++i)
    {
        if (mags[i] < mags[i - 1] && mags[i] <= mags[i + 1])
        {
            const double denom = mags[i - 1] - 2.0 * mags[i] + mags[i + 1];
            const double offset = denom > 0.0 ? 0.5 * (mags[i - 1] - mags[i + 1]) / denom : 0.0;
            found.push_back (freqs[i] * std::pow (step, offset));
        }
    }
    return found;
}

std::vector<int> chartHoles (const Geometry& g)
{
    std::vector<int> holes;
    if (g.chart == FingeringChart::baroqueRecorder)
    {
        for (int i = 0; i < kNumHoles; ++i)
            holes.push_back (i);
        return holes;
    }
    for (int i = 1; i < kNumHoles; ++i)  // Front holes, top first
        if (g.holes[(size_t) i].enabled)
            holes.push_back (i);
    return holes;
}

int ChartFingering::holeFor (const Geometry& g, int finger)
{
    const auto holes = chartHoles (g);
    return finger >= 0 && finger < (int) holes.size() ? holes[(size_t) finger] : -1;
}

Openness ChartFingering::openness (const Geometry& g) const
{
    Openness open;
    for (auto& h : open.holes)
        h = 1.0;
    const auto holes = chartHoles (g);
    auto value = [] (char c) { return c == 'x' ? 0.0 : c == 'h' ? 0.5 : c == 'p' ? 0.25 : 1.0; };
    size_t finger = 0;
    for (; finger < holes.size() && pattern[finger] != 0; ++finger)
        open.holes[(size_t) holes[finger]] = value (pattern[finger]);
    if (finger == holes.size() && pattern[finger] != 0)  // A character after the fingers: the foot (bell)
        open.foot = value (pattern[finger]);
    return open;
}

juce::String setChart (Geometry& g, FingeringChart chart)
{
    g.chart = chart;
    auto& h = g.holes;
    juce::StringArray opened, plugged;
    auto open = [&] (int i, double position, double diameter)
    {
        if (h[(size_t) i].enabled)
            return;
        h[(size_t) i] = { true, std::max (0.25 * h[2].position, position), diameter };
        opened.add (kHoleNames[(size_t) i]);
    };
    auto plug = [&] (int i)
    {
        if (! h[(size_t) i].enabled)
            return;
        h[(size_t) i].enabled = false;
        plugged.add (kHoleNames[(size_t) i]);
    };

    if (chart == FingeringChart::baroqueRecorder)
    {
        // Starting places, kept from the holes there are: L1 a hole spacing above L2, the thumb a little above L1 on
        // the back, R4 a spacing below R3. Tune holes then places them all.
        if (! h[1].enabled)
            open (1, h[2].position - (h[3].position - h[2].position), h[2].diameter);
        open (0, h[1].position - 0.25 * (h[2].position - h[1].position), 0.8 * h[1].diameter);
        if (! h[7].enabled)
            open (7, std::min (h[6].position + (h[6].position - h[5].position), g.boreLength - 0.02), 0.85 * h[6].diameter);
    }
    else
    {
        plug (0);
        int front = 0;
        for (int i = 1; i < kNumHoles; ++i)
            front += h[(size_t) i].enabled ? 1 : 0;
        if (front > 6)
            plug (h[7].enabled ? 1 : 7);
    }

    juce::String out;
    if (! opened.isEmpty())
        out << "opened " << opened.joinIntoString (", ");
    if (! plugged.isEmpty())
        out << (out.isEmpty() ? "" : "; ") << "plugged " << plugged.joinIntoString (", ");
    return out;
}

std::string ChartFingering::holePattern (const Geometry& g) const
{
    std::string out (kNumHoles, '-');
    const auto holes = chartHoles (g);
    for (size_t finger = 0; finger < holes.size() && pattern[finger] != 0; ++finger)
        out[(size_t) holes[finger]] = pattern[finger];
    return out;
}

const std::vector<ChartFingering>& chartFor (FingeringChart chart)
{
    constexpr int bore = ChartFingering::kTunesBore, none = ChartFingering::kTunesNothing;

    // Whistle: six fingers, top first (finger 0 is the top hole, L1 or L2 depending on the sensors used).
    static const std::vector<ChartFingering> whistle {
        { "D", "xxxxxx", 0, 1, bore },         { "E", "xxxxxo", 2, 1, 5 },            { "F#", "xxxxoo", 4, 1, 4 },
        { "G", "xxxooo", 5, 1, 3 },            { "A", "xxoooo", 7, 1, 2 },            { "B", "xooooo", 9, 1, 1 },
        { "C nat", "oxxooo", 10, 1, none, 0 }, { "C#", "oooooo", 11, 1, 0 },
        // High D overblown with every hole covered is shown but left out of the tuning: whistle players don't use it.
        { "high D", "oxxxxx", 12, 2 },         { "high D", "xxxxxx", 12, 2, none, none, false },        { "high E", "xxxxxo", 14, 2 },
        { "high F#", "xxxxoo", 16, 2 },        { "high G", "xxxooo", 17, 2 },         { "high A", "xxoooo", 19, 2 },
        { "high B", "xooooo", 21, 2 },
    };

    // Recorder: the eight holes by name, LT L1 L2 L3 R1 R2 R3 R4 (fingers = hole indices); a ninth character is
    // the foot (bell). Baroque fingering in C, from the Dolmetsch chart for recorders in C, with its forks; the
    // thumb is pinched ('p', a small crack open) for the second and third octaves.
    // The pairings below seed the tuner and say which sizes it may change; fitTuning then settles the values over
    // all the notes, so none of these is exact any more.
    // Each hole's position is set by a note that uses it (R2 by the forked F, the thumb hole by high D: thumb
    // open, L2 covered). With cross-fingering tuning the hole sizes tune the forks and the upper notes, as a maker
    // voices them (a hole's position mostly sets its low note, its size its upper notes): R1 by the F# fork, L3
    // by high A, the thumb hole (every pinched note vents through it) by high Bb and R3 by high B. Top C is left
    // to the player: it only adds L2 to high B's fingering, and with L2's position held by low B no hole size moves
    // it apart from high B (it stays ~15 c below, WhistleCheck grid), so a recorder player opens the thumb a
    // little wider. The other forks and upper notes are left to the geometry.
    static const std::vector<ChartFingering> recorder {
        { "C", "xxxxxxxx", 0, 1, bore },          { "D", "xxxxxxxo", 2, 1, 7 },          { "E", "xxxxxxoo", 4, 1, 6 },
        { "F", "xxxxxoxx", 5, 1, 5 },             { "F#", "xxxxoxxo", 6, 1, none, 4 },   { "G", "xxxxoooo", 7, 1, 4 },
        { "A", "xxxooooo", 9, 1, 3 },             { "Bb", "xxoxxooo", 10, 1 },           { "B", "xxoooooo", 11, 1, 2 },
        { "high C", "xoxooooo", 12, 1, 1 },       { "high D", "ooxooooo", 14, 1, 0 },
        { "high E", "pxxxxxoo", 16, 2 },          { "high F", "pxxxxoxo", 17, 2 },       { "high F#", "pxxxoxoo", 18, 2 },
        { "high G", "pxxxoooo", 19, 2 },          { "high G#", "pxxoxooo", 20, 2 },      { "high A", "pxxooooo", 21, 2, none, 3 },
        { "high Bb", "pxxooxxo", 22, 2, none, 0 }, { "high B", "pxxoxxoo", 23, 2, none, 6 },
        { "top C", "pxooxxoo", 24, 3 },                     { "top C#", "pxoxxoxox", 25, 3 },      { "top D", "pxoxxoxo", 26, 3 },
    };

    switch (chart)
    {
        case FingeringChart::baroqueRecorder: return recorder;
        case FingeringChart::whistle: break;
    }
    return whistle;
}

std::string fingeringId (const ChartFingering& c)
{
    return std::string (c.name) + " " + c.pattern;
}

bool fingeringCounts (const Geometry& g, const ChartFingering& c)
{
    const auto found = g.tuning.fingerings.find (fingeringId (c));
    return found != g.tuning.fingerings.end() ? found->second : c.counts;
}

namespace
{
// The degrees (semitones above the tonic) of a major or natural minor scale.
bool inScale (int degree, bool minor)
{
    static constexpr bool major[] = { true, false, true, false, true, true, false, true, false, true, false, true };
    static constexpr bool naturalMinor[] = { true, false, true, true, false, true, false, true, true, false, true, false };
    return (minor ? naturalMinor : major)[((degree % 12) + 12) % 12];
}

// Solves a small linear system A x = b (Gaussian elimination, partial pivoting). False if singular.
bool solveSmall (std::vector<std::vector<double>> a, std::vector<double> b, std::vector<double>& x)
{
    const size_t n = b.size();
    for (size_t col = 0; col < n; ++col)
    {
        size_t pivot = col;
        for (size_t row = col + 1; row < n; ++row)
            if (std::abs (a[row][col]) > std::abs (a[pivot][col]))
                pivot = row;
        if (std::abs (a[pivot][col]) < 1.0e-12)
            return false;
        std::swap (a[col], a[pivot]);
        std::swap (b[col], b[pivot]);
        for (size_t row = col + 1; row < n; ++row)
        {
            const double factor = a[row][col] / a[col][col];
            for (size_t k = col; k < n; ++k)
                a[row][k] -= factor * a[col][k];
            b[row] -= factor * b[col];
        }
    }
    x.assign (n, 0.0);
    for (size_t row = n; row-- > 0;)
    {
        double sum = b[row];
        for (size_t k = row + 1; k < n; ++k)
            sum -= a[row][k] * x[k];
        x[row] = sum / a[row][row];
    }
    return true;
}

// The offsets (cents from equal, degree 0 = 0) that make the chords of the weighted keys as pure as possible:
// for each key its tonic chord (weight 1) and the chords on its fourth and fifth degree (0.5 each, minor ones in
// a minor key, so they stay in its scale), each with a pure fifth and a pure third (least squares).
std::array<double, 12> bestForKeys (const TuningGoal& goal)
{
    constexpr double kFifth = 701.955, kMajorThird = 386.314, kMinorThird = 315.641;
    struct Interval { int from, to; double cents, weight; };
    std::vector<Interval> intervals;
    for (int key = 0; key < 24; ++key)
    {
        const double weight = goal.keys[(size_t) key];
        if (weight <= 0.0)
            continue;
        const bool minor = key >= TuningGoal::kMinor;
        const int tonic = key % 12;
        for (const auto& [root, share] : { std::pair { 0, 1.0 }, std::pair { 5, 0.5 }, std::pair { 7, 0.5 } })
        {
            const int r = tonic + root;
            const double w = weight * share;
            intervals.push_back ({ r, r + 7, kFifth, w });
            intervals.push_back ({ r, r + (minor ? 3 : 4), minor ? kMinorThird : kMajorThird, w });
        }
    }

    // Unknowns: the offsets of degrees 1-11. Each interval wants offset(to) - offset(from) = pure - equal; a tiny
    // pull to 0 settles the degrees no chord uses.
    constexpr size_t n = 11;
    std::vector<std::vector<double>> normal (n, std::vector<double> (n, 0.0));
    std::vector<double> right (n, 0.0), x;
    for (size_t k = 0; k < n; ++k)
        normal[k][k] += 1.0e-4;
    for (const auto& i : intervals)
    {
        const int to = ((i.to % 12) + 12) % 12, from = ((i.from % 12) + 12) % 12;
        const double want = i.cents - 100.0 * (i.to - i.from);
        // Row: +1 at `to`, -1 at `from` (degree 0 is fixed at 0, so it has no column).
        std::array<std::pair<int, double>, 2> row { { { to, 1.0 }, { from, -1.0 } } };
        for (const auto& [a, va] : row)
        {
            if (a == 0)
                continue;
            right[(size_t) a - 1] += i.weight * va * want;
            for (const auto& [b, vb] : row)
                if (b != 0)
                    normal[(size_t) a - 1][(size_t) b - 1] += i.weight * va * vb;
        }
    }
    std::array<double, 12> offsets {};
    if (! intervals.empty() && solveSmall (normal, right, x))
        for (size_t k = 0; k < n; ++k)
            offsets[k + 1] = x[k];
    return offsets;
}
} // namespace

std::array<double, 12> temperamentCents (const TuningGoal& goal)
{
    std::array<double, 12> offsets {};
    switch (goal.temperament)
    {
        case Temperament::equal:
            return offsets;
        case Temperament::meantone:
        {
            // Fifths a quarter of a syntonic comma narrow (696.58 c): degree d is f fifths from the tonic, f in -5..6.
            constexpr double kNarrowing = 696.578 - 700.0;
            for (int f = -5; f <= 6; ++f)
                offsets[(size_t) (((7 * f) % 12 + 12) % 12)] = f * kNarrowing;
            break;
        }
        case Temperament::just:
        {
            // 1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 16/9 15/8 (16/9 for the flat seventh: the fourth of the fourth).
            static constexpr double ratios[] = { 1.0, 16.0 / 15.0, 9.0 / 8.0, 6.0 / 5.0, 5.0 / 4.0, 4.0 / 3.0,
                                                 45.0 / 32.0, 3.0 / 2.0, 8.0 / 5.0, 5.0 / 3.0, 16.0 / 9.0, 15.0 / 8.0 };
            for (int d = 0; d < 12; ++d)
                offsets[(size_t) d] = 1200.0 * std::log2 (ratios[d]) - 100.0 * d;
            break;
        }
        case Temperament::bestForKeys:
            offsets = bestForKeys (goal);
            break;
        case Temperament::harjedalSwedish:
            offsets[2] = -20.0;
            offsets[4] = 35.0;
            offsets[9] = -20.0;
            break;
        case Temperament::harjedalEnglish:
            offsets[4] = offsets[9] = offsets[11] = -22.5;
            break;
        case Temperament::custom:
            offsets = goal.custom;
            offsets[0] = 0.0;  // The slide sets the overall pitch
            break;
    }
    const double keep = 1.0 - juce::jlimit (0.0, 1.0, goal.towardEqual);
    for (auto& o : offsets)
        o *= keep;
    return offsets;
}

double targetCents (const Geometry& g, const ChartFingering& c)
{
    const int degree = (((int) std::lround (c.semitones)) % 12 + 12) % 12;
    return temperamentCents (g.tuning)[(size_t) degree];
}

double targetHz (const Geometry& g, const ChartFingering& c, double lowestHz)
{
    return lowestHz * std::pow (2.0, (c.semitones + 0.01 * targetCents (g, c)) / 12.0);
}

double predictedCents (const Geometry& g, const ChartFingering& c, double lowestHz)
{
    const double target = targetHz (g, c, lowestHz);
    double nearest = 0.0;
    for (double f : resonances (g, c.openness (g), target * 0.7, target * 1.4))
        if (nearest == 0.0 || std::abs (std::log2 (f / target)) < std::abs (std::log2 (nearest / target)))
            nearest = f;
    return nearest > 0.0 ? 1200.0 * std::log2 (nearest / target) : 0.0;
}

namespace
{
double firstResonance (const Geometry& g, const Openness& open, double guessHz)
{
    const auto found = resonances (g, open, guessHz * 0.7, guessHz * 1.4);
    return found.empty() ? 0.0 : found.front();
}

// Solves f(value) = target for one length by the secant method (pitch falls as the length grows).
// Resonances come from a 2-cent scan refined by interpolation, so aim for 0.5 cents and accept 2.
bool solveLength (Geometry& g, double& value, double lo, double hi, const Openness& open, double target)
{
    double x0 = value, f0 = firstResonance (g, open, target);
    double x1 = juce::jlimit (lo, hi, value * (f0 > 0.0 ? f0 / target : 1.0));
    double bestValue = value, bestError = 1.0e9;
    for (int iteration = 0; iteration < 30; ++iteration)
    {
        value = x1;
        const double f1 = firstResonance (g, open, target);
        if (f1 <= 0.0 || f0 <= 0.0)
            break;
        const double error = std::log2 (f1 / target) * 1200.0;
        if (std::abs (error) < std::abs (bestError))
        {
            bestError = error;
            bestValue = value;
        }
        if (std::abs (error) < 0.5)
            return true;
        const double slope = (std::log2 (f1) - std::log2 (f0)) / (x1 - x0);
        const double next = std::abs (slope) > 1.0e-12 ? x1 - std::log2 (f1 / target) / slope : x1 * (f1 / target);
        x0 = x1;
        f0 = f1;
        x1 = juce::jlimit (lo, hi, next);
    }
    value = bestValue;
    return std::abs (bestError) < 2.0;
}

// Tunes the bore and hole positions (diameters fixed), in chart order: the lowest note first, then holes
// from the bottom up. Moving a hole slightly changes the notes below it (its closed chimney is part of
// their bore), so the pass is repeated.
//
// With `resize`, a hole that can't reach its note gets a new size (the maker's trade-off between a hole's
// size and position): one that would have to sit lower than the hole below allows is made smaller (it vents
// less, so it sits higher for the same note), one that would have to sit above the top of its range larger.
bool tunePositions (Geometry& g, double lowestHz, bool resize = false)
{
    const auto& chart = chartFor (g.chart);
    auto targetFor = [&] (const ChartFingering& c) { return targetHz (g, c, lowestHz); };

    // Closely spaced holes (e.g. a recorder's) need more passes.
    for (int pass = 0; pass < 20; ++pass)
    {
        double below = g.boreLength;  // Position of the hole tuned just before (lower down).
        for (const auto& c : chart)
        {
            const int index = ChartFingering::holeFor (g, c.tunes);
            if (c.tunes == ChartFingering::kTunesBore)
            {
                const double before = g.boreLength;
                if (! solveLength (g, g.boreLength, 0.05, 2.0, c.openness (g), targetFor (c)))
                    return false;
                below = g.boreLength;
                // A new key can change the length a lot: start the holes from where they would be on the scaled
                // instrument, not where they were (on a shorter bore some would be past the foot).
                if (pass == 0)
                    for (auto& h : g.holes)
                        h.position *= g.boreLength / before;
            }
            else if (index >= 0 && g.holes[(size_t) index].enabled)
            {
                auto& hole = g.holes[(size_t) index];
                auto upperLimit = [&] { return index == 0 ? g.boreLength - hole.diameter  // The thumb hole is on the back.
                                                          : below - hole.diameter; };
                bool solved = solveLength (g, hole.position, 0.01, upperLimit(), c.openness (g), targetFor (c));
                for (int attempt = 0; ! solved && resize && attempt < 20; ++attempt)
                {
                    const bool wantsLower = hole.position > upperLimit() - 0.0005;  // Stopped at the hole below
                    const double widest = 0.85 * 2.0 * g.radiusAt (hole.position);
                    const double resized = juce::jlimit (0.0025, widest, hole.diameter * (wantsLower ? 0.9 : 1.1));
                    if (std::abs (resized - hole.diameter) < 1.0e-6)
                        break;  // At the size limit
                    hole.diameter = resized;
                    solved = solveLength (g, hole.position, 0.01, upperLimit(), c.openness (g), targetFor (c));
                }
                if (! solved)
                    return false;
                if (index != 0)
                    below = hole.position;
            }
        }

        double worst = 0.0;
        for (const auto& c : chart)
            if (c.tunes == ChartFingering::kTunesBore || ChartFingering::holeFor (g, c.tunes) >= 0)
                worst = std::max (worst, std::abs (predictedCents (g, c, lowestHz)));
        if (worst < 1.5)
            return true;
    }
    return false;
}
} // namespace

namespace
{
// Solves a small linear system A x = b (Gaussian elimination, partial pivoting). False if singular.
bool solveLinear (std::vector<std::vector<double>> a, std::vector<double> b, std::vector<double>& x)
{
    const size_t n = b.size();
    for (size_t col = 0; col < n; ++col)
    {
        size_t pivot = col;
        for (size_t row = col + 1; row < n; ++row)
            if (std::abs (a[row][col]) > std::abs (a[pivot][col]))
                pivot = row;
        if (std::abs (a[pivot][col]) < 1.0e-12)
            return false;
        std::swap (a[col], a[pivot]);
        std::swap (b[col], b[pivot]);
        for (size_t row = col + 1; row < n; ++row)
        {
            const double factor = a[row][col] / a[col][col];
            for (size_t k = col; k < n; ++k)
                a[row][k] -= factor * a[col][k];
            b[row] -= factor * b[col];
        }
    }
    x.assign (n, 0.0);
    for (size_t row = n; row-- > 0;)
    {
        double sum = b[row];
        for (size_t k = row + 1; k < n; ++k)
            sum -= a[row][k] * x[k];
        x[row] = sum / a[row][row];
    }
    return true;
}
} // namespace

double tuningWeight (const Geometry& g, const ChartFingering& c)
{
    if (! fingeringCounts (g, c))
        return 0.0;
    if (const auto set = g.tuning.weights.find (fingeringId (c)); set != g.tuning.weights.end())
        return set->second;  // The player's own
    double weight = 1.0;
    // A note in none of the keys that count (a fork or cross-fingering in another key) 0.3, one in all of them 1:
    // a player leans on the others anyway, and they shouldn't pull the keys' notes out of tune. The third octave
    // 0.3 as well.
    {
        const int degree = ((int) std::lround (c.semitones)) % 12;
        double share = 0.0, most = 0.0;
        for (int d = 0; d < 12; ++d)
        {
            double here = 0.0;
            for (int key = 0; key < 24; ++key)
                if (g.tuning.keys[(size_t) key] > 0.0 && inScale (d - key % 12, key >= TuningGoal::kMinor))
                    here += g.tuning.keys[(size_t) key];
            most = std::max (most, here);
            if (d == degree)
                share = here;
        }
        weight = most > 0.0 ? 0.3 + 0.7 * share / most : (inScale (degree, false) ? 1.0 : 0.3);
        if (c.reg >= 3)
            weight *= 0.3;
        if (g.chart == FingeringChart::whistle && g.tuning.topHoleForHighD && std::string (c.pattern) == "oxxooo")
            weight *= 0.1;  // C natural gives L1's size up to high D (TuningGoal::topHoleForHighD)
    }
    // A note with two fingerings that count (the whistle's high D) counts once in all.
    int sameNote = 0;
    for (const auto& other : chartFor (g.chart))
        if (fingeringCounts (g, other) && std::abs (other.semitones - c.semitones) < 0.01)
            ++sameNote;
    return weight / std::max (1, sameNote);
}

TuningError tuningError (const Geometry& g, double lowestHz)
{
    TuningError result;
    double sum = 0.0, total = 0.0;
    for (const auto& c : chartFor (g.chart))
    {
        const double cents = predictedCents (g, c, lowestHz), weight = tuningWeight (g, c);
        if (std::abs (cents) >= kCentsNotReachable)
            continue;  // A plugged hole's note
        sum += weight * cents * cents;
        total += weight;
        if (weight >= 0.5 - 1.0e-9)
            result.worstScaleNote = std::max (result.worstScaleNote, std::abs (cents));
    }
    result.rms = total > 0.0 ? std::sqrt (sum / total) : 0.0;
    return result;
}

void setSlide (Geometry& g, double slide)
{
    const double change = slide - g.slide;
    const double joint = g.slideJoint();
    g.boreLength += change;
    for (auto& hole : g.holes)
        if (hole.position > joint)
            hole.position += change;
    g.slide = slide;
}

double bestSlide (const Geometry& g, double lowestHz, const std::vector<double>* pullCents)
{
    // Each note's cents move almost in proportion to the slide (a short tube more than a long one), so a few
    // Gauss-Newton steps on the weighted squared error find the best setting. A note's pull from its resonance is
    // taken to stay the same while the slide moves.
    struct Note { const ChartFingering* fingering; double weight, pull; };
    std::vector<Note> notes;
    const auto& chart = chartFor (g.chart);
    for (size_t i = 0; i < chart.size(); ++i)
    {
        const double pull = pullCents != nullptr ? (i < pullCents->size() ? (*pullCents)[i] : std::nan ("")) : 0.0;
        if (std::isfinite (pull) && std::abs (predictedCents (g, chart[i], lowestHz)) < kCentsNotReachable)
            notes.push_back ({ &chart[i], tuningWeight (g, chart[i]), pull });
    }
    if (notes.empty())
        return g.slide;

    constexpr double kStep = 0.0005;  // m: the change the slope is measured over
    auto at = [&] (double slide)
    {
        auto moved = g;
        setSlide (moved, slide);
        std::vector<double> cents;
        for (const auto& n : notes)
            cents.push_back (predictedCents (moved, *n.fingering, lowestHz) + n.pull);
        return cents;
    };
    double slide = g.slide;
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const auto here = at (slide), further = at (slide + kStep);
        double gradient = 0.0, curvature = 0.0;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const double slope = (further[i] - here[i]) / kStep;
            gradient += notes[i].weight * here[i] * slope;
            curvature += notes[i].weight * slope * slope;
        }
        if (curvature <= 0.0)
            break;
        const double next = juce::jlimit (kSlideMin, kSlideMax, slide - gradient / curvature);
        const bool settled = std::abs (next - slide) < 0.00002;
        slide = next;
        if (settled)
            break;
    }
    return slide;
}

void fitTuning (Geometry& g, double lowestHz, bool fitSizes, const std::vector<double>* aimCents, bool fitHead)
{
    // The notes to fit, as (fingering, square root of its weight, the resonance's aim in cents from the note). A
    // note far from its aim isn't this instrument's (its hole is plugged): chasing it would only detune the others.
    struct Note { const ChartFingering* fingering; double rootWeight, aim; };
    std::vector<Note> notes;
    const auto& chart = chartFor (g.chart);
    for (size_t i = 0; i < chart.size(); ++i)
    {
        const double aim = aimCents != nullptr && i < aimCents->size() ? (*aimCents)[i] : 0.0;
        if (std::abs (predictedCents (g, chart[i], lowestHz) - aim) < kCentsNotReachable)
            notes.push_back ({ &chart[i], std::sqrt (tuningWeight (g, chart[i])), aim });
    }

    // The unknowns: the bore's length, each hole's position in use, the sizes the chart lets the tuner set, and with
    // fitHead the head section's diameter (it sets how the octaves lie, which the holes can't).
    constexpr int kBore = -1, kHead = -2;
    struct Unknown { int hole; bool isSize; };
    std::vector<Unknown> unknowns { { kBore, false } };
    std::vector<int> sized;
    for (const auto& c : chartFor (g.chart))
    {
        const int hole = ChartFingering::holeFor (g, c.tunes);
        if (hole >= 0 && g.holes[(size_t) hole].enabled)
            unknowns.push_back ({ hole, false });
        // A size is fitted only for a note that counts (the Härjedalspipa leaves C natural out: L1 keeps its size).
        const int sizeHole = fingeringCounts (g, c) ? ChartFingering::holeFor (g, c.tunesDiameter) : -1;
        if (fitSizes && sizeHole >= 0 && g.holes[(size_t) sizeHole].enabled
            && std::find (sized.begin(), sized.end(), sizeHole) == sized.end())
            sized.push_back (sizeHole);
    }
    for (int hole : sized)
        unknowns.push_back ({ hole, true });
    if (fitHead && g.headLength > 0.0)
        unknowns.push_back ({ kHead, true });
    const size_t m = unknowns.size();

    // Resizing a hole is a bigger change to the instrument than moving it, and changes its tone: each 10 % is
    // charged like 1.5 cents on a note, so that a hole isn't reshaped for a fraction of a cent.
    const auto startSizes = g.holes;
    const double startHead = g.headDiameter;
    constexpr double kCentsPerLogSize = 15.0;

    auto errors = [&] (const Geometry& trial)
    {
        std::vector<double> out;
        out.reserve (notes.size() + sized.size());
        for (const auto& n : notes)
            out.push_back (n.rootWeight * (predictedCents (trial, *n.fingering, lowestHz) - n.aim));
        for (int hole : sized)
        {
            // L1 given to high D (TuningGoal::topHoleForHighD) is free to change a lot: its size moves high D only ~3 c
            // per 10 %, and the player asked for it.
            const bool givenToHighD = g.chart == FingeringChart::whistle && g.tuning.topHoleForHighD && hole == ChartFingering::holeFor (g, 0);
            out.push_back ((givenToHighD ? 0.1 : 1.0) * kCentsPerLogSize * std::log (trial.holes[(size_t) hole].diameter / startSizes[(size_t) hole].diameter));
        }
        if (fitHead && g.headLength > 0.0)
            out.push_back (kCentsPerLogSize * std::log (trial.headDiameter / startHead));
        return out;
    };
    auto cost = [] (const std::vector<double>& e)
    {
        double sum = 0.0;
        for (double v : e)
            sum += v * v;
        return sum;
    };

    // Applies a change (m, or log size) to the unknowns and keeps the holes where a hole can be: each front hole
    // above the next one down, the lowest above the foot. The thumb hole is on the back, so it has only the ends.
    auto apply = [&] (Geometry& trial, const std::vector<double>& step)
    {
        for (size_t k = 0; k < m; ++k)
        {
            const auto u = unknowns[k];
            if (u.hole == kBore)
                trial.boreLength = juce::jlimit (0.05, 2.0, trial.boreLength + step[k]);
            else if (u.hole == kHead)  // Within 60-140 % of the bore below it
                trial.headDiameter = juce::jlimit (0.6 * trial.boreDiameterTop, 1.4 * trial.boreDiameterTop, trial.headDiameter * std::exp (step[k]));
            else if (u.isSize)
            {
                auto& hole = trial.holes[(size_t) u.hole];
                const double widest = 0.85 * 2.0 * trial.radiusAt (hole.position);
                hole.diameter = juce::jlimit (0.0025, widest, hole.diameter * std::exp (step[k]));
            }
            else
                trial.holes[(size_t) u.hole].position += step[k];
        }
        double below = trial.boreLength;
        for (int i = kNumHoles - 1; i >= 0; --i)
        {
            auto& hole = trial.holes[(size_t) i];
            if (! hole.enabled)
                continue;
            hole.position = juce::jlimit (0.01, std::max (0.01, below - hole.diameter), hole.position);
            if (i != 0)
                below = hole.position;
        }
    };

    auto error = errors (g);
    double damping = 0.01;
    for (int iteration = 0; iteration < 30; ++iteration)
    {
        // Cents per metre or per unit of log size, from trying each unknown in turn. Big enough steps to be
        // well above the resolution of the resonance search.
        std::vector<std::vector<double>> jacobian (error.size(), std::vector<double> (m, 0.0));
        for (size_t k = 0; k < m; ++k)
        {
            const double delta = unknowns[k].isSize ? 0.05 : 0.0004;
            std::vector<double> step (m, 0.0);
            step[k] = delta;
            auto trial = g;
            apply (trial, step);
            const auto moved = errors (trial);
            for (size_t i = 0; i < error.size(); ++i)
                jacobian[i][k] = (moved[i] - error[i]) / delta;
        }

        // Levenberg-Marquardt: (J'J + damping diag) step = -J'e, with more damping while a step doesn't help.
        bool improved = false;
        double before = cost (error), after = before;
        for (int attempt = 0; attempt < 8 && ! improved; ++attempt)
        {
            std::vector<std::vector<double>> normal (m, std::vector<double> (m, 0.0));
            std::vector<double> gradient (m, 0.0), step;
            for (size_t r = 0; r < m; ++r)
                for (size_t i = 0; i < error.size(); ++i)
                {
                    gradient[r] -= jacobian[i][r] * error[i];
                    for (size_t c = 0; c < m; ++c)
                        normal[r][c] += jacobian[i][r] * jacobian[i][c];
                }
            double largest = 0.0;
            for (size_t r = 0; r < m; ++r)
                largest = std::max (largest, normal[r][r]);
            for (size_t r = 0; r < m; ++r)
                normal[r][r] = normal[r][r] * (1.0 + damping) + 1.0e-9 * largest;  // A hole against its limit has no slope
            if (! solveLinear (normal, gradient, step))
                break;

            for (size_t k = 0; k < m; ++k)  // Linear predictions are only good for small changes
                step[k] = unknowns[k].isSize ? juce::jlimit (-0.2, 0.2, step[k]) : juce::jlimit (-0.003, 0.003, step[k]);
            auto trial = g;
            apply (trial, step);
            auto trialError = errors (trial);
            if (cost (trialError) < before)
            {
                g = trial;
                error = std::move (trialError);
                after = cost (error);
                damping = std::max (1.0e-4, damping / 3.0);
                improved = true;
            }
            else
            {
                damping *= 5.0;
            }
        }
        if (! improved || before - after < 0.001 * before)
            break;
    }
}

bool tuneToScale (Geometry& g, double lowestHz, bool tuneCrossFingerings, bool resizeHoles)
{
    // Placing each hole for its own low note finds the right neighbourhood from any starting point; the fit then
    // shares the error out over all the notes.
    if (! tunePositions (g, lowestHz, resizeHoles))
        return false;
    fitTuning (g, lowestHz, tuneCrossFingerings);
    return true;
}

} // namespace whistle
