#include "MidiControlUI.h"
#include "ConsoleLookAndFeel.h"
#include <juce_audio_devices/juce_audio_devices.h>

using namespace ConsoleColours;

//==============================================================================
class MidiBindingList::Row : public juce::Component
{
public:
    explicit Row (MidiBindingList& o) : owner (o)
    {
        learn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
        learn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        learn.setTooltip ("Learn: press the MIDI key or move the controller you want here (click again to stop)");
        clear.setTooltip ("Take the MIDI off this");
        learn.onClick = [this] { if (owner.rows.learn) owner.rows.learn (row); owner.refresh(); };
        clear.onClick = [this] { if (owner.rows.clear) owner.rows.clear (row); owner.refresh(); };
        addAndMakeVisible (learn);
        addAndMakeVisible (clear);
    }

    void update (int r)
    {
        row = r;
        header = owner.rows.isHeader && owner.rows.isHeader (r);
        learn.setVisible (! header);
        clear.setVisible (! header);
        if (! header)
        {
            const auto b = owner.rows.binding ? owner.rows.binding (r) : MidiBinding();
            learning = owner.rows.isLearning && owner.rows.isLearning (r);
            learn.setToggleState (learning, juce::dontSendNotification);
            clear.setEnabled (b.isSet());
            bindingText = learning ? juce::String ("press or move it...") : b.text();
            hasBinding = b.isSet();
        }
        name = owner.rows.name ? owner.rows.name (r) : juce::String();
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 3);
        clear.setBounds (r.removeFromRight (26));
        r.removeFromRight (4);
        learn.setBounds (r.removeFromRight (58));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff191b1c));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        if (header)
        {
            g.setColour (legend.withAlpha (0.7f));
            g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
            g.drawText (name.toUpperCase(), getLocalBounds().reduced (8, 0), juce::Justification::bottomLeft, true);
            return;
        }
        const int w = getWidth() - 100;
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f));
        g.drawText (name, 14, 0, w * 3 / 5 - 14, getHeight(), juce::Justification::centredLeft, true);
        g.setColour (learning ? juce::Colour (0xffff6a3d) : hasBinding ? amber : legend.withAlpha (0.4f));
        g.drawText (bindingText, w * 3 / 5, 0, w * 2 / 5, getHeight(), juce::Justification::centredLeft, true);
    }

private:
    MidiBindingList& owner;
    juce::TextButton learn { "Learn" }, clear { juce::CharPointer_UTF8 ("\xe2\x9c\x95") };
    int row = 0;
    bool header = false, learning = false, hasBinding = false;
    juce::String name, bindingText;
};

MidiBindingList::MidiBindingList (Rows r) : rows (std::move (r))
{
    list.setModel (this);
    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0e0f10));
    list.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2a2d30));
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
}

MidiBindingList::~MidiBindingList()
{
    list.setModel (nullptr);
}

void MidiBindingList::refresh()
{
    list.updateContent();
    for (int r = 0; r < getNumRows(); ++r)
        if (auto* c = dynamic_cast<Row*> (list.getComponentForRowNumber (r)))
            c->update (r);
}

juce::Component* MidiBindingList::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
    {
        delete existing;
        return nullptr;
    }
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }
    r->update (row);
    return r;
}

//==============================================================================
namespace
{
    const juce::Colour windowBg { 0xff26292c };

    juce::String deviceName (const juce::String& id)
    {
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (d.identifier == id)
                return d.name;
        return id + "  (not connected)";
    }
}

class ShuttleWindow::Content : public juce::Component
{
public:
    Content (TapeMachine& tape, ShuttleMidi& m)
        : panel (tape, true), midi (m),
          list ({ [] { return (int) ShuttleMidi::numControls; },
                  [] (int r) { return ShuttleMidi::name ((ShuttleMidi::Control) r) + (ShuttleMidi::isContinuous ((ShuttleMidi::Control) r) ? "  (controller)" : ""); },
                  {},
                  [&m] (int r) { return m.get ((ShuttleMidi::Control) r); },
                  [&m] (int r) { return m.getLearning() == r; },
                  [&m] (int r) { m.learn (m.getLearning() == r ? -1 : r); },
                  [&m] (int r) { m.set ((ShuttleMidi::Control) r, {}); } })
    {
        addAndMakeVisible (panel);

        input.setTooltip ("The MIDI input the Jog / shuttle controls listen to");
        input.onChange = [this]
        {
            const int id = input.getSelectedId();
            if (id == 1)       midi.setInput ({});
            else if (id == 2)  midi.setInput (ShuttleMidi::allInputs);
            else if (id >= 10) midi.setInput (devices[id - 10]);
        };
        channel.addItem ("Any", 1);
        for (int c = 1; c <= 16; ++c)
            channel.addItem (juce::String (c), c + 1);
        channel.onChange = [this] { midi.setChannel (channel.getSelectedId() - 1); };
        encoder.addItem ("Relative (1-63 forward, 65-127 back)", 1);
        encoder.addItem ("Relative around 64 (65+ forward)", 2);
        encoder.addItem ("Absolute (0-127)", 3);
        encoder.setTooltip ("How your jog wheel's controller counts: most jog wheels send relative values");
        encoder.onChange = [this] { midi.setEncoder ((ShuttleMidi::Encoder) (encoder.getSelectedId() - 1)); };
        for (auto* c : { &input, &channel, &encoder })
            addAndMakeVisible (c);
        addAndMakeVisible (list);
        fillInputs();
        setSize (ShuttlePanel::copyW + 24, ShuttlePanel::designH + 24 + 104 + 10 + 300);
    }

    void fillInputs()
    {
        devices.clear();
        input.clear (juce::dontSendNotification);
        input.addItem ("None", 1);
        input.addItem ("All MIDI inputs", 2);
        const auto list = juce::MidiInput::getAvailableDevices();
        for (int i = 0; i < list.size(); ++i)
        {
            devices.add (list[i].identifier);
            input.addItem (list[i].name, 10 + i);
        }
        refresh();
    }

    void refresh()
    {
        int id = midi.input.isEmpty() ? 1 : midi.input == ShuttleMidi::allInputs ? 2 : 10 + devices.indexOf (midi.input);
        if (id == 9)
        {
            if (input.indexOfItemId (9) < 0)
                input.addItem (deviceName (midi.input), 9);
        }
        input.setSelectedId (id, juce::dontSendNotification);
        channel.setSelectedId (midi.channel + 1, juce::dontSendNotification);
        encoder.setSelectedId ((int) midi.encoder + 1, juce::dontSendNotification);
        list.refresh();
    }

    juce::StringArray currentDevices() const
    {
        juce::StringArray ids;
        for (auto& d : juce::MidiInput::getAvailableDevices())
            ids.add (d.identifier);
        return ids;
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        panel.setBounds (r.removeFromTop (ShuttlePanel::designH).withWidth (ShuttlePanel::copyW));
        r.removeFromTop (10);
        midiBox = r;
        auto inner = r.reduced (10, 8).withTrimmedTop (16);
        auto row = inner.removeFromTop (26);
        inputLabel = row.removeFromLeft (44);
        input.setBounds (row.removeFromLeft (200));
        row.removeFromLeft (10);
        channelLabel = row.removeFromLeft (26);
        channel.setBounds (row.removeFromLeft (64));
        inner.removeFromTop (6);
        row = inner.removeFromTop (26);
        encoderLabel = row.removeFromLeft (110);
        encoder.setBounds (row.removeFromLeft (260));
        inner.removeFromTop (8);
        list.setBounds (inner);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (windowBg);
        g.setColour (juce::Colour (0xff2a2d30));
        g.drawRoundedRectangle (midiBox.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (legend.withAlpha (0.6f));
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        g.drawText ("MIDI CONTROL", midiBox.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft, false);
        g.drawText ("INPUT", inputLabel, juce::Justification::centredLeft, false);
        g.drawText ("CH", channelLabel.withTrimmedLeft (4), juce::Justification::centredLeft, false);
        g.drawText ("JOG WHEEL CC", encoderLabel, juce::Justification::centredLeft, false);
    }

    ShuttlePanel panel;
    juce::StringArray devices;

private:
    ShuttleMidi& midi;
    juce::ComboBox input, channel, encoder;
    MidiBindingList list;
    juce::Rectangle<int> midiBox, inputLabel, channelLabel, encoderLabel;
};

ShuttleWindow::ShuttleWindow (TapeMachine& tape, ShuttleMidi& m, juce::PropertiesFile& s, std::function<void()> save)
    : juce::DocumentWindow ("Jog / shuttle", windowBg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      midi (m), settings (s), saveSettings (std::move (save))
{
    setUsingNativeTitleBar (true);
    content = new Content (tape, m);
    setContentOwned (content, true);
    setResizable (false, false);
    const auto state = settings.getValue ("shuttleWindow");
    if (state.isEmpty() || ! restoreWindowStateFromString (state))
        centreWithSize (getWidth(), getHeight());
    startTimerHz (4);
}

ShuttleWindow::~ShuttleWindow()
{
    midi.learn (-1);
}

void ShuttleWindow::show()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
}

void ShuttleWindow::closeButtonPressed()
{
    midi.learn (-1);
    settings.setValue ("shuttleWindow", getWindowStateAsString());
    if (saveSettings)
        saveSettings();
    setVisible (false);
}

void ShuttleWindow::timerCallback()
{
    if (! isVisible())
        return;
    if (midi.getRevision() != shownRevision)
    {
        shownRevision = midi.getRevision();
        content->refresh();
    }
    if (content->currentDevices() != content->devices)
        content->fillInputs();
}

//==============================================================================
MidiShortcutsEditor::MidiShortcutsEditor (juce::ApplicationCommandManager& commands, MidiShortcuts& s)
    : shortcuts (s),
      list ({ [this] { return (int) items.size(); },
              [this] (int r) { return items[(size_t) r].text; },
              [this] (int r) { return items[(size_t) r].command < 0; },
              [this] (int r) { return shortcuts.get (items[(size_t) r].command); },
              [this] (int r) { return shortcuts.getLearning() == items[(size_t) r].command; },
              [this] (int r) { const int c = items[(size_t) r].command; shortcuts.learn (shortcuts.getLearning() == c ? -1 : c); },
              [this] (int r) { shortcuts.clear (items[(size_t) r].command); } })
{
    for (auto& category : commands.getCommandCategories())
    {
        items.push_back ({ category, -1 });
        for (auto id : commands.getCommandsInCategory (category))
            items.push_back ({ commands.getNameOfCommand (id), id });
    }
    addAndMakeVisible (list);
    startTimerHz (4);
}

MidiShortcutsEditor::~MidiShortcutsEditor()
{
    shortcuts.learn (-1);
}

void MidiShortcutsEditor::timerCallback()
{
    if (shortcuts.getRevision() != shownRevision)
    {
        shownRevision = shortcuts.getRevision();
        list.refresh();
    }
}

void MidiShortcutsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d30));
    g.setColour (legend.withAlpha (0.7f));
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    g.drawText ("A MIDI note or controller for any command, from any MIDI input. Learn, then press the key or move the controller.",
                getLocalBounds().reduced (10, 0).removeFromTop (28), juce::Justification::centredLeft, true);
}

void MidiShortcutsEditor::resized()
{
    list.setBounds (getLocalBounds().withTrimmedTop (28).reduced (6, 0).withTrimmedBottom (6));
}
