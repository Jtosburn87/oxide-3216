#include "MainComponent.h"
#include "AudioSettingsComponent.h"
#include "TalkbackSettings.h"
#include "BinaryData.h"

using namespace ConsoleColours;

namespace
{
    constexpr int topBarH = 48;

    /** Touchscreens: the console's viewport scrolls when a finger drags across it. Controls opt out, so a
        drag that starts on a fader, knob, button or text field works that control and leaves the view
        alone; dragging on the panels between them still scrolls. */
    void keepTouchDragsOnControls (juce::Component& c)
    {
        const auto* label = dynamic_cast<juce::Label*> (&c);
        if (dynamic_cast<juce::Slider*> (&c) != nullptr || dynamic_cast<juce::Button*> (&c) != nullptr
            || dynamic_cast<juce::TextEditor*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr
            || dynamic_cast<Knob*> (&c) != nullptr || dynamic_cast<Fader*> (&c) != nullptr || dynamic_cast<BusKnob*> (&c) != nullptr
            || (label != nullptr && label->isEditable()))
            c.setViewportIgnoreDragFlag (true);

        for (auto* child : c.getChildren())
            keepTouchDragsOnControls (*child);
    }
}

MainComponent::MainComponent (AudioEngine& e, PluginManager& pm, SessionController& sc, juce::PropertiesFile& s, std::function<void()> save)
    : engine (e), saveSettings (std::move (save)), settings (s), pluginManager (pm), sessions (sc),
      pluginContext { pm, pluginWindows,
                      [this] { auto* d = engine.getDeviceManager().getCurrentAudioDevice(); return d != nullptr ? d->getCurrentSampleRate() : 48000.0; },
                      [this] { if (saveSettings) saveSettings(); } },
      desk (e, &pluginContext),
      patchBay (e, [this] { if (saveSettings) saveSettings(); })
{
    logo = juce::Drawable::createFromImageData (BinaryData::oxide3216logo_svg, (size_t) BinaryData::oxide3216logo_svgSize);
    audioButton.onClick = [this] { openAudioSettings(); };
    patchButton.setTooltip ("Open the patch bay");
    patchButton.onClick = [this] { patchBay.setVisible (true); patchBay.toFront (true); };
    patchBay.onClose = [this] { patchBay.setVisible (false); patchButton.grabKeyboardFocus(); };
    addChildComponent (patchBay);

    pluginsButton.setTooltip ("Scan for VST3 plugins and see the ones that failed");
    pluginsButton.onClick = [this] { openPluginList(); };
    rackButton.setTooltip ("Every insert slot at once, as racks of outboard gear");
    rackButton.onClick = [this]
    {
        if (rackWindow == nullptr)
            rackWindow = std::make_unique<RackWindow> (engine, pluginContext, settings);
        rackWindow->show();
    };
    sessionButton.setTooltip ("New, open, save, save as, export the mix");
    sessionButton.onClick = [this] { showSessionMenu(); };
    sectionsButton.setTooltip ("Sections of the console in windows of their own; unlock the channel chain order");
    desk.setChainUnlocked (settings.getBoolValue ("chainUnlocked"));
    applyCassetteAnimation();
    sectionsButton.onClick = [this] { showSectionsMenu(); };
    // Minimised strip sections are remembered between launches.
    // Record (inputs), Mix (returns) or Full (both) next to the centre section; Record on first launch.
    const int view = juce::jlimit (0, 2, settings.getIntValue ("deskView", Desk::recordView));
    desk.setView ((Desk::View) view);
    viewSwitch.setView (view);
    for (auto& b : viewSwitch.buttons)
        addAndMakeVisible (b);   // in the top bar
    tapeMachineButton.setTooltip ("The tape machine in a window of its own: transport, moving cassette, and meters for all 16 tracks");
    tapeMachineButton.onClick = [this]
    {
        if (tapeWindow == nullptr)
        {
            tapeWindow = std::make_unique<TapeMachineWindow> (engine, settings, [this] { if (saveSettings) saveSettings(); });
            tapeWindow->onShownChanged = [this] (bool) { applyCassetteAnimation(); };
        }
        tapeWindow->show();
        applyCassetteAnimation();
    };
    addAndMakeVisible (tapeMachineButton);
    viewSwitch.onChange = [this] (int v) { showView (v); };
    desk.setCollapsed (Desk::inputSection, settings.getIntValue ("collapsed.input"));
    desk.setCollapsed (Desk::returnSection, settings.getIntValue ("collapsed.return"));
    desk.onCollapseChanged = [this]
    {
        settings.setValue ("collapsed.input", desk.getCollapsed (Desk::inputSection));
        settings.setValue ("collapsed.return", desk.getCollapsed (Desk::returnSection));
        if (saveSettings) saveSettings();
    };
    // Docking or undocking a section refits the view; minimising a section keeps the zoom and the view.
    desk.onSizeChanged = [this] (bool sectionMoved) { if (sectionMoved) fitToWindow(); else applyZoom(); };
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

    for (juce::Component* c : { (juce::Component*) &zoomLabel, (juce::Component*) &zoomSlider, (juce::Component*) &statusLabel })
        addAndMakeVisible (c);

    // The app buttons sit on the console, in a column between the tape machine and the bus compressor.
    for (auto* b : { &sessionButton, &audioButton, &patchButton, &pluginsButton, &rackButton, &sectionsButton })
        desk.getAppButtons().addAndMakeVisible (b);
    keepTouchDragsOnControls (desk);   // every control is built by now (sections keep the flag in their own windows)

    deskHolder.addAndMakeVisible (desk);
    viewport.setViewedComponent (&deskHolder, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    touch = std::make_unique<TouchPanZoom> (viewport, [this] { return (float) zoomSlider.getValue() / 100.0f; },
                                            [this] (float z) { zoomSlider.setValue (z * 100.0f, juce::sendNotificationSync); });
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
    rackWindow = nullptr;
    if (tapeWindow != nullptr)
        tapeWindow->onShownChanged = nullptr;
    tapeWindow = nullptr;
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
    if (viewport.getWidth() <= 0 || viewport.getHeight() <= 0)
        return;
    // The whole console, as big as it fits; below 30 % it stops shrinking and scrolls instead.
    const float z = juce::jmin ((float) viewport.getWidth() / (float) desk.getWidth(),
                                (float) viewport.getHeight() / (float) desk.getHeight());
    zoomSlider.setValue (juce::jlimit (30.0, 200.0, std::floor (z * 100.0)), juce::dontSendNotification);
    applyZoom();
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

    // The logo's black inset (OXIDE | MODEL 3216), without its aluminium faceplate.
    if (logo != nullptr)
    {
        const juce::Rectangle<float> inset (68.0f, 68.0f, 1064.0f, 234.0f);   // in the SVG's coordinates
        const auto plateR = bar.reduced (14, 6).removeFromLeft (200).toFloat()
                                .withWidth ((float) (topBarH - 12) * inset.getWidth() / inset.getHeight());
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (plateR.toNearestInt());
        logo->draw (g, 1.0f, juce::RectanglePlacement (juce::RectanglePlacement::stretchToFit).getTransformToFit (inset, plateR));
    }
}

void MainComponent::resized()
{
    auto bar = getLocalBounds().removeFromTop (topBarH).reduced (14, 9);
    bar.removeFromLeft (214);
    zoomLabel.setBounds (bar.removeFromLeft (48));
    zoomSlider.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (24);
    for (auto& b : viewSwitch.buttons)
        b.setBounds (bar.removeFromLeft (76));
    bar.removeFromLeft (16);
    tapeMachineButton.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (16);
    statusLabel.setBounds (bar);

    viewport.setBounds (getLocalBounds().withTrimmedTop (topBarH));
    patchBay.setBounds (getLocalBounds());

    fitToWindow();   // the console scales with the window
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
    m.addCommandItem (&commands, importAudio, "Import audio...");
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
                    newSession, openSession, saveSession, saveSessionAs, exportMix, importAudio,
                    recordViewCmd, mixViewCmd, fullViewCmd, nextViewCmd,
                    tapeMachineCmd, rackCmd, patchBayCmd, talkbackCmd });
    for (int t = 0; t < TapeMachine::numTracks; ++t)
        ids.add (armTrack1 + t);
    ids.add (disarmAllCmd);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    // Record arms have no keys until the user gives them some.
    if (id >= armTrack1 && id < armTrack1 + TapeMachine::numTracks)
    {
        const juce::String n (id - armTrack1 + 1);
        info.setInfo ("Arm track " + n, "Record arm on / off for tape track " + n, "Record arm", 0);
        return;
    }
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
        case exportMix:     info.setInfo ("Export mix", "Exports the whole session or In to Out as a WAV in the session's Bounced Audio folder", "Session", 0);
                            info.addDefaultKeypress ('e', cmd); break;
        case importAudio:   info.setInfo ("Import audio", "Imports an audio file onto one tape track (a stereo file onto two)", "Session", 0);
                            info.addDefaultKeypress ('i', cmd); break;
        case recordViewCmd: info.setInfo ("Record view", "Shows input channels 1-16 and the centre section", "View", 0);
                            info.addDefaultKeypress ('1', cmd); break;
        case mixViewCmd:    info.setInfo ("Mix view", "Shows the centre section and the tape returns", "View", 0);
                            info.addDefaultKeypress ('2', cmd); break;
        case fullViewCmd:   info.setInfo ("Full view", "Shows both banks either side of the centre section", "View", 0);
                            info.addDefaultKeypress ('3', cmd); break;
        case nextViewCmd:   info.setInfo ("Next view", "Cycles Record, Mix, Full, then back to Record", "View", 0);
                            info.addDefaultKeypress (juce::KeyPress::tabKey, cmd); break;
        case tapeMachineCmd: info.setInfo ("Tape Machine", "Opens the Tape Machine window (transport, cassette and meters)", "Windows", 0);
                            info.addDefaultKeypress ('t', cmd); break;
        case rackCmd:       info.setInfo ("Rack", "Opens the effects rack", "Windows", 0);
                            info.addDefaultKeypress ('r', cmd); break;
        case patchBayCmd:   info.setInfo ("Patch bay", "Opens the patch bay", "Windows", 0);
                            info.addDefaultKeypress ('p', cmd); break;
        case talkbackCmd:   info.setInfo ("Talkback Settings", "Opens the Talkback Settings window", "Windows", 0);
                            info.addDefaultKeypress ('k', cmd); break;
        case disarmAllCmd:  info.setInfo ("Disarm all tracks", "Takes the record arm off every tape track", "Record arm", 0); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    auto& tape = engine.tape;
    if (info.commandID >= armTrack1 && info.commandID < armTrack1 + TapeMachine::numTracks)
    {
        tape.arm[(size_t) (info.commandID - armTrack1)]->toggle();
        return true;
    }
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
        case importAudio:   ImportAudioDialog::show (engine, settings); return true;
        case recordViewCmd: showView (Desk::recordView); return true;
        case mixViewCmd:    showView (Desk::mixView); return true;
        case fullViewCmd:   showView (Desk::fullView); return true;
        case nextViewCmd:   showView ((desk.getView() + 1) % 3); return true;
        case tapeMachineCmd: tapeMachineButton.onClick(); return true;
        case rackCmd:       rackButton.onClick(); return true;
        case patchBayCmd:   patchButton.onClick(); return true;
        case talkbackCmd:   TalkbackSettings::show (engine); return true;
        case disarmAllCmd:  for (auto* a : tape.arm) a->set (0.0f);
                            return true;
        default:            return false;
    }
}

void MainComponent::showView (int v)
{
    desk.setView ((Desk::View) v);
    viewSwitch.setView (v);
    settings.setValue ("deskView", v);
    if (saveSettings) saveSettings();
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
    const bool coloured = engine.preampColour->on();
    m.addItem ("Console colour (1073-style preamps)", true, coloured, [this, coloured]
    {
        engine.preampColour->set (coloured ? 0.0f : 1.0f);
    });
    const bool animated = settings.getBoolValue ("cassetteAnimation", true);
    m.addItem ("Animate the cassette (turn off to save CPU)", true, animated, [this, animated]
    {
        settings.setValue ("cassetteAnimation", ! animated);
        applyCassetteAnimation();
        if (saveSettings) saveSettings();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sectionsButton));
}

void MainComponent::applyCassetteAnimation()
{
    // While the Tape machine window is up, its cassette is the one that moves; the console's stands still.
    const bool animated = settings.getBoolValue ("cassetteAnimation", true);
    const bool windowShown = tapeWindow != nullptr && tapeWindow->isVisible();
    desk.setCassetteAnimated (animated);
    desk.setCassetteFrozen (windowShown);
    if (tapeWindow != nullptr)
        tapeWindow->setCassetteAnimated (animated);
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
