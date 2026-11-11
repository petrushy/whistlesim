// The iPhone app's screens, with a tab bar at the bottom:
//   Play     the front page: connection, instrument (preset), the live whistle, room and reverb, scale, record
//   Instrument  presets (save, export, import), key and tuning, the tuning table, and every dimension and voicing
//            setting in folding sections
//   Room     the room (reverb) and its settings
//   Setup    WARBL (connection, Bluetooth, simulator), sound output (latency, buffer), About
//   Details  the desktop pages, for fine-tuning and diagnostics (larger with the phone turned sideways)
// The desktop window (MainComponent) lives on behind them: it owns the engine, runs the connection and keeps the
// settings, and the phone screens drive its controls (see DesktopControls). So the sound is the desktop app's.
// On a Mac, `--phone` shows this in a phone-sized window, for trying layouts.

#pragma once

#include "../MainComponent.h"
#include "DesktopControls.h"
#include "PhoneStyle.h"
#include "PhoneWidgets.h"

class PhoneUI : public juce::Component, private juce::Timer
{
public:
    enum Tab
    {
        play,
        instrument,
        room,
        setup,
        details,
        numTabs
    };

    explicit PhoneUI (std::unique_ptr<MainComponent>);
    ~PhoneUI() override;

    MainComponent& getMain() { return *main; }
    void showTab (int tab);

    // Shows a sheet over everything until it dismisses itself.
    void present (std::unique_ptr<phone::Sheet>);
    void showAbout();
    void showPresetList();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TabBar;
    class PlayScreen;
    class InstrumentScreen;
    class RoomScreen;
    class SetupScreen;
    class DetailsScreen;

    struct Screen : public juce::Component
    {
        virtual void refresh() {}  // About 30 times a second while shown
    };

    void timerCallback() override;
    juce::BorderSize<int> safeArea() const;

    phone::LookAndFeel lookAndFeel;          // Outlives everything that uses it
    juce::LookAndFeel_V4 desktopLookAndFeel;  // The desktop pages keep their own look
    std::unique_ptr<MainComponent> main;
    DesktopControls controls { *main };
    std::array<std::unique_ptr<Screen>, numTabs> screens;
    std::unique_ptr<TabBar> tabBar;
    int currentTab = play;
    std::unique_ptr<phone::Sheet> sheet;
    std::unique_ptr<juce::Component> overlay;  // About

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhoneUI)
};
