// The "Instrument" tab: the whistle's geometry and voicing, a drawing of it with the live hole openings,
// the resonances its geometry predicts for each fingering, and playing from the screen.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "HelpDot.h"
#include "Whistle/Acoustics.h"
#include "Whistle/InstrumentFile.h"

// Front view of the whistle to scale, upright (mouthpiece at the top). Holes show how open they are; the thumb hole,
// on the back, is drawn beside the tube. In manual play, clicking a hole covers or uncovers it, and right-clicking
// half-covers it.
class WhistleView : public juce::Component
{
public:
    explicit WhistleView (AudioEngine&);
    void setGeometry (const whistle::Geometry& g) { geometry = g; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> holeBounds (int hole) const;
    juce::Rectangle<float> footBounds() const;
    float yFor (double metres) const;  // Upright: distance from the window down the tube, in px
    float scale() const;               // px per metre, the same along and across the tube
    float axisX() const;               // The tube's centre line

    AudioEngine& audio;
    whistle::Geometry geometry;
};

// The default look, with the sliders' value boxes' text as big as the box allows (the settings scale with the
// window).
class ScaledValueLook : public juce::LookAndFeel_V4
{
public:
    juce::Font getLabelFont (juce::Label& label) override { return juce::FontOptions (juce::jmax (12.0f, label.getHeight() * 0.62f)); }
};

class InstrumentPage : public juce::Component, private juce::Timer
{
public:
    explicit InstrumentPage (AudioEngine&);
    ~InstrumentPage() override;

    void loadPreset (int index);

    // The whole design (geometry and voicing), e.g. for saving the app's state.
    whistle::Instrument getInstrument() const;
    void setInstrument (const whistle::Instrument&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // For the Tuning workshop tab, which tunes this instrument.
    const whistle::Geometry& getGeometry() const { return geometry; }
    void applyGeometry (const whistle::Geometry&);  // Takes the whole geometry, as the sliders would
    double lowestNoteHz() const;                    // The key and octave chosen, at the reference A
    std::function<void()> onInstrumentChanged;      // Geometry, voicing, key or reference changed
    std::function<void()> onNameChanged;            // Saved or exported under a new name (nothing else changed)
    std::function<void (const juce::String&)> onChartHolesChanged;  // A new chart opened or plugged holes (which)
    void setTuningSummary (const juce::String&);    // One line from the Tuning workshop, shown under the drawing
    // The key, octave, reference A and fingering chart: what the tuner aims for (the sound never uses them), shown on
    // the Tuning workshop tab. They stay this page's (the phone screens and Play scale use them).
    void showTuningControlsIn (juce::Component& parent);
    void layOutTuningControls (juce::Rectangle<int> row, juce::Rectangle<int> chartRow);

    // How much bigger than the smallest (24 px rows) this page draws its settings in a page of this size: the other
    // tabs are scaled by the same, so all text is the same size.
    float textScaleFor (juce::Rectangle<int> page) const;

private:

    enum class RowKind { normal, holePosition, holeDiameter };  // A hole's two sliders share one line.

    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<HelpDot> help;  // Explains the setting; none on a hole's diameter (its position row has it)
        RowKind kind = RowKind::normal;
    };

    // A labelled slider. `get`/`set` read and write the value in the slider's units.
    juce::Slider& addRow (juce::String name, double min, double max, double step, juce::String unit,
                          std::function<double()> get, std::function<void (double)> set, bool changesGeometry = true,
                          RowKind kind = RowKind::normal);
    void addHeading (juce::String name);

    struct SettingsLayout
    {
        int width = 0, columns = 1, columnWidth = 0, rowHeight = 24;
        size_t split = 0;  // leftColumn index where the second column starts
    };
    SettingsLayout settingsLayout (juce::Rectangle<int> settingsArea) const;
    static juce::Rectangle<int> settingsArea (juce::Rectangle<int> page);

    void geometryChanged();
    void voicingChanged();  // The played pitch changes with the voicing too
    void refreshSliders();
    void timerCallback() override;

    ScaledValueLook valueLook;  // Before the sliders that use it, so it outlives them
    AudioEngine& audio;
    whistle::Geometry geometry;

    juce::ComboBox presetBox, materialBox, keyBox, octaveBox;
    juce::Slider referenceBox;  // Reference A (Hz)
    juce::Label referenceLabel { {}, "A =" };
    juce::TextButton savePresetButton { "Save preset..." }, deleteButton { "Delete" },
        exportButton { "Export..." }, importButton { "Import..." };
    std::unique_ptr<juce::FileChooser> fileChooser;
    void saveToFile();
    void loadFromFile();

    // The player's own presets: .whistle files in the app's Presets folder, listed after the built-in ones.
    static constexpr int kUserPresetId = 1000;  // presetBox id of the first user preset
    std::vector<juce::File> userPresets;
    static juce::File userPresetFolder();
    void refreshPresetList();
    void selectPresetNamed (const juce::String&);
    void loadUserPreset (int index);
    void savePreset();                              // Asks for a name, then savePresetNamed
    void savePresetNamed (const juce::String& name);  // Replaces a preset of the player's with that name
    void deletePreset();                             // Asks first, then deleteUserPreset
    void deleteUserPreset (const juce::File&);       // Deletes it and goes on to the next preset
    juce::File selectedUserPreset() const;           // The player's preset selected in the list, if one is
    juce::ToggleButton bellButton { "Bell   (the bell sensor closes the open foot)" };
    HelpDot bellHelp { "Bell", [this] { return bellButton.getTooltip(); } };
    juce::ComboBox chartBox;
    std::array<std::unique_ptr<juce::ToggleButton>, whistle::kNumHoles> holeToggles;
    std::array<size_t, whistle::kNumHoles> holeRow {};  // Index in rows of each hole's position slider.
    void showKeyOfInstrument();
    void setHoleEnabled (int hole, bool enabled);
    juce::Label tuneStatus;

    std::vector<Row> rows;
    std::vector<std::function<void()>> refreshers;  // Read each slider's value back from the geometry.
    std::vector<std::unique_ptr<juce::Label>> headings;
    std::vector<juce::Component*> leftColumn;  // In layout order, inside settingsContent.
    juce::Component settingsContent;           // The rows and headings
    juce::Viewport settingsView;               // Scrolls them

    WhistleView whistleView { audio };
    juce::ToggleButton enableButton { "Sound on" }, manualButton { "Play from screen" };
    juce::TextButton scaleButton { "Play scale" };
    juce::Slider manualBreath;
    juce::Label manualBreathLabel { {}, "Breath" }, liveLabel, tuningSummary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPage)
};
