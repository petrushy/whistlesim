// The app window: owns the stream receiver and audio engine, with a tab per page.

#pragma once

#include "InstrumentPage.h"
#include "WorkshopPage.h"
#include "ModelPage.h"
#include "MonitorPage.h"
#include "SensorsPage.h"

// A tab page drawn bigger by a factor: laid out in a correspondingly smaller area, then scaled up. Owns the page.
class ScaledPage : public juce::Component
{
public:
    ScaledPage (juce::Component* p, std::function<float (juce::Rectangle<int>)> scaleFor) : page (p), scale (std::move (scaleFor))
    {
        addAndMakeVisible (*page);
    }

    void resized() override
    {
        const float s = juce::jmax (1.0f, scale (getLocalBounds()));
        page->setTransform (juce::AffineTransform::scale (s));
        page->setBounds (0, 0, juce::roundToInt (getWidth() / s), juce::roundToInt (getHeight() / s));
    }

    // The page sees the tab shown and hidden (it measures only while shown).
    void visibilityChanged() override { page->setVisible (isVisible()); }

private:
    std::unique_ptr<juce::Component> page;
    std::function<float (juce::Rectangle<int>)> scale;
};

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    MonitorPage& getMonitor() { return *monitor; }
    InstrumentPage& getInstrumentPage() const { return *instrument; }
    WorkshopPage& getWorkshopPage() const { return *workshop; }
    SensorsPage& getSensorsPage() const { return *sensors; }
    StreamReceiver& getReceiver() { return receiver; }
    void showMonitor() { tabs.setCurrentTabIndex (0); }
    void showInstrument() { tabs.setCurrentTabIndex (1); }
    void showWorkshop() { tabs.setCurrentTabIndex (2); }
    void showSensors() { tabs.setCurrentTabIndex (3); }
    void showModel() { tabs.setCurrentTabIndex (4); }
    void loadPreset (int index) { instrument->loadPreset (index); }
    void showAbout();
    juce::Component* getAboutPanel() { return aboutWindow != nullptr ? aboutWindow->getContentComponent() : nullptr; }
    void setSaveStateOnExit (bool shouldSave) { saveOnExit = shouldSave; }
    AudioEngine& getAudio() { return audio; }

    // The app went to the background (iOS) or came back. In the background it saves its state, since it may be
    // ended without warning, and it keeps playing only while a real WARBL is connected (playing with the screen
    // locked); otherwise it stops the sound, so the model doesn't run on battery for nobody.
    void appSuspended();
    void appResumed();

private:
    // Restores and saves the instrument, sensor response, reverb and audio device between runs.
    void restoreState();
    void saveState();

    juce::ApplicationProperties properties;
    StreamReceiver receiver;
    AudioEngine audio { receiver };
    InstrumentPage* instrument = nullptr;  // Owned by tabs.
    WorkshopPage* workshop = nullptr;      // Owned by tabs.
    SensorsPage* sensors = nullptr;        // Owned by tabs.
    bool saveOnExit = true;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    MonitorPage* monitor = nullptr;  // Owned by tabs.
    juce::TextButton aboutButton { "About" };
    juce::Component::SafePointer<juce::DialogWindow> aboutWindow;  // Deletes itself when closed.
    std::unique_ptr<juce::Component> overlay;  // iOS: the About panel, shown over the app (see OverlayPanel.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
