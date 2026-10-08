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
    sectionsButton.setTooltip ("Sections of the console in windows of their own; unlock the channel chain order");
    desk.setChainUnlocked (settings.getBoolValue ("chainUnlocked"));
    sectionsButton.onClick = [this] { showSectionsMenu(); };
    // Minimised strip sections are remembered between launches.
    desk.setCollapsed (Desk::inputSection, settings.getIntValue ("collapsed.input"));
    desk.setCollapsed (Desk::returnSection, settings.getIntValue ("collapsed.return"));
    desk.onCollapseChanged = [this]
    {
        settings.setValue ("collapsed.input", desk.getCollapsed (Desk::inputSection));
        settings.setValue ("collapsed.return", desk.getCollapsed (Desk::returnSection));
        if (saveSettings) saveSettings();
    };
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

    // Keyboard shortcuts: the defaults, then whatever the user changed.
    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    if (auto xml = settings.getXmlValue ("keyMappings"))
        commands.getKeyMappings()->restoreFromXml (*xml);
    commands.getKeyMappings()->addChangeListener (this);

    engine.getDeviceManager().addChangeListener (this);
    refreshDeviceText();
    startTimerHz (2);

    setSize (1400, 820);

    // Once the main window is up, bring back the section windows that were out last time.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->restoreSections(); });
}

MainComponent::~MainComponent()
{
    commands.getKeyMappings()->removeChangeListener (this);
    if (keysWindow != nullptr)
        delete keysWindow.getComponent();
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

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == commands.getKeyMappings())
    {
        // Only the keys that differ from the defaults are stored.
        if (auto xml = commands.getKeyMappings()->createXml (true))
            settings.setValue ("keyMappings", xml.get());
        if (saveSettings) saveSettings();
        return;
    }
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
    statusLabel.setTooltip (xruns > 0 ? "Dropouts: the audio interface asked for audio and did not get it in time. "
                                        "A larger buffer (Audio device: 128 or 256 samples) usually stops them; "
                                        "so can the High performance power plan and switching Wi-Fi off while recording."
                                      : juce::String());
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
    // Command items show whatever keys are currently assigned.
    juce::PopupMenu m;
    m.addCommandItem (&commands, newSession);
    m.addCommandItem (&commands, openSession, "Open session...");
    m.addSeparator();
    m.addCommandItem (&commands, saveSession);
    m.addCommandItem (&commands, saveSessionAs, "Save as...");
    m.addSeparator();
    m.addCommandItem (&commands, exportMix, "Export mix to WAV...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    // Every shortcut is a command with keys the user can change (Options > Keyboard shortcuts).
    // Text fields that have the keyboard get their keys first.
    return commands.getKeyMappings()->keyPressed (k, this);
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    ids.addArray ({ playStop, play, stop, record, rewind, fastForward, returnToZero,
                    newSession, openSession, saveSession, saveSessionAs, exportMix });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    switch (id)
    {
        case playStop:      info.setInfo ("Play / stop", "Plays the tape, or stops it if it is playing", "Transport", 0);
                            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0); break;
        case play:          info.setInfo ("Play", "Plays the tape", "Transport", 0); break;
        case stop:          info.setInfo ("Stop", "Stops the tape (a wind brakes to a stop)", "Transport", 0); break;
        case record:        info.setInfo ("Record", "Record enable on / off", "Transport", 0); break;
        case rewind:        info.setInfo ("Rewind", "Winds back to the start", "Transport", 0);
                            info.addDefaultKeypress (juce::KeyPress::homeKey, 0); break;
        case fastForward:   info.setInfo ("Fast forward", "Winds on to the end of the recording", "Transport", 0);
                            info.addDefaultKeypress (juce::KeyPress::endKey, 0); break;
        case returnToZero:  info.setInfo ("Return to zero", "Jumps straight back to the start, without winding", "Transport", 0); break;
        case newSession:    info.setInfo ("New session", "Starts a new, empty session", "Session", 0);
                            info.addDefaultKeypress ('n', cmd); break;
        case openSession:   info.setInfo ("Open session", "Opens a saved session", "Session", 0);
                            info.addDefaultKeypress ('o', cmd); break;
        case saveSession:   info.setInfo ("Save", "Saves the session", "Session", 0);
                            info.addDefaultKeypress ('s', cmd); break;
        case saveSessionAs: info.setInfo ("Save as", "Saves the session in a new folder", "Session", 0);
                            info.addDefaultKeypress ('s', cmd | juce::ModifierKeys::shiftModifier); break;
        case exportMix:     info.setInfo ("Export mix", "Exports the mix to a WAV file", "Session", 0);
                            info.addDefaultKeypress ('e', cmd); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    auto& tape = engine.tape;
    switch (info.commandID)
    {
        case playStop:      if (tape.isPlaying()) tape.stop(); else tape.play(); return true;
        case play:          tape.play(); return true;
        case stop:          tape.stop(); return true;
        case record:        tape.setRecordEnabled (! tape.isRecordEnabled()); return true;
        case rewind:        tape.rewind(); return true;
        case fastForward:   tape.fastForward(); return true;
        case returnToZero:  tape.stop(); tape.locate (0); return true;
        case newSession:    sessions.newSession(); return true;
        case openSession:   sessions.openSession(); return true;
        case saveSession:   sessions.save(); return true;
        case saveSessionAs: sessions.saveAs(); return true;
        case exportMix:     sessions.exportMix(); return true;
        default:            return false;
    }
}

void MainComponent::openKeyMappings()
{
    if (keysWindow != nullptr)
    {
        keysWindow->toFront (true);
        return;
    }

    auto editor = std::make_unique<juce::KeyMappingEditorComponent> (*commands.getKeyMappings(), true);
    editor->setColours (juce::Colour (0xff2a2d30), bridgeText);
    editor->setSize (560, 460);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor.release());
    o.dialogTitle = "Keyboard shortcuts  -  click + to add a key, click a key to change or remove it";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    keysWindow = o.launchAsync();
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
    m.addSeparator();
    const bool unlocked = settings.getBoolValue ("chainUnlocked");
    m.addItem ("Keyboard shortcuts...", [this] { openKeyMappings(); });
    m.addSeparator();
    m.addItem ("Unlock chain order (EQ, gate, compressor per channel)", true, unlocked, [this, unlocked]
    {
        settings.setValue ("chainUnlocked", ! unlocked);
        desk.setChainUnlocked (! unlocked);
        if (saveSettings) saveSettings();
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
