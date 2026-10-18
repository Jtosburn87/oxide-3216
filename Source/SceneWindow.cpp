#include "SceneWindow.h"
#include "ConsoleLookAndFeel.h"
#include <juce_audio_devices/juce_audio_devices.h>

using namespace ConsoleColours;

namespace
{
    const juce::Colour segOn { 0xffff3b24 }, segOff { 0x17ff3b24 };
}

//==============================================================================
SceneReadout::SceneReadout (SceneMemory& s) : scenes (s)
{
    setTitle ("Scene number");
    setTooltip ("The selected scene (flashing: press GO to recall it). Right-click: Scene Memory");
    startTimerHz (8);
}

void SceneReadout::paintDigit (juce::Graphics& g, juce::Rectangle<float> r, juce::juce_wchar c)
{
    // Segments a-g: a top, b top right, c bottom right, d bottom, e bottom left, f top left, g middle.
    static const std::map<juce::juce_wchar, const char*> lit {
        { '0', "abcdef" }, { '1', "bc" }, { '2', "abged" }, { '3', "abgcd" }, { '4', "fgbc" }, { '5', "afgcd" },
        { '6', "afgedc" }, { '7', "abc" }, { '8', "abcdefg" }, { '9', "abcdfg" }, { '-', "g" } };
    const auto it = lit.find (c);
    const juce::String on (it != lit.end() ? it->second : "");

    const float w = r.getWidth(), h = r.getHeight(), t = w * 0.17f, gap = t * 0.25f;
    const float x0 = r.getX(), y0 = r.getY(), midY = y0 + h * 0.5f;
    const auto horizontal = [&] (float y)
    {
        juce::Path p;
        const float l = x0 + gap + t * 0.5f, rr = x0 + w - gap - t * 0.5f;
        p.startNewSubPath (l - t * 0.5f, y);
        p.lineTo (l, y - t * 0.5f);
        p.lineTo (rr, y - t * 0.5f);
        p.lineTo (rr + t * 0.5f, y);
        p.lineTo (rr, y + t * 0.5f);
        p.lineTo (l, y + t * 0.5f);
        p.closeSubPath();
        return p;
    };
    const auto vertical = [&] (float x, float top, float bottom)
    {
        juce::Path p;
        const float tt = top + gap + t * 0.5f, bb = bottom - gap - t * 0.5f;
        p.startNewSubPath (x, tt - t * 0.5f);
        p.lineTo (x + t * 0.5f, tt);
        p.lineTo (x + t * 0.5f, bb);
        p.lineTo (x, bb + t * 0.5f);
        p.lineTo (x - t * 0.5f, bb);
        p.lineTo (x - t * 0.5f, tt);
        p.closeSubPath();
        return p;
    };

    const float left = x0 + t * 0.5f, right = x0 + w - t * 0.5f, top = y0 + t * 0.5f, bottom = y0 + h - t * 0.5f;
    const std::pair<char, juce::Path> segments[] = {
        { 'a', horizontal (top) }, { 'g', horizontal (midY) }, { 'd', horizontal (bottom) },
        { 'f', vertical (left, top, midY) }, { 'b', vertical (right, top, midY) },
        { 'e', vertical (left, midY, bottom) }, { 'c', vertical (right, midY, bottom) } };

    // A slight italic slant, as on the real thing.
    const auto slant = juce::AffineTransform::shear (-0.08f, 0.0f).translated (0.08f * (y0 + h), 0.0f);
    for (const auto& [name, path] : segments)
    {
        const bool isOn = on.containsChar ((juce::juce_wchar) name);
        if (isOn)
        {
            g.setColour (segOn.withAlpha (0.35f));
            g.strokePath (path, juce::PathStrokeType (2.5f), slant);
        }
        g.setColour (isOn ? segOn : segOff);
        g.fillPath (path, slant);
    }
}

void SceneReadout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (r);
    g.setColour (juce::Colour (0xff1d1f20));
    g.drawRect (r, 1.0f);

    const int sel = scenes.getSelected(), cur = scenes.getCurrent();
    bool any = false;
    for (int s = 0; s < SceneMemory::numScenes && ! any; ++s)
        any = scenes.isStored (s);
    const auto text = any || cur >= 0 ? SceneMemory::number (sel) : juce::String ("--");
    const bool dark = any && sel != cur && ! flashOn;

    const float dh = r.getHeight() * 0.72f, dw = dh * 0.56f, gapX = dw * 0.32f;
    const float x = r.getCentreX() - (2.0f * dw + gapX) * 0.5f, y = r.getCentreY() - dh * 0.5f;
    for (int i = 0; i < 2; ++i)
        paintDigit (g, { x + (float) i * (dw + gapX), y, dw, dh }, dark ? (juce::juce_wchar) ' ' : text[i]);

    // Edited since the current scene: an amber dot at the bottom right.
    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getRight() - 8.0f, r.getBottom() - 8.0f });
    g.setColour (shownEdited ? amber : juce::Colour (0xff2a2112));
    g.fillEllipse (dot);
}

void SceneReadout::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()) && onPopupMenu)
        onPopupMenu();
}

void SceneReadout::timerCallback()
{
    ticks = (ticks + 1) % 8;
    const bool edited = ticks % 4 == 0 ? scenes.isEdited() : shownEdited;   // twice a second is plenty
    const bool flash = ticks < 5;   // on most of the time, off briefly
    const bool waiting = scenes.getSelected() != scenes.getCurrent();
    if (edited != shownEdited || scenes.getSelected() != shownSelected || scenes.getCurrent() != shownCurrent
        || (waiting && flash != flashOn))
    {
        shownEdited = edited;
        shownSelected = scenes.getSelected();
        shownCurrent = scenes.getCurrent();
        flashOn = flash;
        repaint();
    }
}

//==============================================================================
juce::Component::SafePointer<juce::DialogWindow> SceneMemoryPanel::window;

namespace
{
    /** Not modal (the console stays usable behind it) and on top until closed; closing deletes it. */
    class SceneMemoryWindow : public juce::DialogWindow
    {
    public:
        explicit SceneMemoryWindow (SceneMemoryPanel* panel)
            : juce::DialogWindow ("Scene Memory", room, true, true)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (panel, true);
            setResizable (false, false);
            setAlwaysOnTop (true);
            centreWithSize (getWidth(), getHeight());
        }

        void closeButtonPressed() override
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SceneMemoryWindow> (this)] { delete safe.getComponent(); });
            setVisible (false);
        }
    };
}

void SceneMemoryPanel::show (SceneMemory& scenes)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }
    auto* w = new SceneMemoryWindow (new SceneMemoryPanel (scenes));
    w->setVisible (true);
    window = w;
}

void SceneMemoryPanel::closeWindow()
{
    delete window.getComponent();
}

SceneMemoryPanel::SceneMemoryPanel (SceneMemory& s)
    : scenes (s), fadeKnob (*s.fadeTime, "Fade", juce::Colour (0xffd9d6cc))
{
    list.setModel (this);
    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0e0f10));
    list.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2a2d30));
    list.setOutlineThickness (1);
    addAndMakeVisible (list);

    go.setTooltip ("Recall the selected scene: its faders and knobs (double-click a scene does the same)");
    store.setTooltip ("Keep the console's faders and knobs in the selected scene");
    prev.setTooltip ("Select the previous stored scene (Go recalls it)");
    next.setTooltip ("Select the next stored scene (Go recalls it)");
    go.onClick = [this] { scenes.recall (scenes.getSelected()); refresh(); };
    store.onClick = [this] { storeSelected(); };
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    fadeKnob.setDialSize (40);
    fadeKnob.setEndLabels ("0", "5 s");
    fadeKnob.setFormatter ([] (float sec) { return sec < 0.05f ? juce::String ("Instant") : juce::String (sec, 1) + " s fade"; });
    addAndMakeVisible (fadeKnob);
    for (auto* b : { &go, &store, &prev, &next })
        addAndMakeVisible (b);

    title.setFont (ConsoleLookAndFeel::uiFont (14.0f));
    title.setTextToShowWhenEmpty ("Scene title", juce::Colour (0xff6c7174));
    title.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e0f10));
    title.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2d30));
    title.onReturnKey = [this] { scenes.setTitle (scenes.getSelected(), title.getText()); refresh(); };
    title.onFocusLost = [this] { scenes.setTitle (scenes.getSelected(), title.getText()); refresh(); };
    addAndMakeVisible (title);

    status.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    status.setColour (juce::Label::textColourId, legend.withAlpha (0.65f));
    status.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (status);

    // MIDI control.
    midiInput.setTooltip ("The MIDI input scene control listens to (a controller, a foot switch, a sequencer)");
    midiInput.onChange = [this]
    {
        const int id = midiInput.getSelectedId();
        if (id == 1)       scenes.setMidiInput ({});
        else if (id == 2)  scenes.setMidiInput (SceneMemory::allInputs);
        else if (id >= 10) scenes.setMidiInput (shownDevices[id - 10]);
    };
    fromPage.setClickingTogglesState (true);
    fromPage.setColour (juce::TextButton::buttonOnColourId, amber);
    fromPage.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1b1b1b));
    fromPage.setTooltip ("Listen to MIDI page tracks whose Out menu has \"Send to scene control\" on: "
                         "their clips' controllers select scenes and their program changes press Go, in time with the song");
    fromPage.onClick = [this] { scenes.setMidiFromPage (fromPage.getToggleState()); };
    midiChannel.addItem ("Any", 1);
    for (int c = 1; c <= 16; ++c)
        midiChannel.addItem (juce::String (c), c + 1);
    midiChannel.setTooltip ("The MIDI channel scene control listens on");
    midiChannel.onChange = [this] { scenes.setMidiChannel (midiChannel.getSelectedId() - 1); };
    sceneCc.addItem ("None", 1);
    for (int c = 0; c < 128; ++c)
        sceneCc.addItem ("CC " + juce::String (c), c + 2);
    sceneCc.setTooltip ("The controller that selects this scene (any value above 0). Go then recalls it.");
    sceneCc.onChange = [this] { scenes.setSceneCc (scenes.getSelected(), sceneCc.getSelectedId() - 2); };
    for (int p = 0; p < 128; ++p)
        goProgram.addItem ("Program " + juce::String (p + 1), p + 1);
    goProgram.setTooltip ("The program change that presses Go (recalls the selected scene)");
    goProgram.onChange = [this] { scenes.setGoProgram (goProgram.getSelectedId() - 1); };
    for (auto* b : { &learnCc, &learnGo })
    {
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }
    learnCc.setTooltip ("Learn: move a controller (from the MIDI input or a MIDI page track) and it selects this scene");
    learnGo.setTooltip ("Learn: send a program change and it becomes Go");
    learnCc.onClick = [this]
    {
        const bool on = scenes.getLearning() != SceneMemory::Learn::sceneCc;
        scenes.learn (on ? SceneMemory::Learn::sceneCc : SceneMemory::Learn::none, scenes.getSelected());
        refresh();
    };
    learnGo.onClick = [this]
    {
        const bool on = scenes.getLearning() != SceneMemory::Learn::goProgram;
        scenes.learn (on ? SceneMemory::Learn::goProgram : SceneMemory::Learn::none);
        refresh();
    };
    for (juce::Component* c : { (juce::Component*) &midiInput, (juce::Component*) &fromPage, (juce::Component*) &midiChannel,
                                (juce::Component*) &sceneCc, (juce::Component*) &learnCc, (juce::Component*) &goProgram, (juce::Component*) &learnGo })
        addAndMakeVisible (c);
    fillInputs();

    setSize (560, 470 + 96);
    refresh();
    list.scrollToEnsureRowIsOnscreen (scenes.getSelected());
    startTimerHz (4);
}

SceneMemoryPanel::~SceneMemoryPanel()
{
    list.setModel (nullptr);
}

void SceneMemoryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
{
    const bool stored = scenes.isStored (row);
    if (isSelected)
        g.fillAll (juce::Colour (0xff2b4a6b));
    g.setColour (juce::Colour (0xff191b1c));
    g.fillRect (0, height - 1, width, 1);

    if (row == scenes.getCurrent())
    {
        g.setColour (juce::Colour (0xff47e06a));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 13.0f, (float) height * 0.5f }));
    }
    const auto text = stored ? legend : juce::Colour (0xff6c7174);
    g.setColour (isSelected ? juce::Colours::white : text);
    g.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
    g.drawText (SceneMemory::number (row), 26, 0, 30, height, juce::Justification::centredLeft, false);
    g.setFont (ConsoleLookAndFeel::uiFont (14.0f));
    g.drawText (stored ? scenes.getTitle (row) : juce::String ("(empty)"), 62, 0, width - 62 - 176, height, juce::Justification::centredLeft, true);
    if (stored)
    {
        // The fade stored with it (its fade out), then when it was stored.
        const float fade = scenes.getStoredFade (row);
        g.setColour (legend.withAlpha (fade >= 0.0f ? 0.8f : 0.35f));
        g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
        g.drawText (fade < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : fade < 0.05f ? juce::String ("Instant") : juce::String (fade, 1) + " s",
                    width - 170, 0, 58, height, juce::Justification::centredRight, false);
        g.setColour (legend.withAlpha (0.5f));
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
        g.drawText (scenes.getStoredTime (row).formatted ("%d %b %H:%M"), width - 106, 0, 98, height, juce::Justification::centredRight, false);
    }
}

void SceneMemoryPanel::selectedRowsChanged (int row)
{
    if (row >= 0 && row != scenes.getSelected())
    {
        scenes.setTitle (scenes.getSelected(), title.getText());   // keep a title being typed for the scene it was typed on
        scenes.select (row);
        refresh();
    }
}

void SceneMemoryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    scenes.select (row);
    scenes.recall (row);
    refresh();
}

void SceneMemoryPanel::storeSelected()
{
    const int s = scenes.getSelected();
    const auto doStore = [this, s]
    {
        scenes.store (s, title.getText());
        status.setText ("Stored the console's faders and knobs in " + SceneMemory::number (s) + ", fade out "
                        + (scenes.getStoredFade (s) < 0.05f ? juce::String ("instant") : juce::String (scenes.getStoredFade (s), 1) + " s") + ".", juce::dontSendNotification);
        refresh();
    };
    if (! scenes.isStored (s))
    {
        doStore();
        return;
    }
    juce::Component::SafePointer<SceneMemoryPanel> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace scene " + SceneMemory::number (s) + "?",
                                        SceneMemory::number (s) + " holds \"" + scenes.getTitle (s) + "\". Store the console there instead?",
                                        "Replace", "Cancel", nullptr,
                                        juce::ModalCallbackFunction::create ([safe, doStore] (int result)
    {
        if (safe != nullptr && result != 0)
            doStore();
    }));
}

void SceneMemoryPanel::step (int direction)
{
    const int s = scenes.nextStored (direction);
    if (s >= 0)
        scenes.select (s);
    refresh();
}

void SceneMemoryPanel::refresh()
{
    const int sel = scenes.getSelected();
    if (list.getSelectedRow() != sel)
    {
        list.selectRow (sel, false, true);
        list.scrollToEnsureRowIsOnscreen (sel);
    }
    if (! title.hasKeyboardFocus (true))
        title.setText (scenes.getTitle (sel), juce::dontSendNotification);
    go.setEnabled (scenes.isStored (sel));
    juce::String text;
    if (! scenes.isStored (sel))
        text = SceneMemory::number (sel) + " is empty. Store keeps the console's faders and knobs here.";
    else if (sel == scenes.getCurrent())
        text = "The console is on " + SceneMemory::number (sel) + (scenes.isEdited() ? ", changed since." : ".");
    else
        text = "Go recalls " + SceneMemory::number (sel) + " " + scenes.getTitle (sel) + ".";
    if (scenes.getLearning() == SceneMemory::Learn::sceneCc)
        text = "Learning: move a controller for " + SceneMemory::number (scenes.getLearningScene()) + ".";
    else if (scenes.getLearning() == SceneMemory::Learn::goProgram)
        text = "Learning: send the program change for Go.";
    status.setText (text, juce::dontSendNotification);
    refreshMidi();
    shownRevision = scenes.getRevision();
    list.repaint();
}

void SceneMemoryPanel::fillInputs()
{
    shownDevices.clear();
    const auto devices = juce::MidiInput::getAvailableDevices();
    midiInput.clear (juce::dontSendNotification);
    midiInput.addItem ("None", 1);
    midiInput.addItem ("All MIDI inputs", 2);
    for (int i = 0; i < devices.size(); ++i)
    {
        shownDevices.add (devices[i].identifier);
        midiInput.addItem (devices[i].name, 10 + i);
    }
    refreshMidi();
}

void SceneMemoryPanel::refreshMidi()
{
    const auto& m = scenes.getMidiSettings();
    int inputId = m.input.isEmpty() ? 1 : m.input == SceneMemory::allInputs ? 2 : 10 + shownDevices.indexOf (m.input);
    if (inputId == 9)   // the session's input is not plugged in
    {
        if (midiInput.indexOfItemId (9) < 0)
            midiInput.addItem (m.input + "  (not connected)", 9);
    }
    midiInput.setSelectedId (inputId, juce::dontSendNotification);
    fromPage.setToggleState (m.fromMidiPage, juce::dontSendNotification);
    midiChannel.setSelectedId (m.channel + 1, juce::dontSendNotification);
    sceneCc.setSelectedId (m.cc[(size_t) scenes.getSelected()] + 2, juce::dontSendNotification);
    goProgram.setSelectedId (m.goProgram + 1, juce::dontSendNotification);
    learnCc.setToggleState (scenes.getLearning() == SceneMemory::Learn::sceneCc, juce::dontSendNotification);
    learnGo.setToggleState (scenes.getLearning() == SceneMemory::Learn::goProgram, juce::dontSendNotification);
    repaint (midiBox);
}

void SceneMemoryPanel::timerCallback()
{
    // The console's arrows and Go change the selection too (and so can MIDI).
    if (scenes.getRevision() != shownRevision)
        refresh();
    if (++deviceCheck >= 8)   // every 2 s: MIDI inputs plugged in or out
    {
        deviceCheck = 0;
        juce::StringArray now;
        for (auto& d : juce::MidiInput::getAvailableDevices())
            now.add (d.identifier);
        if (now != shownDevices)
            fillInputs();
    }
}

void SceneMemoryPanel::resized()
{
    auto area = getLocalBounds().reduced (14);

    // MIDI control along the bottom.
    midiBox = area.removeFromBottom (82);
    area.removeFromBottom (10);
    auto rows = midiBox.reduced (10, 8).withTrimmedTop (14);
    auto row1 = rows.removeFromTop (26);
    rows.removeFromTop (6);
    auto row2 = rows.removeFromTop (26);
    midiLabelArea = row1.removeFromLeft (52);
    midiInput.setBounds (row1.removeFromLeft (180));
    row1.removeFromLeft (8);
    fromPage.setBounds (row1.removeFromLeft (130));
    row1.removeFromLeft (8);
    channelLabelArea = row1.removeFromLeft (40);
    midiChannel.setBounds (row1.removeFromLeft (70));
    ccLabelArea = row2.removeFromLeft (52);
    sceneCc.setBounds (row2.removeFromLeft (110));
    row2.removeFromLeft (4);
    learnCc.setBounds (row2.removeFromLeft (64));
    row2.removeFromLeft (16);
    goLabelArea = row2.removeFromLeft (30);
    goProgram.setBounds (row2.removeFromLeft (124));
    row2.removeFromLeft (4);
    learnGo.setBounds (row2.removeFromLeft (64));

    auto side = area.removeFromRight (150);
    area.removeFromRight (14);
    list.setBounds (area);

    go.setBounds (side.removeFromTop (34));
    side.removeFromTop (8);
    store.setBounds (side.removeFromTop (34));
    side.removeFromTop (8);
    auto pair = side.removeFromTop (34);
    prev.setBounds (pair.removeFromLeft (pair.getWidth() / 2 - 4));
    pair.removeFromLeft (8);
    next.setBounds (pair);
    side.removeFromTop (10);
    fadeKnob.setBounds (side.removeFromTop (64).withSizeKeepingCentre (72, 64));
    side.removeFromTop (8);
    titleLabelArea = side.removeFromTop (16);
    title.setBounds (side.removeFromTop (30));
    side.removeFromTop (10);
    status.setBounds (side.removeFromTop (90));
}

void SceneMemoryPanel::paint (juce::Graphics& g)
{
    g.fillAll (room);
    g.setColour (legend.withAlpha (0.6f));
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    g.drawText ("TITLE", titleLabelArea, juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xff2a2d30));
    g.drawRoundedRectangle (midiBox.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (legend.withAlpha (0.6f));
    g.drawText ("MIDI CONTROL", midiBox.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft, false);
    g.drawText ("INPUT", midiLabelArea, juce::Justification::centredLeft, false);
    g.drawText ("CH", channelLabelArea.withTrimmedLeft (8), juce::Justification::centredLeft, false);
    g.drawText (SceneMemory::number (scenes.getSelected()), ccLabelArea, juce::Justification::centredLeft, false);
    g.drawText ("GO", goLabelArea, juce::Justification::centredLeft, false);
}
