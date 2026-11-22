#include "MainComponent.h"

#include "AboutPanel.h"
#include "OverlayPanel.h"
#include "ReverbFile.h"
#include "Player/SensorSettings.h"


MainComponent::MainComponent()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "WhistleSim";
    options.folderName = "WhistleSim";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (options);

    // The engine's settings are restored first, so the pages show them when they are made.
    restoreState();

    const juce::Colour background { 0xff1e1f22 };
    monitor = new MonitorPage (receiver, audio);
    instrument = new InstrumentPage (audio);
    monitor->recordingHeader = [this]
    {
        juce::StringArray lines;
        lines.add ("WhistleSim recording " + juce::Time::getCurrentTime().toISO8601 (true));
        lines.add ("instrument " + juce::String (whistle::toJson (instrument->getInstrument(), true)));
        lines.add ("sensors " + juce::String (sensorsToText (audio)));
        return lines;
    };
    // The other tabs scaled to the Instrument tab's text size (it sizes its own text to the window).
    auto scaled = [this] (juce::Component* page) { return new ScaledPage (page, [this] (juce::Rectangle<int> b) { return instrument->textScaleFor (b); }); };
    tabs.addTab ("Monitor", background, scaled (monitor), true);
    tabs.addTab ("Instrument", background, instrument, true);
    workshop = new WorkshopPage (audio, *instrument);
    tabs.addTab ("Tuning workshop", background, scaled (workshop), true);
    sensors = new SensorsPage (audio);
    tabs.addTab ("Sensors", background, scaled (sensors), true);
    tabs.addTab ("Model", background, scaled (new ModelPage (audio)), true);
    tabs.setTabBarDepth (30);
    addAndMakeVisible (tabs);
    aboutButton.onClick = [this] { showAbout(); };
    addAndMakeVisible (aboutButton);  // After the tabs: it sits in the empty end of their bar.

    if (auto* settings = properties.getUserSettings())
    {
        std::string error;
        if (auto saved = whistle::fromJson (settings->getValue ("instrument").toStdString(), error))
            instrument->setInstrument (*saved);
        tabs.setCurrentTabIndex (settings->getIntValue ("tab", 0));
    }

    setSize (1200, 910);
}

MainComponent::~MainComponent()
{
    if (aboutWindow != nullptr)
        delete aboutWindow.getComponent();
    if (saveOnExit)
        saveState();
    tabs.clearTabs();  // Pages go before the engine they use.
}

void MainComponent::restoreState()
{
    auto* settings = properties.getUserSettings();
    if (settings == nullptr)
        return;

    if (auto device = settings->getXmlValue ("audioDevice"))
        audio.deviceManager.initialise (0, 2, device.get(), true);

    sensorsFromText (settings->getValue ("sensors").toStdString(), audio);

    if (settings->containsKey ("smoothBluetooth"))
        audio.smoothBluetooth = settings->getBoolValue ("smoothBluetooth");
    if (settings->containsKey ("sourceSpread_deg"))
        audio.reverb.setSpread ((float) settings->getDoubleValue ("sourceSpread_deg"));
    const auto irFile = juce::File (settings->getValue ("reverbFile"));
    if (irFile.existsAsFile())
        loadReverbFile (audio.reverb, irFile);
    else if (settings->containsKey ("reverbRoom"))
        audio.reverb.loadPreset (settings->getIntValue ("reverbRoom"));
    if (settings->containsKey ("reverbLevel_dB"))
        audio.reverb.wetDb = (float) settings->getDoubleValue ("reverbLevel_dB");
    if (settings->containsKey ("reverbPreDelay_ms"))
        audio.reverb.preDelayMs = (float) settings->getDoubleValue ("reverbPreDelay_ms");
}

void MainComponent::saveState()
{
    auto* settings = properties.getUserSettings();
    if (settings == nullptr)
        return;

    if (instrument != nullptr)
        settings->setValue ("instrument", juce::String (whistle::toJson (instrument->getInstrument())));
    settings->setValue ("sensors", juce::String (sensorsToText (audio)));
    settings->setValue ("reverbRoom", audio.reverb.getPresetIndex());
    settings->setValue ("reverbFile", juce::String (audio.reverb.getCurrentFile()));
    settings->setValue ("reverbLevel_dB", audio.reverb.wetDb.load());
    settings->setValue ("reverbPreDelay_ms", audio.reverb.preDelayMs.load());
    settings->setValue ("sourceSpread_deg", audio.reverb.getSpread());
    settings->setValue ("smoothBluetooth", audio.smoothBluetooth.load());
    settings->setValue ("tab", tabs.getCurrentTabIndex());
    if (auto device = audio.deviceManager.createStateXml())
        settings->setValue ("audioDevice", device.get());
    properties.saveIfNeeded();
}

void MainComponent::appSuspended()
{
    if (saveOnExit)
        saveState();
    if (! receiver.isConnected() || receiver.isSimulating())
        audio.deviceManager.closeAudioDevice();
}

void MainComponent::appResumed()
{
    if (audio.deviceManager.getCurrentAudioDevice() == nullptr)
        audio.deviceManager.restartLastAudioDevice();
}

void MainComponent::resized()
{
    tabs.setBounds (getLocalBounds());
    aboutButton.setBounds (getWidth() - 70, 3, 62, 24);
}

void MainComponent::showAbout()
{
   #if JUCE_IOS
    // No dialog windows on iOS (they get no title bar to close them with): show it over the app instead.
    auto panel = std::make_unique<OverlayPanel> (std::make_unique<AboutPanel>(), "About WhistleSim");
    panel->onClose = [this] { overlay.reset(); };
    panel->showOver (*this);
    overlay = std::move (panel);
   #else
    if (aboutWindow != nullptr)
    {
        aboutWindow->toFront (true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AboutPanel());
    options.dialogTitle = "About WhistleSim";
    options.dialogBackgroundColour = juce::Colour (0xff1e1f22);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    aboutWindow = options.launchAsync();
   #endif
}
