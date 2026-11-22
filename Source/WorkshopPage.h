// The "Tuning workshop" tab: tuning the instrument. Its fingering table (which fingerings count, each note's target, weight,
// tube resonance and played pitch), the tuning goal (temperament, the keys that matter), the tuner (Tune holes,
// Tune slide) and how pure the keys' chords play. The goal is part of the instrument (Geometry::tuning); the
// Instrument tab holds the instrument and this page changes it through InstrumentPage::applyGeometry.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "AudioEngine.h"
#include "InstrumentPage.h"
#include "Whistle/PlayedTuning.h"

// Every fingering of the chart, one row each. Clicking a row's Use box takes the fingering in or out of the tuning.
class TuningTable : public juce::Component
{
public:
    struct Entry
    {
        juce::String note;
        juce::String pattern;
        double cents = 0.0;                 // The tube's resonance, from the target, with the slide where it is
        double fromEqual = std::nan ("");   // Played, from the equal-tempered note (target + played)
        double played = std::nan ("");      // Played, from the target (NaN: not measured yet, or doesn't sound as this note)
        double slideTuned = std::nan ("");  // Played, with the slide tuned
        double target = 0.0;                // The target, from equal temperament
        double weight = 0.0;                // How much the tuner counts it (0-1)
        double breath = 0.0;                // Pa: the model's pressure it is tuned (and measured) at; 0 if not measured
        bool learnedBreath = false;         // ...the player's learned breath
        bool estimatedBreath = false;       // ...estimated from the notes learned around it
        bool measuring = false;             // The played pitch is being measured
        bool counts = true;                 // Part of the tuning
    };

    void setEntries (std::vector<Entry> e)
    {
        entries = std::move (e);
        repaint();
    }
    const std::vector<Entry>& getEntries() const { return entries; }
    std::function<void (size_t)> onToggle;  // A row's Use box was clicked

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int rowHeight() const;
    std::vector<Entry> entries;
};

class WorkshopPage : public juce::Component, private juce::Timer
{
public:
    WorkshopPage (AudioEngine&, InstrumentPage&);
    ~WorkshopPage() override;

    void instrumentChanged();  // From the Instrument tab: recompute the table, and measure again when shown
    void setLearning (bool);   // As the Learn my breath button (used by --learn test runs)

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    friend class DesktopControls;  // The iPhone screens use the tuner and the table

    void timerCallback() override;
    void rebuildTable();
    void measurePlayedInBackground();
    void tuneHoles();
    void setGoal (const whistle::TuningGoal&);  // Into the instrument, which then calls instrumentChanged
    void refreshGoalControls();
    juce::String chordReport() const;
    juce::String keyName (int key) const;  // e.g. "G major" for key 5 on a D whistle
    void sampleBreath();                   // While learning: the breath of the steady note being played, if any
    void stopLearning();                   // Keeps the median breath of each fingering heard long enough

    AudioEngine& audio;
    InstrumentPage& instrument;

    juce::Label title, goalHeading, keysHeading, tunerHeading, chordsHeading, chords, status, tableTitle;
    juce::ComboBox temperamentBox;
    std::array<juce::Label, 12> customCells;  // Custom tuning: cents from equal, by degree (editable)
    std::array<juce::Label, 12> customNames;
    juce::Slider towardEqual;
    juce::Label towardEqualLabel { {}, "Toward equal" };

    // The keys offered, by TuningGoal::keys index: on a D whistle D, G, A, C major and Bm, Em, F#m, Am.
    static constexpr int kKeys[] = { 0, 5, 7, 10, whistle::TuningGoal::kMinor + 9, whistle::TuningGoal::kMinor + 2,
                                     whistle::TuningGoal::kMinor + 4, whistle::TuningGoal::kMinor + 7 };
    struct KeyRow
    {
        juce::Label name;
        juce::Slider weight;
    };
    std::array<KeyRow, 8> keyRows;

    juce::TextButton tuneButton { "Tune holes" }, bestSlideButton { "Tune slide" };

    // The breath the notes are tuned at: learned from the player, or where in each note's range.
    juce::Label breathHeading, learnStatus, lowPositionLabel { {}, "Low octave" }, highPositionLabel { {}, "Upper octaves" };
    juce::TextButton learnButton { "Learn my breath" }, forgetButton { "Forget" };
    juce::ToggleButton useLearnedButton { "Tune at my learned breath" };
    juce::Slider lowPosition, highPosition;
    bool learning = false;
    std::map<std::string, std::vector<float>> breathSamples;  // Pa, by fingeringId, while learning
    std::string lastFingering;
    float lastPressure = 0.0f;
    juce::ToggleButton crossButton { "Also tune hole sizes for cross-fingerings and upper notes" };
    juce::ComboBox topHoleBox;  // Whistles: L1's size for C natural or for high D
    juce::ToggleButton headButton { "Also shape the head section, for the octaves (adds one if there is none)" };

    TuningTable table;
    double bestSlide = 0.0;  // m: the slide setting the table's Slide tuned column is for

    bool dirty = true;
    int countdown = 0;

    // The played pitch, measured by playing the model (about a second, on other threads) while this tab is shown:
    // for the instrument as it was when playedGeneration was last increased.
    std::vector<whistle::PlayedNote> playedNotes;
    bool playedValid = false, tuning = false;
    int playedGeneration = 0;

    // Last, so it stops (waiting for a running job) before the rest goes.
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    juce::ThreadPool worker { juce::ThreadPoolOptions().withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WorkshopPage)
};
