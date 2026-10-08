#include "Desk.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;
using namespace DeskLayout;

namespace
{
    const juce::Colour muteRed { 0xffd0473c };

    juce::String formatHz (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (f));
    }

    void paintSection (juce::Graphics& g, const juce::String& title, juce::Rectangle<int> box, juce::Colour background)
    {
        g.setColour (juce::Colour (0xff8e9598));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 2.0f, 1.0f);

        g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
        const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 6;
        const auto label = juce::Rectangle<int> (box.getX() + 6, box.getY() - 6, w, 12);
        g.setColour (background);
        g.fillRect (label);
        g.setColour (legend);
        g.drawText (title, label, juce::Justification::centred, false);
    }

    void setupScribble (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::backgroundColourId, paper);
        l.setColour (juce::Label::textColourId, juce::Colour (0xff111111));
        l.setColour (juce::Label::outlineColourId, juce::Colours::black.withAlpha (0.35f));
        l.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
        l.setColour (juce::TextEditor::textColourId, juce::Colour (0xff111111));
        l.setColour (juce::TextEditor::highlightColourId, amber.withAlpha (0.5f));
        l.setColour (juce::TextEditor::focusedOutlineColourId, focus);
    }

    void paintColumn (juce::Graphics& g, juce::Rectangle<int> r)
    {
        // Bridge cell.
        g.setColour (juce::Colour (0xff09090a));
        g.fillRect (r.withHeight (bridgeH));
        g.setColour (juce::Colours::black);
        g.fillRect (r.getX(), bridgeH - 2, r.getWidth(), 2);
        g.setColour (juce::Colour (0xff2a2d2f));
        g.fillRect (r.getX(), bridgeH - 3, r.getWidth(), 1);

        // Mid-grey knob area.
        g.setColour (stripGrey);
        g.fillRect (r.getX(), bridgeH, r.getWidth(), knobH);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRect (r.getX(), bridgeH, r.getWidth(), 1);

        // Black fader area.
        g.setColour (faderArea);
        g.fillRect (r.getX(), bridgeH + knobH, r.getWidth(), faderH);
        g.setColour (juce::Colours::black);
        g.fillRect (r.getX(), bridgeH + knobH, r.getWidth(), 1);
    }
}

//==============================================================================
StripComponent::StripComponent (ChannelStrip& s, bool ret)
    : strip (s), isReturn (ret),
      trim (*s.controls.trim, "Trim"),
      hpf (*s.controls.hpf, "HPF"),
      pan (*s.controls.pan, "Pan"),
      mainButton (*s.controls.main, "Main"),
      mute (*s.controls.mute, "Mute"),
      solo (*s.controls.solo, "Solo"),
      fader (*s.controls.fader)
{
    hpf.setFormatter ([] (float f) { return f < 20.5f ? juce::String ("off") : formatHz (f); });
    hpf.setEndLabels ("20", "300");
    pan.setEndLabels ("L", "R");

    mute.setColour (juce::TextButton::buttonOnColourId, muteRed);
    mute.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    for (juce::Component* c : { (juce::Component*) &meter, (juce::Component*) &trim, (juce::Component*) &hpf,
                                (juce::Component*) &pan, (juce::Component*) &mainButton, (juce::Component*) &mute,
                                (juce::Component*) &solo, (juce::Component*) &fader, (juce::Component*) &scribble })
        addAndMakeVisible (c);

    setupScribble (scribble, strip.name);
    scribble.setEditable (false, true, false);   // double-click to rename
    scribble.setTooltip ("Double-click to rename");
    scribble.onTextChange = [this] { strip.name = scribble.getText(); };

    fader.slider.setTitle (strip.name + " fader");
    setTitle (strip.name);
    setFocusContainerType (FocusContainerType::focusContainer);
}

void StripComponent::resized()
{
    meter.setBounds (juce::Rectangle<int> (10, 64).withCentre ({ stripW / 2, bridgeH / 2 }));

    sections.clear();
    int y = bridgeH + 12;

    auto box = juce::Rectangle<int> (5, y, stripW - 10, 66);
    sections.push_back ({ "Input", box });
    trim.setBounds (box.getX() + 2, box.getY() + 10, Knob::preferredWidth, Knob::preferredHeight);
    hpf.setBounds (box.getRight() - 2 - Knob::preferredWidth, box.getY() + 10, Knob::preferredWidth, Knob::preferredHeight);
    y = box.getBottom() + 14;

    box = juce::Rectangle<int> (5, y, stripW - 10, 90);
    sections.push_back ({ "Routing", box });
    mainButton.setBounds (box.getX() + 8, box.getY() + 12, box.getWidth() - 16, 20);
    pan.setBounds (juce::Rectangle<int> (Knob::preferredWidth, Knob::preferredHeight).withCentre ({ stripW / 2, box.getY() + 64 }));

    const int f = bridgeH + knobH;
    mute.setBounds (8, f + 8, 45, 20);
    solo.setBounds (stripW - 8 - 45, f + 8, 45, 20);
    scribble.setBounds (4, getHeight() - 28, stripW - 8, 22);
    fader.setBounds (8, f + 36, stripW - 16, scribble.getY() - 6 - (f + 36));
}

void StripComponent::paint (juce::Graphics& g)
{
    paintColumn (g, getLocalBounds());

    for (auto& s : sections)
        paintSection (g, s.title, s.box, stripGrey);

    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    juce::ignoreUnused (isReturn);
}

//==============================================================================
void TapeClock::setTime (double seconds, bool recording)
{
    const auto tenths = (juce::int64) (seconds * 10.0);
    auto t = juce::String::formatted ("%02d:%02d.%d", (int) (tenths / 600), (int) ((tenths / 10) % 60), (int) (tenths % 10));
    if (t != text || recording != rec)
    {
        text = t;
        rec = recording;
        repaint();
    }
}

void TapeClock::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (r);
    g.setColour (juce::Colour (0xff1d1f20));
    g.drawRect (r, 1.0f);

    const juce::Colour led (0xffff6a3d);
    g.setFont (ConsoleLookAndFeel::uiFont (r.getHeight() * 0.78f, true).withExtraKerningFactor (0.08f));
    g.setColour (led.withAlpha (0.25f));
    g.drawText (text, r.translated (0.0f, 0.5f).expanded (1.0f), juce::Justification::centred, false);
    g.setColour (led);
    g.drawText (text, r, juce::Justification::centred, false);

    if (rec)
    {
        g.setColour (muteRed.brighter (0.3f));
        g.fillEllipse (r.getX() + 10.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        g.drawText ("REC", juce::Rectangle<float> (r.getX() + 24.0f, r.getCentreY() - 6.0f, 30.0f, 12.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
CentreSection::CentreSection (AudioEngine& e)
    : engine (e), tape (e.tape),
      recordLevel (*e.tape.recordLevel, "Level", juce::Colour (0xffd6392d)),
      mainFader (*e.mainFader)
{
    addAndMakeVisible (clock);
    clock.setTitle ("Tape clock");

    styleTransport (power, juce::Colour (0xffc9a23a));
    styleTransport (rew, juce::Colour (0xff8a9094));
    styleTransport (stop, juce::Colour (0xffd8dadb));
    styleTransport (play, juce::Colour (0xff3fae3f));
    styleTransport (rec, juce::Colour (0xffc8413a));
    styleTransport (erase, juce::Colour (0xff8a9094));

    power.setTooltip ("Switches the audio interface on and off (off frees the driver for other programs)");
    rew.setTooltip ("Stop and return to the start");
    rec.setTooltip ("Record enable: armed tracks record while the tape plays");
    erase.setTooltip ("Erase all tracks");

    power.onClick = [this] { engine.setPower (! engine.isPowered()); };
    rew.onClick   = [this] { tape.stop(); tape.locate (0); };
    stop.onClick  = [this] { tape.stop(); };
    play.onClick  = [this] { tape.play(); };
    rec.onClick   = [this] { tape.setRecordEnabled (! tape.isRecordEnabled()); };
    erase.onClick = [this] { confirmErase(); };

    recordLevel.setEndLabels ("-12", "+18");
    addAndMakeVisible (recordLevel);

    for (int t = 0; t < TapeMachine::numTracks; ++t)
    {
        auto* b = armButtons.add (new ParamButton (*tape.arm[(size_t) t], juce::String (t + 1)));
        b->setColour (juce::TextButton::buttonOnColourId, muteRed);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setTooltip ("Arm track " + juce::String (t + 1) + ": armed tracks monitor their input and record; others play back");
        addAndMakeVisible (b);
    }

    for (auto& m : mainMeters)
        addAndMakeVisible (m);
    addAndMakeVisible (mainFader);
    mainFader.slider.setTitle ("Main fader");
    setupScribble (mainScribble, "Main");
    addAndMakeVisible (mainScribble);

    setFocusContainerType (FocusContainerType::focusContainer);
}

void CentreSection::styleTransport (juce::TextButton& b, juce::Colour c)
{
    b.setColour (juce::TextButton::buttonColourId, c.interpolatedWith (juce::Colour (0xff222527), 0.78f));
    b.setColour (juce::TextButton::buttonOnColourId, c.interpolatedWith (juce::Colour (0xff222527), 0.12f));
    b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffdfe2e3));
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b0b));
    addAndMakeVisible (b);
}

void CentreSection::confirmErase()
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::WarningIcon, "Erase tape",
        "Erase all " + juce::String (TapeMachine::numTracks) + " tracks? This cannot be undone.",
        "Erase", "Cancel", this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<CentreSection> (this)] (int result)
    {
        if (result == 1 && safe != nullptr)
            safe->tape.eraseAll();
    });
}

void CentreSection::resized()
{
    // Main meters in the bridge, above the main fader.
    const int mainX = centreW - 100;
    mainMeters[0].setBounds (mainX + 34, 10, 12, 64);
    mainMeters[1].setBounds (mainX + 52, 10, 12, 64);

    // Tape machine panel, left column.
    deckBox = { 12, bridgeH + 12, 300, knobH - 24 };
    auto d = deckBox.reduced (10);
    d.removeFromTop (18);   // title
    clock.setBounds (d.removeFromTop (44));
    d.removeFromTop (8);

    auto row = d.removeFromTop (32);
    const int bw = (row.getWidth() - 5 * 4) / 6;
    for (auto* b : { &power, &rew, &stop, &play, &rec, &erase })
    {
        b->setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (4);
    }

    d.removeFromTop (16);
    levelBox = d.removeFromTop (70);
    recordLevel.setBounds (juce::Rectangle<int> (Knob::preferredWidth, Knob::preferredHeight).withCentre (levelBox.getCentre().translated (0, 4)));

    d.removeFromTop (14);
    armBox = d.removeFromTop (54);
    auto grid = armBox.reduced (8, 0).withTrimmedTop (14).withHeight (24);
    const int cols = 8, cw = grid.getWidth() / cols;
    for (int t = 0; t < armButtons.size(); ++t)
        armButtons[t]->setBounds (grid.getX() + (t % cols) * cw + 1, grid.getY() + (t / cols) * 28, cw - 2, 24);

    // Main fader, far right of the fader area.
    const int f = bridgeH + knobH;
    mainScribble.setBounds (mainX + 6, getHeight() - 28, 90, 22);
    mainFader.setBounds (mainX + 8, f + 36, 84, mainScribble.getY() - 6 - (f + 36));
}

void CentreSection::paint (juce::Graphics& g)
{
    paintColumn (g, getLocalBounds());

    // Tape machine panel.
    g.setColour (faderArea);
    g.fillRoundedRectangle (deckBox.toFloat(), 2.0f);
    g.setColour (juce::Colour (0xff2a2d2f));
    g.drawRoundedRectangle (deckBox.toFloat().reduced (0.5f), 2.0f, 1.0f);
    g.setColour (juce::Colour (0xffaeb3b5));
    g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
    g.drawText ("Tape machine, " + juce::String (TapeMachine::numTracks) + " track",
                deckBox.reduced (10).withHeight (16), juce::Justification::centredLeft, false);

    paintSection (g, "Record level", levelBox, faderArea);
    paintSection (g, "Record arm", armBox, faderArea);

    // Divider before the main fader.
    const int mainX = centreW - 100;
    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (mainX, bridgeH + knobH, 1, faderH);

    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void CentreSection::update()
{
    for (int side = 0; side < 2; ++side)
        mainMeters[(size_t) side].setLevel (engine.takeMainPeak (side));

    bool recording = false;
    for (int t = 0; t < TapeMachine::numTracks; ++t)
        recording = recording || tape.isRecording (t);

    const double sr = tape.getSampleRate();
    clock.setTime (sr > 0 ? (double) tape.getPosition() / sr : 0.0, recording);

    power.setToggleState (engine.isPowered(), juce::dontSendNotification);
    play.setToggleState (tape.isPlaying(), juce::dontSendNotification);
    rec.setToggleState (tape.isRecordEnabled(), juce::dontSendNotification);
}

//==============================================================================
Desk::Desk (AudioEngine& e) : engine (e), centre (e)
{
    for (auto* s : engine.inputStrips)
        addAndMakeVisible (inputs.add (new StripComponent (*s, false)));
    addAndMakeVisible (centre);
    for (auto* s : engine.returnStrips)
        addAndMakeVisible (returns.add (new StripComponent (*s, true)));

    setSize (2 * cheekW + (inputs.size() + returns.size()) * stripW + centreW, totalH);
    startTimerHz (30);
}

void Desk::resized()
{
    int x = cheekW;
    for (auto* s : inputs)  { s->setBounds (x, 0, stripW, totalH); x += stripW; }
    centre.setBounds (x, 0, centreW, totalH);
    x += centreW;
    for (auto* s : returns) { s->setBounds (x, 0, stripW, totalH); x += stripW; }
}

void Desk::paint (juce::Graphics& g)
{
    g.fillAll (bridge);
    for (auto cheek : { juce::Rectangle<int> (0, 0, cheekW, getHeight()), juce::Rectangle<int> (getWidth() - cheekW, 0, cheekW, getHeight()) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222324), (float) cheek.getX(), 0.0f,
                                                 juce::Colour (0xff060607), (float) cheek.getX() + cheekW * 0.45f, 0.0f, false));
        g.fillRect (cheek);
        g.setColour (juce::Colour (0xff3b3e41));
        g.fillRect (cheek.getX(), 0, 1, getHeight());
        g.fillRect (cheek.getRight() - 1, 0, 1, getHeight());
    }
}

void Desk::timerCallback()
{
    for (auto* s : inputs)  s->updateMeter();
    for (auto* s : returns) s->updateMeter();
    centre.update();
}
