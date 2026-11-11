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
