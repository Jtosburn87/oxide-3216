#include "EraseDialog.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

juce::Component::SafePointer<juce::DialogWindow> EraseDialog::window;

void EraseDialog::show (TapeMachine& tape)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new EraseDialog (tape));
    o.dialogTitle = "Erase tape";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    window = o.launchAsync();
}

EraseDialog::EraseDialog (TapeMachine& t) : tape (t)
{
    const auto red = juce::Colour (0xffd0473c);
    for (int i = 0; i < TapeMachine::numTracks; ++i)
    {
        auto& b = trackButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setTitle ("Track " + juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, red);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.onClick = [this] { refresh(); };
        addAndMakeVisible (b);
    }
    const auto selectWhere = [this] (auto&& pick)
    {
        for (int i = 0; i < TapeMachine::numTracks; ++i)
            trackButtons[(size_t) i].setToggleState (pick (i), juce::dontSendNotification);
        refresh();
    };
    allButton.onClick   = [selectWhere] { selectWhere ([] (int) { return true; }); };
    noneButton.onClick  = [selectWhere] { selectWhere ([] (int) { return false; }); };
    armedButton.onClick = [this, selectWhere] { selectWhere ([this] (int i) { return tape.arm[(size_t) i]->on(); }); };
    armedButton.setTooltip ("Select the tracks that are armed for recording");
    for (auto* b : { &allButton, &noneButton, &armedButton })
        addAndMakeVisible (b);

    wholeButton.setRadioGroupId (1);
    sectionButton.setRadioGroupId (1);
    for (auto* b : { &wholeButton, &sectionButton })
    {
        b->setColour (juce::ToggleButton::textColourId, legend);
        b->onClick = [this] { refresh(); };
        addAndMakeVisible (b);
    }

    for (auto* l : { &inLabel, &outLabel })
    {
        l->setColour (juce::Label::textColourId, legend);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }
    for (auto* f : { &inField, &outField })
    {
        f->setJustification (juce::Justification::centred);
        f->setFont (ConsoleLookAndFeel::uiFont (15.0f));
        f->setTooltip ("Tape time as minutes:seconds, e.g. 1:23.5");
        f->onReturnKey = f->onFocusLost = [this, f]
        {
            juce::int64 p;
            auto& target = f == &inField ? inPos : outPos;
            if (parseTime (f->getText(), p))
                target = p;
            f->setText (formatTime (target), false);   // tidy what was typed, or put back the last good value
            refresh();
        };
        addAndMakeVisible (f);
    }
    locatorsButton.onClick = [this] { setRange (tape.getInPoint(), tape.getOutPoint()); };
    addAndMakeVisible (locatorsButton);

    hint.setColour (juce::Label::textColourId, legend.withAlpha (0.6f));
    hint.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    addAndMakeVisible (hint);

    eraseButton.setColour (juce::TextButton::buttonColourId, red.darker (0.3f));
    eraseButton.onClick = [this] { confirm(); };
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (eraseButton);
    addAndMakeVisible (cancelButton);

    // Start from the locators when they mark a section, otherwise from whole tracks.
    setRange (tape.getInPoint(), tape.getOutPoint());
    (tape.hasRegion() ? sectionButton : wholeButton).setToggleState (true, juce::dontSendNotification);
    setSize (500, 290);
    refresh();
}

std::vector<int> EraseDialog::selectedTracks() const
{
    std::vector<int> tracks;
    for (int i = 0; i < TapeMachine::numTracks; ++i)
        if (trackButtons[(size_t) i].getToggleState())
            tracks.push_back (i);
    return tracks;
}

void EraseDialog::setRange (juce::int64 in, juce::int64 out)
{
    inPos = juce::jmax ((juce::int64) 0, in);
    outPos = juce::jmax ((juce::int64) 0, out);
    inField.setText (formatTime (inPos), false);
    outField.setText (formatTime (outPos), false);
    refresh();
}

void EraseDialog::refresh()
{
    const bool section = sectionButton.getToggleState();
    for (auto* c : { (juce::Component*) &inField, (juce::Component*) &outField, (juce::Component*) &inLabel,
                     (juce::Component*) &outLabel, (juce::Component*) &locatorsButton })
        c->setEnabled (section);

    // Empty tracks are dimmed (they can still be picked).
    for (int i = 0; i < TapeMachine::numTracks; ++i)
        trackButtons[(size_t) i].setColour (juce::TextButton::textColourOffId,
                                            tape.trackHasAudio (i) ? juce::Colour (0xffdfe2e3) : juce::Colour (0xffdfe2e3).withAlpha (0.4f));

    const auto tracks = selectedTracks();
    juce::String text;
    if (tracks.empty())
        text = "Pick the tracks to erase.";
    else if (section && ! rangeValid())
        text = "The Out time must be after the In time.";
    else if (section)
        text = "Erases " + formatTime (inPos) + " to " + formatTime (outPos) + " (" + juce::String ((double) (outPos - inPos) / tape.getSampleRate(), 1) + " s).";
    else
        text = "Erases everything on the chosen tracks.";
    hint.setText (text, juce::dontSendNotification);
    eraseButton.setEnabled (! tracks.empty() && (! section || rangeValid()));
}

void EraseDialog::confirm()
{
    const auto tracks = selectedTracks();
    if (tracks.empty())
        return;

    juce::StringArray names;
    for (int t : tracks)
        names.add (juce::String (t + 1));
    const auto which = (tracks.size() == 1 ? "track " : "tracks ") + names.joinIntoString (", ");
    const bool section = sectionButton.getToggleState();
    const auto what = section ? "Erase " + formatTime (inPos) + " to " + formatTime (outPos) + " on " + which + "?"
                              : "Erase " + which + " completely?";

    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Are you sure?",
                                                                 what + "\n\nThis can't be undone.", "Erase", "Cancel", this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<EraseDialog> (this), tracks, section] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;
        auto& tape = safe->tape;
        tape.stop();
        if (! section && (int) tracks.size() == TapeMachine::numTracks)
            tape.eraseAll();
        else
            for (int t : tracks)
            {
                if (section)
                    tape.eraseSection (t, safe->inPos, safe->outPos);
                else
                    tape.eraseTrack (t);
            }
        safe->close();
    });
}

void EraseDialog::close()
{
    if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
        w->exitModalState (0);
}

juce::String EraseDialog::formatTime (juce::int64 samples) const
{
    const double sr = tape.getSampleRate() > 0 ? tape.getSampleRate() : 48000.0;
    const auto hundredths = (juce::int64) std::llround ((double) samples / sr * 100.0);
    return juce::String::formatted ("%02d:%02d.%02d", (int) (hundredths / 6000), (int) ((hundredths / 100) % 60), (int) (hundredths % 100));
}

bool EraseDialog::parseTime (const juce::String& text, juce::int64& samples) const
{
    // "m:ss.ss", "h:mm:ss" or plain seconds.
    auto parts = juce::StringArray::fromTokens (text.trim(), ":", {});
    parts.removeEmptyStrings();
    if (parts.isEmpty() || parts.size() > 3)
        return false;
    double seconds = 0.0;
    for (auto& p : parts)
    {
        if (! p.trim().containsOnly ("0123456789."))
            return false;
        seconds = seconds * 60.0 + p.trim().getDoubleValue();
    }
    samples = (juce::int64) std::llround (seconds * tape.getSampleRate());
    return true;
}

void EraseDialog::paint (juce::Graphics& g)
{
    g.fillAll (room);
    g.setColour (legend);
    g.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
    const int x = trackButtons[0].getX(), w = getWidth() - 2 * x;
    g.drawText ("Tracks", x, trackButtons[0].getY() - 20, w, 18, juce::Justification::centredLeft, false);
    g.drawText ("What to erase", x, wholeButton.getY() - 20, w, 18, juce::Justification::centredLeft, false);
}

void EraseDialog::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    area.removeFromTop (20);   // "Tracks"

    // Two rows of eight track buttons, then All / None / Armed.
    const int cols = 8, gap = 4, bw = (area.getWidth() - (cols - 1) * gap) / cols;
    for (int row = 0; row < 2; ++row)
    {
        auto r = area.removeFromTop (30);
        for (int c = 0; c < cols; ++c)
        {
            trackButtons[(size_t) (row * cols + c)].setBounds (r.removeFromLeft (bw));
            r.removeFromLeft (gap);
        }
        area.removeFromTop (4);
    }
    area.removeFromTop (4);
    auto quick = area.removeFromTop (26);
    for (auto* b : { &allButton, &noneButton, &armedButton })
    {
        b->setBounds (quick.removeFromLeft (70));
        quick.removeFromLeft (gap);
    }
    area.removeFromTop (14);

    area.removeFromTop (20);   // "What to erase"
    wholeButton.setBounds (area.removeFromTop (26).removeFromLeft (200));
    auto sec = area.removeFromTop (30);
    sectionButton.setBounds (sec.removeFromLeft (90));
    inLabel.setBounds (sec.removeFromLeft (28));
    sec.removeFromLeft (4);
    inField.setBounds (sec.removeFromLeft (84).reduced (0, 2));
    outLabel.setBounds (sec.removeFromLeft (36));
    sec.removeFromLeft (4);
    outField.setBounds (sec.removeFromLeft (84).reduced (0, 2));
    auto loc = area.removeFromTop (28);
    locatorsButton.setBounds (loc.withTrimmedLeft (90).removeFromLeft (220).reduced (0, 2));

    auto buttons = area.removeFromBottom (30);
    cancelButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    eraseButton.setBounds (buttons.removeFromRight (100));
    hint.setBounds (buttons);
}
