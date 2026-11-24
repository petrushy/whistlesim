// The "Tuning workshop" tab: tuning the instrument. Its fingering table (which fingerings count, each note's target, weight,
// tube resonance and played pitch), the tuning goal (temperament, the keys that matter), the tuner (Tune holes,
// Tune slide) and how pure the keys' chords play. The goal is part of the instrument (Geometry::tuning); the
// Instrument tab holds the instrument and this page changes it through InstrumentPage::applyGeometry.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "AudioEngine.h"
#include "InstrumentPage.h"
#include "Player/Workshop.h"
#include "Whistle/PlayedTuning.h"

// Every fingering of the chart, one row each. Clicking a row's Use box takes the fingering in or out of the tuning;
// clicking its Weight or Breath lets the player type their own.
class TuningTable : public juce::Component, public juce::SettableTooltipClient
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
        bool weightSet = false;             // The player's own weight, not the keys'
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
    // A row's breath was typed in (Pa; 0: cleared, back to estimated or the sliders' position).
    std::function<void (size_t, double)> onBreathEdited;
    // A row's weight was typed in (0-1; NaN: cleared, back to the keys' weight).
    std::function<void (size_t, double)> onWeightEdited;

    TuningTable();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    int rowHeight() const;
    int rowAt (juce::Point<int>, int column) const;  // The row whose cell in `column` is there, or -1
    juce::Rectangle<int> cellBounds (size_t row, int column) const;
    void edit (size_t row, int column);  // Opens the editor over a Weight or Breath cell
    std::vector<Entry> entries;
    juce::TextEditor editor;  // Over the cell being edited
    size_t editingRow = 0;
    int editingColumn = 0;
};

// An old-fashioned needle tuner: the note nearest the pitch heard, and the needle at its cents from equal
// temperament (+-50). The tuning's target for that note is marked on the scale, with a green zone around it.
class TunerView : public juce::Component
{
public:
    // hz 0: nothing heard. Called at the timer's rate; the needle eases toward the reading like a damped meter.
    void setReading (double hz, const juce::String& note, double centsFromEqual, double targetCents);
    void setActive (bool);
    void paint (juce::Graphics&) override;

private:
    bool active = false;
    double hz = 0.0, cents = 0.0, target = 0.0, needle = 0.0;
    juce::String note;
    int silentTicks = 1000;
};

class WorkshopPage : public juce::Component, private juce::Timer
{
public:
    WorkshopPage (AudioEngine&, InstrumentPage&);
    ~WorkshopPage() override;

    void instrumentChanged();  // From the Instrument tab: recompute the table, and measure again when shown
    void setLearning (bool);   // As the Learn my breath button (used by --learn test runs)
    void setTuner (bool on)    // As the tuner's button (used by --tuner test runs)
    {
        tunerButton.setToggleState (on, juce::sendNotification);
    }

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    bool active() const { return isShowing(); }
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
    static constexpr auto kKeys = workshop::kKeys;
    struct KeyRow
    {
        juce::Label name;
        juce::Slider weight;
    };
    std::array<KeyRow, 8> keyRows;

    juce::TextButton tuneButton { "Tune holes" }, bestSlideButton { "Tune slide" };

    // The breath the notes are tuned at: learned from the player, or where in each note's range.
    juce::Label breathHeading, learnStatus, lowPositionLabel { {}, "Low octave" }, highPositionLabel { {}, "Upper octaves" };
    juce::TextButton learnButton { "Learn my breath" }, forgetButton { "Forget" }, smoothButton { "Smooth" };
    juce::ToggleButton useLearnedButton { "Tune at my learned breath" };
    juce::Slider lowPosition, highPosition;
    bool learning = false;
    workshop::BreathLearner learner;
    juce::ToggleButton crossButton { "Also tune hole sizes for cross-fingerings and upper notes" };
    juce::ComboBox topHoleBox;  // Whistles: L1's size for C natural or for high D
    juce::ToggleButton headButton { "Also shape the head section, for the octaves (adds one if there is none)" };

    TuningTable table;

    // The tuner: listens to the model's sound (whatever is played: WARBL, the screen, Play scale).
    juce::ToggleButton tunerButton { "Tuner: listens to what is played" };
    TunerView tunerView;
    workshop::Tuner tuner;  // The model's pitch, from the latest samples
    double heardHz = 0.0;             // The pitch heard last (0: none), for the tuner and for learning the breath
    void listen();                    // Reads the sound and its pitch, while the tuner is on or learning
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
