#include "PlayedTuning.h"

#include "ScalePlan.h"

#include <cmath>
#include <future>
#include <map>

namespace whistle
{

namespace
{
// Further than this from the note, the fingering plays something else (another register, or a wolf).
constexpr double kInRegisterCents = 70.0;

PlayedNote measureOne (const Geometry& g, const Voicing& v, double lowestHz, size_t index)
{
    const auto& c = chartFor (g.chart)[index];
    PlayedNote note;
    note.chartIndex = index;
    note.resonance = predictedCents (g, c, lowestHz);
    if (std::abs (note.resonance) >= kCentsNotReachable)
        return note;  // A plugged hole's note

    const double target = targetHz (g, c, lowestHz);
    const auto open = c.openness (g);
    auto centsAt = [&] (double pressure, double& cents)
    {
        const double f = playNote (g, v, open, pressure);
        cents = f > 0.0 ? 1200.0 * std::log2 (f / target) : 1.0e9;
        return std::abs (cents) < kInRegisterCents;
    };

    // Up from a whisper in 18 % steps: the longest stretch of pressures where it plays as this note (near the
    // threshold a note may come and go, and a fingering can return to it after another register).
    constexpr double kStep = 1.18;
    double runStart = 0.0, runEnd = 0.0;
    for (double pressure = 60.0; pressure < 12000.0; pressure *= kStep)
    {
        double cents = 0.0;
        if (! centsAt (pressure, cents))
        {
            runStart = 0.0;
            continue;
        }
        if (runStart == 0.0)
            runStart = pressure;
        runEnd = pressure;
        if (note.lowPa == 0.0 || runEnd / runStart > note.highPa / note.lowPa)
        {
            note.lowPa = runStart;
            note.highPa = runEnd;
        }
    }
    if (note.lowPa == 0.0)
        return note;

    const double position = juce::jlimit (0.0, 1.0, g.tuning.breathPosition[c.reg >= 2 ? 1 : 0]);
    note.pressure = std::pow (note.lowPa, 1.0 - position) * std::pow (note.highPa, position);
    if (g.tuning.useLearnedBreath)
    {
        bool estimated = false;
        if (const double breath = playerBreath (g, c, estimated); breath > 0.0)
        {
            // Within the pressures it sounds at: blown softer or harder the model plays another register (or nothing).
            note.pressure = juce::jlimit (note.lowPa, note.highPa, breath);
            note.learned = ! estimated;
            note.estimated = estimated;
        }
    }
    double cents = 0.0;
    if (! centsAt (note.pressure, cents))
    {
        // A gap inside the range (hysteresis near a register change): measure at the nearest end instead.
        note.pressure = note.highPa;
        if (! centsAt (note.pressure, cents))
            return note;
    }
    note.sounds = true;
    note.cents = cents;
    return note;
}
} // namespace

double playerBreath (const Geometry& g, const ChartFingering& c, bool& estimated)
{
    estimated = false;
    const auto& learned = g.tuning.learnedBreath;
    if (const auto found = learned.find (fingeringId (c)); found != learned.end())
        return found->second;

    // The heard notes as (semitones, log pressure), one point per pitch (two fingerings of one note averaged).
    std::map<double, std::pair<double, int>> byPitch;
    for (const auto& other : chartFor (g.chart))
        if (const auto found = learned.find (fingeringId (other)); found != learned.end() && found->second > 0.0)
        {
            auto& [sum, count] = byPitch[other.semitones];
            sum += std::log (found->second);
            ++count;
        }
    if (byPitch.size() < 2)
        return 0.0;
    std::vector<std::pair<double, double>> points;
    for (const auto& [semitones, sumCount] : byPitch)
        points.push_back ({ semitones, sumCount.first / sumCount.second });

    estimated = true;
    const double x = c.semitones;
    for (size_t i = 1; i < points.size(); ++i)
        if (x <= points[i].first && x >= points[i - 1].first)
        {
            const double t = (x - points[i - 1].first) / (points[i].first - points[i - 1].first);
            return std::exp (points[i - 1].second + t * (points[i].second - points[i - 1].second));
        }

    // Beyond the heard notes: the trend of all of them, from the nearest.
    double meanX = 0.0, meanY = 0.0;
    for (const auto& [px, py] : points)
    {
        meanX += px;
        meanY += py;
    }
    meanX /= (double) points.size();
    meanY /= (double) points.size();
    double sxy = 0.0, sxx = 0.0;
    for (const auto& [px, py] : points)
    {
        sxy += (px - meanX) * (py - meanY);
        sxx += (px - meanX) * (px - meanX);
    }
    const double slope = juce::jlimit (0.0, std::log (2.0) / 3.5, sxx > 0.0 ? sxy / sxx : 0.0);  // ln Pa per semitone
    const auto& nearest = x < points.front().first ? points.front() : points.back();
    return std::exp (nearest.second + slope * (x - nearest.first));
}

std::vector<PlayedNote> measurePlayed (const Geometry& g, const Voicing& v, double lowestHz)
{
    const size_t count = chartFor (g.chart).size();
    std::vector<std::future<PlayedNote>> jobs;
    for (size_t i = 0; i < count; ++i)
        jobs.push_back (std::async (std::launch::async, [&g, &v, lowestHz, i] { return measureOne (g, v, lowestHz, i); }));
    std::vector<PlayedNote> notes;
    for (auto& job : jobs)
        notes.push_back (job.get());
    return notes;
}

double playedRms (const Geometry& g, const std::vector<PlayedNote>& notes)
{
    double sum = 0.0, total = 0.0;
    for (const auto& n : notes)
    {
        if (! n.sounds)
            continue;
        const double weight = tuningWeight (g, chartFor (g.chart)[n.chartIndex]);
        sum += weight * n.cents * n.cents;
        total += weight;
    }
    return total > 0.0 ? std::sqrt (sum / total) : 0.0;
}

bool tuneToPlayed (Geometry& g, const Voicing& v, double lowestHz, bool tuneCrossFingerings, bool shapeHead, int passes,
                   std::vector<PlayedNote>* played)
{
    // The instrument as it is competes too: a fresh tuning can come out a little worse than an earlier one.
    const auto original = g;
    const auto originalNotes = measurePlayed (g, v, lowestHz);

    if (shapeHead && g.headLength <= 0.0)
    {
        // A head section to shape: 30 mm (at most 12 % of the bore), starting as wide as the bore, so nothing
        // changes until the fit widens or narrows it.
        g.headLength = std::min (0.030, 0.12 * g.boreLength);
        g.headStraightLength = 0.0;
        g.headDiameter = 2.0 * g.radiusAt (g.headLength);
    }
    // As Tune holes always did: if the holes can't all reach their notes at their sizes, the stuck ones get new sizes.
    auto placed = g;
    if (! tuneToScale (placed, lowestHz, tuneCrossFingerings))
    {
        placed = g;
        if (! tuneToScale (placed, lowestHz, tuneCrossFingerings, true))
            return false;
    }
    g = placed;

    auto best = g;
    auto bestNotes = measurePlayed (g, v, lowestHz);
    double bestRms = playedRms (g, bestNotes);
    auto notes = bestNotes;
    std::vector<double> aims (chartFor (g.chart).size(), 0.0);
    for (int pass = 0; pass < passes; ++pass)
    {
        // The pull (played minus resonance) stays about the same when the holes move a little: aim each resonance
        // that far the other way from the note. A note that doesn't sound keeps its aim.
        // (played = resonance + pull, so a resonance at resonance - played plays the note.)
        for (const auto& n : notes)
            if (n.sounds)
                aims[n.chartIndex] = n.resonance - n.cents;
        fitTuning (g, lowestHz, tuneCrossFingerings, &aims, shapeHead);
        notes = measurePlayed (g, v, lowestHz);
        const double rms = playedRms (g, notes);
        if (rms < bestRms)
        {
            best = g;
            bestNotes = notes;
            bestRms = rms;
        }
    }
    if (playedRms (original, originalNotes) <= bestRms)
    {
        best = original;
        bestNotes = originalNotes;
    }
    g = best;
    if (played != nullptr)
        *played = bestNotes;
    return true;
}

} // namespace whistle
