// What the iPhone screens use of the app: they drive the desktop pages' own controls (the preset and room lists,
// the reverb sliders, Simulate WARBL, Record...) instead of the engine directly, so a change made on the phone
// does exactly what it does on the desktop, and is saved the same way. The pages keep running behind the phone
// screens (connection, stream renewal, recording), whichever screen is shown. Friend of MonitorPage,
// InstrumentPage, WorkshopPage and SensorsPage.

#pragma once

#include "../MainComponent.h"

#if JUCE_IOS || JUCE_ANDROID
 #include <juce_audio_utils/juce_audio_utils.h>
#endif

class DesktopControls
{
public:
    explicit DesktopControls (MainComponent& m) : main (m) {}

    AudioEngine& audio() { return main.getAudio(); }
    StreamReceiver& receiver() { return main.getReceiver(); }
    MainComponent& window() { return main; }

    // Instrument
    juce::ComboBox& presetBox() { return instrument().presetBox; }
    whistle::Geometry geometry() const { return main.getInstrumentPage().getInstrument().geometry; }

    // Instrument settings (the desktop's Instrument tab)
    juce::ComboBox& keyBox() { return instrument().keyBox; }
    juce::ComboBox& octaveBox() { return instrument().octaveBox; }
    juce::ComboBox& chartBox() { return instrument().chartBox; }
    juce::ComboBox& materialBox() { return instrument().materialBox; }
    juce::Slider& pitchStandard() { return instrument().referenceBox; }
    juce::ToggleButton& crossFingerings() { return workshop().crossButton; }
    juce::ToggleButton& bell() { return instrument().bellButton; }
    juce::ToggleButton& holeUsed (int hole) { return *instrument().holeToggles[(size_t) hole]; }
    void tuneHoles() { workshop().tuneButton.onClick(); }
    juce::String instrumentStatus()
    {
        // The Instrument tab's (files, presets) and the Tuning workshop's (the tuner).
        const auto files = instrument().tuneStatus.getText(), tuner = workshop().status.getText();
        return files.isEmpty() ? tuner : tuner.isEmpty() ? files : files + "\n" + tuner;
    }
    juce::String instrumentName() { return instrument().geometry.name; }
    void savePreset (const juce::String& name) { instrument().savePresetNamed (name); }
    bool canDeletePreset() { return instrument().selectedUserPreset() != juce::File(); }
    juce::String presetToDelete() { return instrument().selectedUserPreset().getFileNameWithoutExtension(); }
    void deletePreset() { instrument().deleteUserPreset (instrument().selectedUserPreset()); }  // Without asking
    void exportInstrument() { instrument().saveToFile(); }
    void importInstrument() { instrument().loadFromFile(); }
    const std::vector<TuningTable::Entry>& tuningTable() { return workshop().table.getEntries(); }

    // The Instrument tab's sliders, grouped under its headings, in its order. A hole's two sliders (position and
    // diameter) carry the hole's index.
    struct Setting
    {
        juce::String name;
        juce::Slider* slider = nullptr;
        int hole = -1;
        bool diameter = false;
    };
    struct SettingGroup
    {
        juce::String heading;
        std::vector<Setting> settings;
    };
    std::vector<SettingGroup> instrumentSettings()
    {
        auto& page = instrument();
        std::vector<SettingGroup> groups;
        int hole = -1;
        for (auto* item : page.leftColumn)
        {
            const auto heading = std::find_if (page.headings.begin(), page.headings.end(), [item] (const auto& h) { return h.get() == item; });
            if (heading != page.headings.end())
            {
                groups.push_back ({ (*heading)->getText(), {} });
                continue;
            }
            const auto row = std::find_if (page.rows.begin(), page.rows.end(), [item] (const auto& r) { return r.label.get() == item; });
            if (row == page.rows.end() || groups.empty())
                continue;
            if (row->kind == InstrumentPage::RowKind::holePosition)
                ++hole;
            const bool isHole = row->kind != InstrumentPage::RowKind::normal;
            groups.back().settings.push_back ({ row->label->getText(), row->slider.get(), isHole ? hole : -1,
                                                row->kind == InstrumentPage::RowKind::holeDiameter });
        }
        return groups;
    }

    // The Tuning workshop (the desktop's tab): its controls, its texts, and the table's row actions.
    void setTuningShown (bool shown) { workshop().setShownElsewhere (shown); }
    juce::ComboBox& temperamentBox() { return workshop().temperamentBox; }
    juce::Slider& towardEqual() { return workshop().towardEqual; }
    std::vector<Setting> keyWeights()
    {
        std::vector<Setting> keys;
        for (auto& row : workshop().keyRows)
            keys.push_back ({ row.name.getText(), &row.weight });
        return keys;
    }
    juce::ComboBox& topHoleBox() { return workshop().topHoleBox; }
    juce::ToggleButton& headShaping() { return workshop().headButton; }
    void tuneSlide() { workshop().bestSlideButton.triggerClick(); }
    bool isTuning() { return workshop().tuning; }
    void toggleLearning() { workshop().learnButton.triggerClick(); }
    bool isLearning() { return workshop().learning; }
    void forgetBreath() { workshop().forgetButton.triggerClick(); }
    bool hasLearnedBreath() { return ! geometry().tuning.learnedBreath.empty(); }
    juce::ToggleButton& useLearnedBreath() { return workshop().useLearnedButton; }
    juce::Slider& lowOctaveBreath() { return workshop().lowPosition; }
    juce::Slider& upperOctaveBreath() { return workshop().highPosition; }
    juce::ToggleButton& tuner() { return workshop().tunerButton; }
    juce::String tuningTitle() { return workshop().title.getText(); }
    juce::String tuningSummary() { return workshop().tableTitle.getText(); }
    juce::String tunerStatus() { return workshop().status.getText(); }
    juce::String learnStatus() { return workshop().learnStatus.getText(); }
    juce::String chords() { return workshop().chords.getText(); }
    juce::String tooltipOf (juce::Component& c)
    {
        auto* t = dynamic_cast<juce::TooltipClient*> (&c);
        return t != nullptr ? t->getTooltip() : juce::String();
    }
    void toggleFingering (size_t row) { if (workshop().table.onToggle) workshop().table.onToggle (row); }
    void setWeight (size_t row, double weight) { if (workshop().table.onWeightEdited) workshop().table.onWeightEdited (row, weight); }
    void setBreath (size_t row, double pa) { if (workshop().table.onBreathEdited) workshop().table.onBreathEdited (row, pa); }
    std::function<void (double, const juce::String&, double, double)>& onTunerReading() { return workshop().onTunerReading; }

    // Sensors (the desktop's Sensors tab): how breath pressure and tone-hole coverage become the model's breath and
    // hole openings. Each list in the tab's order, with its labels.
    std::vector<Setting> breathSettings() { return settingsOf (sensors().breathRows); }
    std::vector<Setting> holeSettings() { return settingsOf (sensors().holeRows); }
    std::vector<Setting> thumbSettings() { return settingsOf (sensors().thumbRows); }
    juce::ComboBox& holeShape() { return sensors().holeShape; }
    juce::ToggleButton& holdHoleJumps() { return sensors().holeJumpToggle; }
    juce::ToggleButton& thumbOwnResponse() { return sensors().thumbToggle; }
    juce::String thumbNow() { return sensors().thumbInfo.getText(); }
    juce::String thumbExplanation() { return sensors().thumbTitle.getTooltip(); }
    void breathDefaults() { sensors().breathDefaults.onClick(); }
    void holeDefaults() { sensors().holeDefaults.onClick(); }

    // Room
    juce::ComboBox& roomBox() { return monitor().roomBox; }
    juce::Slider& reverbLevel() { return monitor().wetSlider; }
    juce::Slider& preDelay() { return monitor().preDelaySlider; }
    juce::Slider& spread() { return monitor().spreadSlider; }
    void loadImpulseResponse() { monitor().loadIrButton.triggerClick(); }
    juce::String roomName() { return audio().reverb.getCurrentName(); }

    // WARBL
    juce::ToggleButton& simulate() { return monitor().simulateButton; }
    juce::ToggleButton& smoothBluetooth() { return monitor().smoothBluetoothButton; }
    void requestInfo() { receiver().requestInfo(); }
    juce::String deviceInfo() { return monitor().infoLabel.getText(); }
    static bool canPairBluetooth()
    {
       #if JUCE_IOS || JUCE_ANDROID
        return true;
       #else
        return false;  // macOS: Audio MIDI Setup
       #endif
    }
    static void pairBluetooth()
    {
       #if JUCE_IOS || JUCE_ANDROID
        juce::BluetoothMidiDevicePairingDialogue::open();
       #endif
    }

    // Playing and recording
    void toggleRecording() { monitor().toggleRecording(); }
    bool isRecording() { return audio().recorder.isRecording(); }
    double recordingSeconds() { return audio().recorder.getSeconds(); }
    // What the last recording did ("Saved ... in ..."), for a few seconds after it stopped; empty otherwise.
    juce::String recordingMessage()
    {
        return juce::Time::getMillisecondCounterHiRes() < monitor().recordingMessageUntilMs ? monitor().recordingMessage : juce::String();
    }

private:
    MonitorPage& monitor() { return main.getMonitor(); }
    InstrumentPage& instrument() { return main.getInstrumentPage(); }
    WorkshopPage& workshop() { return main.getWorkshopPage(); }
    SensorsPage& sensors() { return main.getSensorsPage(); }

    template <typename Rows>
    static std::vector<Setting> settingsOf (Rows& rows)
    {
        std::vector<Setting> settings;
        for (auto& row : rows)
            settings.push_back ({ row.label->getText(), row.slider.get() });
        return settings;
    }

    MainComponent& main;
};
