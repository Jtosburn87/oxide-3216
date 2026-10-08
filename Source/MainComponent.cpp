#include "MainComponent.h"
#include "AudioSettingsComponent.h"

using namespace ConsoleColours;

namespace
{
    constexpr int topBarH = 48;
}

MainComponent::MainComponent (AudioEngine& e, PluginManager& pm, SessionController& sc, juce::PropertiesFile& s, std::function<void()> save)
    : engine (e), saveSettings (std::move (save)), settings (s), pluginManager (pm), sessions (sc),
      pluginContext { pm, pluginWindows,
                      [this] { auto* d = engine.getDeviceManager().getCurrentAudioDevice(); return d != nullptr ? d->getCurrentSampleRate() : 48000.0; },
                      [this] { if (saveSettings) saveSettings(); } },
      desk (e, &pluginContext),
      patchBay (e, [this] { if (saveSettings) saveSettings(); })
{
    audioButton.onClick = [this] { openAudioSettings(); };
    toneButton.setClickingTogglesState (true);
    toneButton.setTooltip ("Sends a 440 Hz tone at -18 dBFS to the main outputs");
    toneButton.onClick = [this] { engine.testToneOn.store (toneButton.getToggleState()); };
    fitButton.setTooltip ("Zoom so the whole console fits the window");
    fitButton.onClick = [this] { fitToWindow(); };
    patchButton.setTooltip ("Open the patch bay");
    patchButton.onClick = [this] { patchBay.setVisible (true); patchBay.toFront (true); };
    patchBay.onClose = [this] { patchBay.setVisible (false); patchButton.grabKeyboardFocus(); };
    addChildComponent (patchBay);

    pluginsButton.setTooltip ("Scan for VST3 plugins and see the ones that failed");
    pluginsButton.onClick = [this] { openPluginList(); };
    sessionButton.setTooltip ("New, open, save, save as, export the mix");
    sessionButton.onClick = [this] { showSessionMenu(); };
    sectionsButton.setTooltip ("Take sections of the console out into windows of their own, or put them back");
    sectionsButton.onClick = [this] { showSectionsMenu(); };
    desk.onSizeChanged = [this] { if (viewport.getWidth() > 0) fitToWindow(); else applyZoom(); };
    sessions.onTitleChanged = [this] { if (auto* w = getTopLevelComponent()) w->setName (sessions.windowTitle()); };
    setWantsKeyboardFocus (true);
    // A plugin window must close before its plugin is deleted.
    forEachChain ([this] (InsertChain& c) { c.beforeDelete = [this] (juce::AudioProcessor* p) { pluginWindows.close (p); }; });

    zoomLabel.setText ("Zoom", juce::dontSendNotification);
    zoomLabel.setFont (ConsoleLookAndFeel::uiFont (14.0f));
    zoomLabel.setColour (juce::Label::textColourId, bridgeText);
    zoomLabel.setJustificationType (juce::Justification::centredRight);

    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setRange (30.0, 200.0, 1.0);
    zoomSlider.setValue (100.0, juce::dontSendNotification);
    zoomSlider.setTitle ("Zoom");
    zoomSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    zoomSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff9aa0a3));
    zoomSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff5d6467));
    zoomSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff26292b));
    zoomSlider.onValueChange = [this] { applyZoom(); };

    statusLabel.setFont (ConsoleLookAndFeel::uiFont (14.0f));
    statusLabel.setColour (juce::Label::textColourId, bridgeText);
    statusLabel.setJustificationType (juce::Justification::centredRight);

    for (juce::Component* c : { (juce::Component*) &sessionButton, (juce::Component*) &audioButton, (juce::Component*) &toneButton, (juce::Component*) &zoomLabel,
                                (juce::Component*) &zoomSlider, (juce::Component*) &fitButton, (juce::Component*) &patchButton, (juce::Component*) &pluginsButton, (juce::Component*) &sectionsButton, (juce::Component*) &statusLabel })
        addAndMakeVisible (c);

    deskHolder.addAndMakeVisible (desk);
    viewport.setViewedComponent (&deskHolder, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    applyZoom();

    engine.getDeviceManager().addChangeListener (this);
    refreshDeviceText();
    startTimerHz (2);

    setSize (1400, 820);

    // Once the main window is up, bring back the section windows that were out last time.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->restoreSections(); });
}

MainComponent::~MainComponent()
{
    saveSections();
    for (auto& w : sectionWindows)
        w = nullptr;
    forEachChain ([] (InsertChain& c) { c.beforeDelete = nullptr; c.onChange = nullptr; });
    pluginWindows.closeAll();
    if (pluginListWindow != nullptr)
        delete pluginListWindow.getComponent();
    engine.getDeviceManager().removeChangeListener (this);
    if (settingsWindow != nullptr)
        delete settingsWindow.getComponent();
}

void MainComponent::applyZoom()
{
    const float z = (float) zoomSlider.getValue() / 100.0f;
    desk.setTransform (juce::AffineTransform::scale (z));
    deskHolder.setSize (juce::roundToInt ((float) desk.getWidth() * z), juce::roundToInt ((float) desk.getHeight() * z));
}

void MainComponent::fitToWindow()
{
    const float zw = (float) viewport.getWidth() / (float) desk.getWidth();
    const float zh = (float) viewport.getHeight() / (float) desk.getHeight();
    zoomSlider.setValue (juce::jlimit (30.0, 200.0, std::floor (juce::jmin (zw, zh) * 100.0)), juce::sendNotificationSync);
}

void MainComponent::openAudioSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AudioSettingsComponent (engine, [this] { if (saveSettings) saveSettings(); }));
    o.dialogTitle = "Audio device";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    settingsWindow = o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshDeviceText();
}

void MainComponent::refreshDeviceText()
{
    auto& dm = engine.getDeviceManager();
    if (auto* device = dm.getCurrentAudioDevice())
        deviceText = dm.getCurrentAudioDeviceType() + "  |  " + device->getName() + "  |  "
                   + juce::String ((int) device->getCurrentSampleRate()) + " Hz  |  "
                   + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
    else
        deviceText = "Audio off. Press Power, or click Audio device to choose one.";

    statusLabel.setText (deviceText, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    // Twice a second: is audio running, CPU load, dropouts.
    auto& dm = engine.getDeviceManager();
    if (dm.getCurrentAudioDevice() == nullptr)
    {
        statusLabel.setText (deviceText, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, bridgeText);
        return;
    }

    const auto count = engine.getCallbackCount();
    const bool running = count != lastCallbackCount;
    lastCallbackCount = count;

    const int xruns = dm.getXRunCount();
    const auto dropped = engine.tape.getDroppedSamples();
    statusLabel.setText (deviceText + "  |  "
                             + (running ? "CPU " + juce::String (juce::roundToInt (dm.getCpuUsage() * 100.0)) + "%"
                                        : juce::String ("AUDIO STOPPED"))
                             + (xruns > 0 ? "  |  dropouts " + juce::String (xruns) : juce::String())
                             + (dropped > 0 ? "  |  tape overrun" : juce::String()),
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, running && dropped == 0 ? bridgeText : meterRed.brighter (0.4f));
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bridge);

    auto bar = getLocalBounds().removeFromTop (topBarH);
    g.setColour (juce::Colour (0xff3a3f42));
    g.fillRect (0, topBarH - 1, getWidth(), 1);

    auto plateR = bar.reduced (14, 8).removeFromLeft (200).toFloat();
    g.setColour (plate);
    g.fillRect (plateR);
    g.setColour (edge);
    g.drawRect (plateR, 1.0f);
    g.setColour (juce::Colour (0xffe4e6e7));
    g.setFont (ConsoleLookAndFeel::uiFont (20.0f, true));
    g.drawText ("Console 32", plateR.withTrimmedLeft (12).withWidth (110), juce::Justification::centredLeft);
    g.setFont (ConsoleLookAndFeel::uiFont (15.0f));
    g.setColour (juce::Colour (0xffe4e6e7).withAlpha (0.8f));
    g.drawText ("Tape 16", plateR.withTrimmedLeft (122), juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto bar = getLocalBounds().removeFromTop (topBarH).reduced (14, 9);
    bar.removeFromLeft (214);
    sessionButton.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (8);
    audioButton.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (8);
    toneButton.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (16);
    zoomLabel.setBounds (bar.removeFromLeft (48));
    zoomSlider.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (6);
    fitButton.setBounds (bar.removeFromLeft (60));
    bar.removeFromLeft (8);
    patchButton.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (8);
    pluginsButton.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (8);
    sectionsButton.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (8);
    statusLabel.setBounds (bar);

    viewport.setBounds (getLocalBounds().withTrimmedTop (topBarH));
    patchBay.setBounds (getLocalBounds());

    if (! fittedOnce && viewport.getWidth() > 0)
    {
        fittedOnce = true;
        fitToWindow();
    }
}

void MainComponent::forEachChain (const std::function<void (InsertChain&)>& fn)
{
    for (auto* c : engine.subInserts) fn (*c);
    for (auto* c : engine.auxInserts) fn (*c);
    fn (engine.masterInserts);
}

void MainComponent::openPluginList()
{
    if (pluginListWindow != nullptr)
    {
        pluginListWindow->toFront (true);
        return;
    }

    // JUCE's plugin list: scan (Options > Scan for new or updated VST3 plug-ins, where extra folders
    // can be added), see what was found, and see the plugins that failed (shown in red).
    auto list = std::make_unique<juce::PluginListComponent> (pluginManager.formats, pluginManager.known,
                                                             pluginManager.deadMansPedalFile(), &pluginManager.settings, true);
    list->setNumberOfThreadsForScanning (juce::jlimit (1, 4, juce::SystemStats::getNumCpus() / 2));
    list->setSize (900, 560);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list.release());
    o.dialogTitle = "Plugins (VST3)  -  Options > Scan to find plugins; failed ones are shown in red";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    pluginListWindow = o.launchAsync();
}

void MainComponent::parentHierarchyChanged()
{
    if (auto* w = getTopLevelComponent(); w != nullptr && w != this)
        w->setName (sessions.windowTitle());
}

void MainComponent::showSessionMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New session", true, false);
    m.addItem (2, "Open session...   (Ctrl+O)");
    m.addSeparator();
    m.addItem (3, "Save   (Ctrl+S)");
    m.addItem (4, "Save as...   (Ctrl+Shift+S)");
    m.addSeparator();
    m.addItem (5, "Export mix to WAV...   (Ctrl+E)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton), [this] (int r)
    {
        switch (r)
        {
            case 1: sessions.newSession(); break;
            case 2: sessions.openSession(); break;
            case 3: sessions.save(); break;
            case 4: sessions.saveAs(); break;
            case 5: sessions.exportMix(); break;
            default: break;
        }
    });
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();

    if (mods.isCommandDown())
    {
        if (code == 'N') { sessions.newSession(); return true; }
        if (code == 'O') { sessions.openSession(); return true; }
        if (code == 'S') { if (mods.isShiftDown()) sessions.saveAs(); else sessions.save(); return true; }
        if (code == 'E') { sessions.exportMix(); return true; }
        return false;
    }

    // Transport: Space plays / stops, Home rewinds to the start (when no text field has the keyboard).
    if (code == juce::KeyPress::spaceKey)
    {
        if (engine.tape.isPlaying()) engine.tape.stop(); else engine.tape.play();
        return true;
    }
    if (code == juce::KeyPress::homeKey)
    {
        engine.tape.rewind();
        return true;
    }
    return false;
}

//==============================================================================
namespace
{
    juce::String sectionKey (Desk::Section s)
    {
        static const char* const names[] = { "input", "centre", "return" };
        return juce::String ("sectionWindow.") + names[(int) s];
    }
}

void MainComponent::showSectionsMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < Desk::numSections; ++i)
    {
        const auto s = (Desk::Section) i;
        m.addItem (Desk::sectionName (s) + " in its own window", true, ! desk.isDocked (s), [this, s]
        {
            if (desk.isDocked (s)) takeOut (s); else putBack (s);
        });
    }
    m.addSeparator();
    const bool anyOut = std::any_of (sectionWindows.begin(), sectionWindows.end(), [] (auto& w) { return w != nullptr; });
    m.addItem ("Put everything back", anyOut, false, [this]
    {
        for (int i = 0; i < Desk::numSections; ++i)
            putBack ((Desk::Section) i);
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sectionsButton));
}

void MainComponent::takeOut (Desk::Section s)
{
    auto& w = sectionWindows[(size_t) s];
    if (w != nullptr)
    {
        w->toFront (true);
        return;
    }

    desk.setDocked (s, false);
    w = std::make_unique<SectionWindow> (s, desk.getSection (s),
                                         [this, s] { putBack (s); },
                                         [this] (const juce::KeyPress& k) { return keyPressed (k); });

    // Back where it was last time, if that is still on a screen.
    const auto state = settings.getValue (sectionKey (s) + ".bounds");
    if (state.isNotEmpty())
    {
        const auto before = w->getBounds();
        w->restoreWindowStateFromString (state);
        const auto titleBar = w->getBounds().withHeight (30);
        if (! juce::Desktop::getInstance().getDisplays().getTotalBounds (true).intersects (titleBar))
            w->setBounds (before);
    }
    saveSections();
}

void MainComponent::putBack (Desk::Section s)
{
    auto& w = sectionWindows[(size_t) s];
    if (w == nullptr)
        return;

    settings.setValue (sectionKey (s) + ".bounds", w->getWindowStateAsString());
    desk.setDocked (s, true);   // the section leaves the window and returns to the console

    // This may be running inside the window's own button or close-button callback, so delete it afterwards.
    juce::MessageManager::callAsync ([old = std::shared_ptr<SectionWindow> (w.release())] {});
    saveSections();
    grabKeyboardFocus();
}

void MainComponent::restoreSections()
{
    for (int i = 0; i < Desk::numSections; ++i)
        if (settings.getBoolValue (sectionKey ((Desk::Section) i) + ".out"))
            takeOut ((Desk::Section) i);
}

void MainComponent::saveSections()
{
    for (int i = 0; i < Desk::numSections; ++i)
    {
        const auto key = sectionKey ((Desk::Section) i);
        if (auto& w = sectionWindows[(size_t) i])
            settings.setValue (key + ".bounds", w->getWindowStateAsString());
        settings.setValue (key + ".out", sectionWindows[(size_t) i] != nullptr);
    }
    if (saveSettings)
        saveSettings();
}
