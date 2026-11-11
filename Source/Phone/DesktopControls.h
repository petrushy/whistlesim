// What the iPhone screens use of the app: they drive the desktop pages' own controls (the preset and room lists,
// the reverb sliders, Simulate WARBL, Record...) instead of the engine directly, so a change made on the phone
// does exactly what it does on the desktop, and is saved the same way. The pages keep running behind the phone
// screens (connection, stream renewal, recording), whichever screen is shown. Friend of MonitorPage and
// InstrumentPage.

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
    juce::ToggleButton& crossFingerings() { return instrument().crossButton; }
    juce::ToggleButton& bell() { return instrument().bellButton; }
    juce::ToggleButton& holeUsed (int hole) { return *instrument().holeToggles[(size_t) hole]; }
    void tuneHoles() { instrument().tuneButton.onClick(); }
    juce::String instrumentStatus() { return instrument().tuneStatus.getText(); }
    juce::String instrumentName() { return instrument().geometry.name; }
    void savePreset (const juce::String& name) { instrument().savePresetNamed (name); }
    bool canDeletePreset() { return instrument().deleteButton.isEnabled(); }
    void deletePreset() { instrument().deletePreset(); }
    void exportInstrument() { instrument().saveToFile(); }
    void importInstrument() { instrument().loadFromFile(); }
    const std::vector<TuningTable::Entry>& tuningTable() { return instrument().tuningTable.getEntries(); }

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

    MainComponent& main;
};
