#include "TapeMachineWindow.h"
#include "ConsoleLookAndFeel.h"
#include "EraseDialog.h"

using namespace ConsoleColours;

namespace
{
    const juce::Colour panelColour { 0xff151617 };
    const juce::Colour silk { 0xffdfe1e1 };
    const juce::Colour logoRed { 0xffe0563a };

    /** A thin white frame with its name printed into the bottom edge, as on the machine's panel. */
    void paintFrame (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& name)
    {
        const auto frame = box.toFloat();
        g.setColour (silk.withAlpha (0.75f));
        g.drawRoundedRectangle (frame, 3.0f, 1.0f);
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 14.0f;
        const auto chip = juce::Rectangle<float> (frame.getCentreX() - w / 2, frame.getBottom() - 6.0f, w, 12.0f);
        g.setColour (panelColour);
        g.fillRect (chip);
        g.setColour (silk);
        g.drawText (name, chip, juce::Justification::centred, false);
    }
}

//==============================================================================
TapeMachinePanel::TapeMachinePanel (AudioEngine& e)
    : engine (e), tape (e.tape),
      wowButton (*e.tape.wowFlutter, "W&F"),
      pitchKnob (*e.tape.pitch, "Pitch", juce::Colour (0xffd9d6cc)),
      levelKnob (*e.tape.recordLevel, "Level", juce::Colour (0xffd6392d)),
      meters (e.tape)
{
    cassette.projectName().referTo (engine.projectName);
    addAndMakeVisible (cassette);
    addAndMakeVisible (clock);
    clock.setTitle ("Tape clock");

    for (auto* b : { &power, &rew, &ff, &stop, &play, &rec, &erase })
        addAndMakeVisible (b);
    power.setTooltip ("Switches the audio interface on and off (off frees the driver for other programs)");
    rew.setTooltip ("Rewind: winds back to the start, speeding up like a real tape machine (Stop brakes)");
    ff.setTooltip ("Fast forward: winds on to the end of the recording (Stop brakes)");
    rec.setTooltip ("Record enable: armed tracks record while the tape plays");
    erase.setTooltip ("Erase: whole tracks, or a section of them");
    power.onClick = [this] { engine.setPower (! engine.isPowered()); };
    rew.onClick   = [this] { tape.rewind(); };
    ff.onClick    = [this] { tape.fastForward(); };
    stop.onClick  = [this] { tape.stop(); };
    play.onClick  = [this] { tape.play(); };
    rec.onClick   = [this] { tape.setRecordEnabled (! tape.isRecordEnabled()); };
    erase.onClick = [this] { EraseDialog::show (tape); };

    setIn.setTooltip ("Set the In point to the current tape position");
    setOut.setTooltip ("Set the Out point to the current tape position");
    loopButton.setTooltip ("Loop between In and Out");
    punchButton.setTooltip ("Auto-punch: record armed tracks only between In and Out (you hear the old take outside)");
    wowButton.setTooltip ("Wow and flutter: subtle tape speed wobble, smaller at higher speeds");
    setIn.onClick  = [this] { tape.setInPoint (tape.getPosition()); };
    setOut.onClick = [this] { tape.setOutPoint (tape.getPosition()); };
    loopButton.onClick  = [this] { tape.loopOn = ! tape.loopOn.load(); };
    punchButton.onClick = [this] { tape.punchOn = ! tape.punchOn.load(); };
    loopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a8fd6));
    punchButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
    punchButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    for (auto* b : { &setIn, &setOut, &loopButton, &punchButton, (juce::TextButton*) &wowButton })
        addAndMakeVisible (b);

    for (int i = 0; i < 3; ++i)
    {
        auto* b = speedButtons.add (new RadioParamButton (*tape.speed, i, TapeMachine::speeds[i].name));
        b->setTooltip (juce::String (TapeMachine::speeds[i].name) + " inches per second");
        addAndMakeVisible (b);
    }
    pitchKnob.setEndLabels ("-", "+");
    pitchKnob.setFormatter ([] (float pc) { return std::abs (pc) < 0.05f ? juce::String ("Pitch") : (pc > 0 ? "+" : "") + juce::String (pc, 1) + " %"; });
    levelKnob.setEndLabels ("-12", "+18");
    addAndMakeVisible (pitchKnob);
    addAndMakeVisible (levelKnob);

    addAndMakeVisible (meters);
    setSize (designW, designH);
    startTimerHz (30);
}

void TapeMachinePanel::timerCallback()
{
    if (! isShowing())
        return;
    updateTapeDisplay (tape, cassette, clock);
    power.setToggleState (engine.isPowered(), juce::dontSendNotification);
    play.setToggleState (tape.isPlaying(), juce::dontSendNotification);
    rew.setToggleState (tape.getWindDirection() < 0, juce::dontSendNotification);
    ff.setToggleState (tape.getWindDirection() > 0, juce::dontSendNotification);
    rec.setToggleState (tape.isRecordEnabled(), juce::dontSendNotification);
    loopButton.setToggleState (tape.loopOn.load(), juce::dontSendNotification);
    punchButton.setToggleState (tape.punchOn.load(), juce::dontSendNotification);
}

void TapeMachinePanel::resized()
{
    // Left panel, x 20..320.
    power.setBounds (leftW - 20 - 100, 10, 100, 26);

    wellBox = { 20, 48, 300, 204 };
    cassette.setBounds (wellBox.withSizeKeepingCentre (Cassette::width, Cassette::height));

    transportBox = { 20, 266, 300, 170 };
    auto t = transportBox.reduced (12, 0);
    t.removeFromTop (12);
    clock.setBounds (t.removeFromTop (44));
    t.removeFromTop (10);
    auto keys = t.removeFromTop (TransportKey::labelHeight + 32);
    const int gap = 5, kw = (keys.getWidth() - 5 * gap) / 6;
    for (auto* b : { &rew, &ff, &stop, &play, &rec, &erase })
    {
        b->setBounds (b == &erase ? keys : keys.removeFromLeft (kw));
        keys.removeFromLeft (gap);
    }
    t.removeFromTop (12);
    auto loc = t.removeFromTop (26);
    const int lw = (loc.getWidth() - 4 * 4) / 5;
    for (auto* b : { &setIn, &setOut, &loopButton, &punchButton, (juce::TextButton*) &wowButton })
    {
        b->setBounds (loc.removeFromLeft (lw));
        loc.removeFromLeft (4);
    }

    controlsBox = { 20, 452, 300, 124 };
    auto c = controlsBox.reduced (12, 0);
    c.removeFromTop (14);
    auto speedRow = c.removeFromTop (26);
    const int sw = (speedRow.getWidth() - 2 * 4) / 3;
    for (auto* b : speedButtons)
    {
        b->setBounds (speedRow.removeFromLeft (sw));
        speedRow.removeFromLeft (4);
    }
    c.removeFromTop (8);
    auto knobs = c.removeFromTop (Knob::preferredHeight + 10);
    pitchKnob.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
    levelKnob.setBounds (knobs.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));

    ventBox = { 20, 594, 300, 76 };

    meters.setBounds (leftW, 0, MeterBridge::designW, MeterBridge::designH);
}

void TapeMachinePanel::paint (juce::Graphics& g)
{
    // The same black panel as the meter section, with its faint top light and grain.
    const auto all = getLocalBounds().toFloat();
    g.fillAll (panelColour);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222426), 0, 0, panelColour, 0, 120, false));
    g.fillRect (all.withHeight (120));
    for (float y = 0; y < all.getHeight(); y += 3.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (((int) y / 3) % 2 == 0 ? 0.012f : 0.0f));
        g.fillRect (0.0f, y, (float) leftW, 1.0f);
    }

    // The machine's name.
    g.setColour (logoRed);
    g.setFont (ConsoleLookAndFeel::uiFont (22.0f, true));
    const auto nameFont = ConsoleLookAndFeel::uiFont (22.0f, true);
    const int oxideW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (nameFont, "OXIDE")) + 8;
    g.drawText ("OXIDE", 20, 10, oxideW, 26, juce::Justification::centredLeft, false);
    g.setColour (silk);
    g.drawText ("3216", 20 + oxideW, 10, 80, 26, juce::Justification::centredLeft, false);

    // The cassette well: a recessed window in the panel.
    const auto well = wellBox.toFloat();
    g.setColour (juce::Colour (0xff070808));
    g.fillRoundedRectangle (well, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (well.reduced (0.5f), 5.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (well.getX() + 5.0f, well.getBottom() + 0.5f, well.getRight() - 5.0f, well.getBottom() + 0.5f, 1.0f);

    paintFrame (g, transportBox, "TRANSPORT");
    paintFrame (g, controlsBox, "TAPE SPEED  |  PITCH  |  RECORD LEVEL");

    // Cooling vents.
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    for (int row = 0; row < 4; ++row)
        for (float x = (float) ventBox.getX() + 4.0f; x + 22.0f <= (float) ventBox.getRight(); x += 28.0f)
        {
            const float y = (float) ventBox.getY() + 8.0f + (float) row * 16.0f;
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (x, y, 22.0f, 6.0f, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (x + 2.0f, y + 6.0f, 18.0f, 1.0f);
        }

    // A groove between the transport and the meters.
    g.setColour (juce::Colours::black);
    g.fillRect ((float) leftW - 1.0f, 48.0f, 2.0f, all.getHeight() - 70.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect ((float) leftW + 1.0f, 48.0f, 1.0f, all.getHeight() - 70.0f);
}

//==============================================================================
/** Holds the panel at its design size and scales it to fit the window. */
class TapeMachineWindow::Content : public juce::Component
{
public:
    explicit Content (AudioEngine& e) : panel (e)
    {
        addAndMakeVisible (panel);
        setSize (TapeMachinePanel::designW * 9 / 10, TapeMachinePanel::designH * 9 / 10);
    }

    void paint (juce::Graphics& g) override { g.fillAll (panelColour); }

    void resized() override
    {
        const float s = juce::jmin ((float) getWidth() / TapeMachinePanel::designW, (float) getHeight() / TapeMachinePanel::designH);
        panel.setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - s * TapeMachinePanel::designW) * 0.5f,
                                                                         (getHeight() - s * TapeMachinePanel::designH) * 0.5f));
    }

    TapeMachinePanel panel;
};

TapeMachineWindow::TapeMachineWindow (AudioEngine& e, juce::PropertiesFile& s, std::function<void()> save)
    : juce::DocumentWindow ("Tape machine", panelColour, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      settings (s), saveSettings (std::move (save))
{
    setUsingNativeTitleBar (true);
    content = new Content (e);
    setContentOwned (content, true);
    constrainer.setFixedAspectRatio ((double) TapeMachinePanel::designW / TapeMachinePanel::designH);
    constrainer.setSizeLimits (TapeMachinePanel::designW / 2, TapeMachinePanel::designH / 2, TapeMachinePanel::designW * 3, TapeMachinePanel::designH * 3);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto state = settings.getValue ("tapeMachineWindow");
    if (state.isEmpty() || ! restoreWindowStateFromString (state))
        centreWithSize (getWidth(), getHeight());
}

void TapeMachineWindow::setCassetteAnimated (bool a)
{
    content->panel.setCassetteAnimated (a);
}

void TapeMachineWindow::show()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
}

void TapeMachineWindow::closeButtonPressed()
{
    settings.setValue ("tapeMachineWindow", getWindowStateAsString());
    if (saveSettings)
        saveSettings();
    setVisible (false);
}

void TapeMachineWindow::visibilityChanged()
{
    juce::DocumentWindow::visibilityChanged();
    if (onShownChanged)
        onShownChanged (isVisible());
}
