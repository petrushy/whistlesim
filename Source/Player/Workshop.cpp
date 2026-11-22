#include "Workshop.h"

#include "Whistle/PitchDetect.h"

#include <algorithm>
#include <cstdio>

namespace workshop
{

namespace
{
const char* const kPitchNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

std::string join (const std::vector<std::string>& items, const char* separator)
{
    std::string out;
    for (const auto& item : items)
        out += (out.empty() ? "" : separator) + item;
    return out;
}
} // namespace

int midiOf (double hz, double referenceA) { return (int) std::lround (69.0 + 12.0 * std::log2 (hz / referenceA)); }

std::string noteName (double hz, double referenceA)
{
    const int midi = midiOf (hz, referenceA);
    return std::string (kPitchNames[(midi % 12 + 12) % 12]) + std::to_string (midi / 12 - 1);
}

std::string signedCents (double cents)
{
    const int c = (int) std::lround (cents);
    return (c >= 0 ? "+" : "") + std::to_string (c) + " c";
}

std::string silentText (int silent)
{
    return silent == 0 ? std::string() : " (" + std::to_string (silent) + (silent == 1 ? " note doesn't" : " notes don't") + " sound)";
}

std::string fixed (double value, int decimals)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%.*f", decimals, value);
    return text;
}

std::string mm (double metres) { return fixed (std::abs (metres) < 0.00005 ? 0.0 : metres * 1000.0, 1) + " mm"; }

std::string keyName (int key, double lowestHz, double referenceA)
{
    const int lowest = midiOf (lowestHz, referenceA);
    const bool minor = key >= whistle::TuningGoal::kMinor;
    return std::string (kPitchNames[((lowest + key % 12) % 12 + 12) % 12]) + (minor ? "m" : "");
}

double lowestNoteHz (const whistle::Geometry& g)
{
    int midi = g.tuning.lowestNote;
    if (midi < 0)
    {
        const auto r = whistle::resonances (g, whistle::Openness::allClosed(), 100.0, 2000.0);
        if (r.empty())
            return 587.33;
        midi = (int) std::lround (69.0 + 12.0 * std::log2 (r.front() / g.referenceA));
    }
    const int lowest = (midi >= 72 ? 72 : 60) + (midi % 12 + 12) % 12;
    return g.referenceA * std::pow (2.0, (lowest - 69) / 12.0);
}

Table buildTable (const whistle::Geometry& geometry, double lowest, const std::vector<whistle::PlayedNote>* playedNotes)
{
    const auto& chart = whistle::chartFor (geometry.chart);
    const bool playedValid = playedNotes != nullptr;
    Table table;

    // Each note's pull from its resonance, as last measured; the slide is tuned on the played notes.
    std::vector<double> pull (chart.size(), std::nan (""));
    if (playedValid)
        for (const auto& n : *playedNotes)
            if (n.sounds && n.chartIndex < pull.size())
                pull[n.chartIndex] = n.cents - n.resonance;
    table.bestSlide = playedValid ? whistle::bestSlide (geometry, lowest, &pull) : whistle::bestSlide (geometry, lowest);
    auto atBest = geometry;
    whistle::setSlide (atBest, table.bestSlide);

    double playedSum = 0.0, slideSum = 0.0, weights = 0.0;
    for (size_t i = 0; i < chart.size(); ++i)
    {
        const auto& c = chart[i];
        // The pattern for the holes in use, grouped as thumb | left hand | right hand.
        const auto holes = c.holePattern (geometry);
        std::string pattern;
        for (int h = 0; h < whistle::kNumHoles; ++h)
        {
            if (! geometry.holes[(size_t) h].enabled)
                continue;
            if ((h == 1 && geometry.holes[0].enabled) || h == 4)
                pattern += '|';
            pattern += (holes[(size_t) h] == '-' ? 'o' : holes[(size_t) h]);
        }
        Row e;
        e.note = noteName (lowest * std::pow (2.0, c.semitones / 12.0), geometry.referenceA);
        e.pattern = pattern;
        e.cents = whistle::predictedCents (geometry, c, lowest);
        e.target = whistle::targetCents (geometry, c);
        const double weight = whistle::tuningWeight (geometry, c);
        e.weight = weight;  // As the tuner counts it (the keys' weights go up to 1; typed ones are shown as typed)
        e.weightSet = geometry.tuning.weights.count (whistle::fingeringId (c)) > 0;
        e.measuring = ! playedValid;
        e.counts = whistle::fingeringCounts (geometry, c);
        if (playedValid)
            for (const auto& n : *playedNotes)
                if (n.chartIndex == i && n.sounds)
                {
                    e.breath = n.pressure;
                    e.learnedBreath = n.learned;
                    e.estimatedBreath = n.estimated;
                }
        if (std::isfinite (pull[i]))
        {
            e.played = e.cents + pull[i];
            e.fromEqual = e.target + e.played;
            e.slideTuned = whistle::predictedCents (atBest, c, lowest) + pull[i];
            playedSum += weight * e.played * e.played;
            slideSum += weight * e.slideTuned * e.slideTuned;
            weights += weight;
        }
        table.rows.push_back (e);
    }

    if (! playedValid)
    {
        table.title = "Measuring the played pitch...";
        return table;
    }
    const double change = table.bestSlide - geometry.slide;
    const std::string move = std::abs (change) < 0.00005 ? std::string ("where it is") : (change > 0.0 ? "pull out " : "push in ") + mm (std::abs (change));
    const auto rms = [weights] (double sum) { return fixed (weights > 0.0 ? std::sqrt (sum / weights) : 0.0, 1); };
    table.title = "From the targets: played " + rms (playedSum) + " c rms (slide at " + mm (geometry.slide) + "). Slide tuned: " + move
                  + (std::abs (change) < 0.00005 || std::abs (geometry.slide) < 0.00005 ? std::string() : " (to " + mm (table.bestSlide) + ")")
                  + ", " + rms (slideSum) + " c rms" + silentText (whistle::silentNotes (geometry, *playedNotes));
    table.summary = "Tuning (Tuning workshop tab): played " + rms (playedSum) + " c rms, slide tuned " + rms (slideSum) + " c rms";
    return table;
}

std::vector<std::string> chordReport (const whistle::Geometry& geometry, double lowestHz, const std::vector<whistle::PlayedNote>* playedNotes)
{
    if (playedNotes == nullptr)
        return { "(once the played pitch is measured)" };
    const auto& chart = whistle::chartFor (geometry.chart);

    // Each note's played pitch from equal temperament, by its semitones above the lowest note (the first fingering
    // for it that counts): a chord is built upward from its root in the low octave, so G major's fifth is the high D.
    std::map<int, double> played;
    for (const auto& n : *playedNotes)
    {
        const auto& c = chart[n.chartIndex];
        const int semitones = (int) std::lround (c.semitones);
        if (n.sounds && whistle::fingeringCounts (geometry, c) && played.count (semitones) == 0)
            played[semitones] = whistle::targetCents (geometry, c) + n.cents;
    }

    std::vector<std::string> lines;
    for (int key = 0; key < 24; ++key)
    {
        if (geometry.tuning.keys[(size_t) key] <= 0.0)
            continue;
        const bool minor = key >= whistle::TuningGoal::kMinor;
        const int root = key % 12, third = root + (minor ? 3 : 4), fifth = root + 7;
        auto at = [&] (int semitones)
        {
            const auto found = played.find (semitones);
            return found != played.end() ? found->second : std::nan ("");
        };
        std::string line = keyName (key, lowestHz, geometry.referenceA);
        line.resize (std::max<size_t> (line.size(), 4), ' ');
        line += "  ";
        if (! std::isfinite (at (root)) || ! std::isfinite (at (third)) || ! std::isfinite (at (fifth)))
        {
            lines.push_back (line + "(a note of its chord isn't on this instrument)");
            continue;
        }
        const double thirdError = (minor ? 300.0 : 400.0) + at (third) - at (root) - (minor ? 315.641 : 386.314);
        const double fifthError = 700.0 + at (fifth) - at (root) - 701.955;
        lines.push_back (line + "third " + signedCents (thirdError) + ", fifth " + signedCents (fifthError));
    }
    if (lines.empty())
        return { "(no key counts)" };
    return lines;
}

std::string tunedReport (const whistle::Geometry& before, const whistle::Geometry& tuned, double hz, const std::vector<whistle::PlayedNote>& notes)
{
    // Say what changed besides the positions: hole sizes the tuner had to change, and the head.
    std::string changed;
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        const double was = before.holes[(size_t) i].diameter, now = tuned.holes[(size_t) i].diameter;
        if (tuned.holes[(size_t) i].enabled && std::abs (now - was) > 0.00005)
            changed += (changed.empty() ? "; " : ", ") + std::string (whistle::kHoleNames[(size_t) i]) + " " + fixed (was * 1000.0, 1) + " -> "
                       + fixed (now * 1000.0, 1) + " mm";
    }
    if (std::abs (tuned.headDiameter - before.headDiameter) > 0.00005 || std::abs (tuned.headLength - before.headLength) > 1.0e-9)
        changed += (changed.empty() ? "; " : ", ") + std::string ("head ") + fixed (tuned.headDiameter * 1000.0, 1) + " mm over "
                   + fixed (tuned.headLength * 1000.0, 0) + " mm";
    return "Tuned for " + noteName (hz, tuned.referenceA) + ": played " + fixed (whistle::playedRms (tuned, notes), 1) + " c rms"
           + silentText (whistle::silentNotes (tuned, notes)) + changed;
}

Tuner::Reading Tuner::update (Player& player, const whistle::Geometry& g, double lowestHz)
{
    // The latest 0.1 s and a bit (what measurePitch looks at), read up to 30 times a second.
    const double rate = player.getSampleRate();
    const int needed = (int) (0.1 * rate) + (int) (rate / 120.0) + 3;
    std::vector<float> fresh ((size_t) needed);
    const int got = player.readTunerSamples (fresh.data(), needed);
    if (got < 100)  // The model is resting (no sound) or audio isn't running
        history.clear();
    else
        history.insert (history.end(), fresh.begin(), fresh.begin() + got);
    if ((int) history.size() > needed)
        history.erase (history.begin(), history.end() - needed);

    double clarity = 0.0;
    const double hz = (int) history.size() == needed ? whistle::measurePitch (history, rate, clarity) : 0.0;
    heard = hz > 0.0 && clarity >= 0.8 ? hz : 0.0;
    if (heard <= 0.0)
        return {};

    Reading reading;
    const double exact = 69.0 + 12.0 * std::log2 (heard / g.referenceA);
    const int midi = (int) std::lround (exact);
    const int key = midiOf (lowestHz, g.referenceA);
    reading.hz = heard;
    reading.note = noteName (heard, g.referenceA);
    reading.centsFromEqual = (exact - midi) * 100.0;
    reading.targetCents = whistle::temperamentCents (g.tuning)[(size_t) (((midi - key) % 12 + 12) % 12)];
    return reading;
}

void BreathLearner::start()
{
    samples.clear();
    lastFingering.clear();
    statusText = "Listening: play each note, holding it steady for a moment";
}

void BreathLearner::sample (const Player& player, const whistle::Geometry& g, double lowest, const std::vector<whistle::PlayedNote>* playedNotes,
                            double heardHz)
{
    // A note is heard when it sounds (the model's output above -54 dBFS), the holes the chart uses match a fingering
    // (each hole taken as open or covered, whichever it is nearer: a finger hovering over a lifted hole reads part
    // covered), and the breath stays within 15 % from one look to the next (30 a second): steady notes, not attacks
    // or changes. Which of that fingering's notes (registers) it is comes from the pitch heard: the nearest, however
    // out of tune (up to 3.5 semitones off), since it is the breath that is being learned. Without a clear pitch, from
    // where the breath lies among the pressures the model plays each of the notes at from rest (measured for the
    // table): the nearest, up to 1.6 x beyond its ends, since a note already sounding holds on past them.
    const float pressure = player.shownPressure.load(), level = player.whistleModel.level.load();
    const auto& chart = whistle::chartFor (g.chart);
    auto nameOf = [&] (const whistle::ChartFingering& c) { return noteName (lowest * std::pow (2.0, c.semitones / 12.0), g.referenceA); };

    std::string now;
    int best = -1;
    if (playedNotes == nullptr)
        now = "waiting for the played pitch to be measured";
    else if (pressure < 20.0f || level < 0.002f)
        now = "no note";
    else
    {
        const bool byPitch = heardHz > 0.0;
        double bestDistance = byPitch ? 350.0 : std::log (1.6);
        bool fingeringFound = false;
        for (const auto& n : *playedNotes)
        {
            const auto& c = chart[n.chartIndex];
            const auto holes = c.holePattern (g);
            bool matches = byPitch || n.sounds;
            for (int h = 0; h < whistle::kNumHoles && matches; ++h)
            {
                if (! g.holes[(size_t) h].enabled || holes[(size_t) h] == '-')
                    continue;
                const float open = player.shownOpenness[(size_t) h].load();
                const char want = holes[(size_t) h];
                matches = want == 'x' ? open < 0.5f : want == 'o' ? open >= 0.5f : std::abs (open - (want == 'h' ? 0.5f : 0.25f)) < 0.25f;
            }
            if (! matches)
                continue;
            fingeringFound = true;
            // How far the pitch heard is from this note (cents), or the breath outside the pressures it sounds at
            // from rest (0 inside).
            const double distance = byPitch ? std::abs (1200.0 * std::log2 (heardHz / (lowest * std::pow (2.0, c.semitones / 12.0))))
                                  : pressure < n.lowPa ? std::log (n.lowPa / pressure)
                                  : pressure > n.highPa ? std::log (pressure / n.highPa) : 0.0;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = (int) n.chartIndex;
            }
        }
        now = best >= 0 ? nameOf (chart[(size_t) best]) + " at " + std::to_string ((int) std::lround (pressure)) + " Pa"
            : fingeringFound ? (byPitch ? "the pitch " + noteName (heardHz, g.referenceA) + " is far from that fingering's notes"
                                        : "breath " + std::to_string ((int) std::lround (pressure)) + " Pa is far from that fingering's notes")
                             : std::string ("the holes match no fingering of the chart");
    }

    if (best >= 0)
    {
        const auto id = whistle::fingeringId (chart[(size_t) best]);
        if (id == lastFingering && std::abs (pressure / lastPressure - 1.0f) < 0.15f)
        {
            auto& s = samples[id];
            if (s.size() < 5000)
                s.push_back (pressure);
        }
        lastFingering = id;
        lastPressure = pressure;
    }
    else
    {
        lastFingering.clear();
    }

    // Which notes have been heard long enough (a third of a second), and what is heard now.
    std::vector<std::string> heard;
    for (const auto& c : chart)
        if (const auto found = samples.find (whistle::fingeringId (c)); found != samples.end() && found->second.size() >= 10)
            if (const auto name = nameOf (c); std::find (heard.begin(), heard.end(), name) == heard.end())
                heard.push_back (name);
    statusText = "Now: " + now + ". Heard: " + (heard.empty() ? std::string ("nothing yet") : join (heard, " "));
}

int BreathLearner::finish (whistle::TuningGoal& goal)
{
    int kept = 0;
    for (auto& [id, s] : samples)
    {
        if (s.size() < 10)
            continue;  // Under a third of a second: not a held note
        std::nth_element (s.begin(), s.begin() + (long) s.size() / 2, s.end());
        goal.learnedBreath[id] = s[s.size() / 2];
        ++kept;
    }
    samples.clear();
    if (kept > 0)
    {
        goal.useLearnedBreath = true;
        statusText = "Learned " + std::to_string (kept) + " fingerings (" + std::to_string ((int) goal.learnedBreath.size()) + " in all)";
    }
    else
    {
        statusText = "Nothing heard long enough: hold each note steady for a moment";
    }
    return kept;
}

} // namespace workshop
