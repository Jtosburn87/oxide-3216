#include "SceneWindow.h"
#include "ConsoleLookAndFeel.h"

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

void SceneMemoryPanel::show (SceneMemory& scenes)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SceneMemoryPanel (scenes));
    o.dialogTitle = "Scene Memory";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    window = o.launchAsync();
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

    setSize (540, 470);
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
    g.drawText (stored ? scenes.getTitle (row) : juce::String ("(empty)"), 62, 0, width - 62 - 110, height, juce::Justification::centredLeft, true);
    if (stored)
    {
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
        status.setText ("Stored the console's faders and knobs in " + SceneMemory::number (s) + ".", juce::dontSendNotification);
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
    status.setText (text, juce::dontSendNotification);
    shownRevision = scenes.getRevision();
    list.repaint();
}

void SceneMemoryPanel::timerCallback()
{
    // The console's arrows and Go change the selection too.
    if (scenes.getRevision() != shownRevision)
        refresh();
}

void SceneMemoryPanel::resized()
{
    auto area = getLocalBounds().reduced (14);
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
}
