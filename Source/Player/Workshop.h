// The Tuning workshop's working parts, without JUCE, shared by the desktop's Workshop tab and the iPhone app: the
// tuning table (each fingering's target, weight, breath, tube resonance and played pitch), the chords as played,
// learning the player's breath from WARBL, the tuner's reading of the model's sound, and the tuner's report.
// The heavy work is the engine's (whistle::measurePlayed, tuneToPlayed, bestSlide); this arranges and words it.

#pragma once

#include "Player.h"
#include "Whistle/PlayedTuning.h"

#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace workshop
{

// Notes and numbers as the workshop words them.
int midiOf (double hz, double referenceA);
std::string noteName (double hz, double referenceA);    // "D5"
std::string signedCents (double cents);                  // "+3 c"
std::string silentText (int silent);                     // After an rms: " (2 notes don't sound)", or empty
std::string mm (double metres);                          // "1.5 mm"
std::string fixed (double value, int decimals);          // "7.6"

// The keys the workshop offers, by their tonic's semitones above the lowest note (minor ones + TuningGoal::kMinor):
// the home key, its fourth and fifth, the flat seventh; and their relative minors.
constexpr std::array<int, 8> kKeys { 0, 5, 7, 10, whistle::TuningGoal::kMinor + 9, whistle::TuningGoal::kMinor + 2,
                                     whistle::TuningGoal::kMinor + 4, whistle::TuningGoal::kMinor + 7 };
std::string keyName (int key, double lowestHz, double referenceA);  // "G", "Em"

// The lowest note the instrument is tuned in, as the desktop's Instrument tab finds it: the key saved with it, or its
// resonance with every hole closed; in octave 5 (high whistles) or 4 (low ones).
double lowestNoteHz (const whistle::Geometry&);

// One row of the tuning table: a fingering of the chart.
struct Row
{
    std::string note;
    std::string pattern;                // The holes in use, as thumb | left hand | right hand
    double cents = 0.0;                 // The tube's resonance, from the target, with the slide where it is
    double fromEqual = std::nan ("");   // Played, from the equal-tempered note (target + played)
    double played = std::nan ("");      // Played, from the target (NaN: not measured yet, or doesn't sound as this note)
    double slideTuned = std::nan ("");  // Played, with the slide tuned
    double target = 0.0;                // The target, from equal temperament
    double weight = 0.0;                // How much the tuner counts it (0-1)
    double breath = 0.0;                // Pa: the model's pressure it is tuned (and measured) at; 0 if not measured
    bool weightSet = false;             // The player's own weight, not the keys'
    bool learnedBreath = false;         // ...the player's learned breath
    bool estimatedBreath = false;       // ...estimated from the notes learned around it
    bool measuring = false;             // The played pitch is being measured
    bool counts = true;                 // Part of the tuning
};

struct Table
{
    std::vector<Row> rows;
    double bestSlide = 0.0;  // m: where the slide plays the notes in tune on the whole
    std::string title;       // "From the targets: played 7.6 c rms ...", or "Measuring the played pitch..."
    std::string summary;     // "Tuning (Tuning workshop tab): played ... c rms, slide tuned ... c rms"; empty until measured
};

// The table for an instrument; `played` is the last measurement of its played pitch (whistle::measurePlayed), or null
// while it is being measured.
Table buildTable (const whistle::Geometry&, double lowestHz, const std::vector<whistle::PlayedNote>* played);

// Each key that counts: its tonic chord's third and fifth as played, from pure. One line per key (or one message).
std::vector<std::string> chordReport (const whistle::Geometry&, double lowestHz, const std::vector<whistle::PlayedNote>* played);

// What Tune holes did, for the status line: the played rms, notes that don't sound, and sizes and the head changed.
std::string tunedReport (const whistle::Geometry& before, const whistle::Geometry& tuned, double lowestHz,
                         const std::vector<whistle::PlayedNote>& notes);

// The pitch of the model's sound (before the room), from the Player's tuner samples, about 30 times a second.
class Tuner
{
public:
    struct Reading
    {
        double hz = 0.0;  // 0: no clear pitch
        std::string note;
        double centsFromEqual = 0.0;  // From the nearest equal-tempered note at the reference A
        double targetCents = 0.0;     // The tuning's target for that note, from equal
    };

    // Reads the samples since the last call (the Player must have tunerListening on).
    Reading update (Player&, const whistle::Geometry&, double lowestHz);
    double heardHz() const { return heard; }  // The last clear pitch, or 0
    void clear() { history.clear(); heard = 0.0; }

private:
    std::vector<float> history;
    double heard = 0.0;
};

// Learning the player's breath: while on, each steady note played on WARBL is heard (by its fingering and its pitch)
// with the breath given it; finish keeps each fingering's median breath.
class BreathLearner
{
public:
    void start();
    // About 30 times a second: what is heard now. Needs the played-pitch measurement (`played`) to tell the notes apart.
    void sample (const Player&, const whistle::Geometry&, double lowestHz, const std::vector<whistle::PlayedNote>* played, double heardHz);
    const std::string& status() const { return statusText; }
    // Into the goal: each fingering heard long enough (a third of a second). Returns how many.
    int finish (whistle::TuningGoal&);

private:
    std::map<std::string, std::vector<float>> samples;  // Pa, by fingeringId
    std::string lastFingering;
    float lastPressure = 0.0f;
    std::string statusText;
};

} // namespace workshop
