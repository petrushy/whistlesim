// Tuning to the played pitch. A whistle doesn't sound at its tube's resonance: the jet pulls each note off it, by
// an amount that depends on the note and the breath (on a measured Golden 0 to -31 cents). The tuner in Acoustics
// places the resonances; this plays the time-domain model to find each note's pull, aims the resonances so the
// notes play in tune, and repeats, since the pull hardly changes when the holes move a little.
//
// The breath: by default each note at the geometric middle of the pressures it sounds at. Played with WARBL, a
// measured Golden's notes were blown there (D5 sounds from ~150 to ~310 Pa, middle 216, played at 210-240; G5 365
// vs 380; D6 648 vs 690), so it stands for a player without assuming any instrument's own pressures. The tuning
// goal can move it within each octave's ranges (TuningGoal::breathPosition), or use the player's own breath,
// learned by playing (TuningGoal::learnedBreath); a note that wasn't heard gets its breath estimated from the
// notes that were (playerBreath).


#pragma once

#include "Acoustics.h"

#include <vector>

namespace whistle
{

struct PlayedNote
{
    size_t chartIndex = 0;   // In chartFor (the geometry's chart)
    bool sounds = false;     // Plays as this note at some pressure
    double lowPa = 0.0, highPa = 0.0, pressure = 0.0;  // The range it sounds in, and the breath it's measured at
    bool learned = false;    // Measured at the player's learned breath (TuningGoal::learnedBreath)
    bool estimated = false;  // ...estimated from the notes learned around it (this fingering wasn't heard)
    double cents = 0.0;      // Played, from the note
    double resonance = 0.0;  // The geometry's resonance, from the note (as predictedCents)
};

// Plays every chart note (all at once, on several threads): takes a few seconds; call it off the audio and
// message threads.
std::vector<PlayedNote> measurePlayed (const Geometry&, const Voicing&, double lowestHz);

// Weighted as the tuner weights notes (tuningWeight), over the notes that sound.
double playedRms (const Geometry&, const std::vector<PlayedNote>&);

// How many of the notes the tuner counts don't sound (as their note) at all.
int silentNotes (const Geometry&, const std::vector<PlayedNote>&);

// The player's breath for a fingering, from the learned breath (Pa, as the model gets it), or 0 if there is none:
// its own if it was heard; otherwise, with two or more notes heard, interpolated by pitch between the nearest
// heard below and above (log pressure, linear in semitones: breath rises in proportion up the scale), or beyond
// them carried on from the nearest with the trend of all heard notes (least squares, held between flat and
// doubling every 3.5 semitones). `estimated` says which.
double playerBreath (const Geometry&, const ChartFingering&, bool& estimated);

// Tunes as tuneToScale, then aims the resonances so the notes play in tune (`passes` rounds of playing and
// refitting), keeping the best round. The holes can't move a fingering's second register against its first;
// with `shapeHead` the head section's diameter is fitted too (an instrument without one gets a 30 mm head).
// The instrument as it was is kept if none of that plays better. `played` (optional) receives the measurement of
// the result. Returns false if the first tuning didn't converge, even with other hole sizes.
bool tuneToPlayed (Geometry&, const Voicing&, double lowestHz, bool tuneCrossFingerings, bool shapeHead = true, int passes = 3,
                   std::vector<PlayedNote>* played = nullptr);

} // namespace whistle
