#include "Desk.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;
using namespace DeskLayout;

namespace
{
    const juce::Colour muteRed  { 0xffd0473c };
    const juce::Colour gateCap  { 0xff3d6b50 };
    const juce::Colour compCap  { 0xffc9c6bb };
    const juce::Colour auxCap   { 0xffd9d6cc };

    juce::String formatHz (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (f));
    }

    /** Range printed at the ends of a frequency knob's sweep: Hz for LF, kHz for the others. */
    std::pair<juce::String, juce::String> freqEnds (const FourBandEq::BandSpec& s)
    {
        if (s.maxHz <= 1000.0f)
            return { juce::String (juce::roundToInt (s.minHz)), juce::String (juce::roundToInt (s.maxHz)) };
        const auto k = [] (float hz) { auto t = juce::String (hz / 1000.0f, 1); return t.endsWith (".0") ? t.dropLastCharacters (2) : t; };
        return { k (s.minHz), k (s.maxHz) };
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

    void redButton (juce::Button& b)
    {
        b.setColour (juce::TextButton::buttonOnColourId, muteRed);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
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

    juce::Rectangle<int> knobAt (int x, int y)      { return { x, y, Knob::preferredWidth, Knob::preferredHeight }; }
    juce::Rectangle<int> smallKnobAt (int x, int y) { return { x, y, Knob::smallWidth, Knob::smallHeight }; }
}

//==============================================================================
StripComponent::StripComponent (ChannelStrip& s)
    : strip (s),
      trim (*s.controls.trim, "Trim"),
      hpf (*s.controls.hpf, "HPF"),
      pan (*s.controls.pan, "Pan"),
      gateThr (*s.gate.threshold, "Thr", gateCap),
      gateRange (*s.gate.range, "Range", gateCap),
      gateFast (*s.gate.fast, "Fast atk"),
      compInput (*s.comp.input, "Input", compCap),
      compOutput (*s.comp.output, "Output", compCap),
      compAttack (*s.comp.attack, "Attack", compCap),
      compRelease (*s.comp.release, "Release", compCap),
      compAll (*s.comp.all, "All"),
      compIn (*s.comp.in, "In"),
      mainButton (*s.controls.main, "Main"),
      mute (*s.controls.mute, "Mute"),
      solo (*s.controls.solo, "Solo"),
      fader (*s.controls.fader)
{
    auto add = [this] (juce::Component& c) { addAndMakeVisible (c); };

    add (meter);

    hpf.setFormatter ([] (float f) { return f < 20.5f ? juce::String ("off") : formatHz (f); });
    hpf.setEndLabels ("20", "300");
    add (trim);
    add (hpf);

    for (int b = 0; b < FourBandEq::numBands; ++b)
    {
        const auto& spec = FourBandEq::bands[b];
        const auto cap = juce::Colour (spec.capColour);
        auto* g = eqKnobs.add (new Knob (*s.eq.gain[b], spec.label, cap));
        g->setEndLabels ("-", "+");
        auto* f = eqKnobs.add (new Knob (*s.eq.freq[b], "freq", cap));
        f->setFormatter ([] (float hz) { return formatHz (hz); });
        const auto [lo, hi] = freqEnds (spec);
        f->setEndLabels (lo, hi);
        add (*g);
        add (*f);
    }

    gateThr.setFormatter ([] (float db) { return db <= -79.0f ? juce::String ("off") : juce::String (juce::roundToInt (db)); });
    gateFast.setTooltip ("Fast attack: opens in 0.1 ms. A loud transient makes the gate pop.");
    gateFast.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9dbd8));
    gateFast.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b0b));
    add (gateThr);
    add (gateRange);
    add (gateFast);

    compInput.setEndLabels ("0", "10");
    compOutput.setEndLabels ("-", "+");
    compAttack.setEndLabels ("1", "7");
    compRelease.setEndLabels ("1", "7");
    for (auto* k : { &compInput, &compOutput, &compAttack, &compRelease })
        add (*k);
    for (int i = 0; i < 4; ++i)
    {
        auto* b = ratioButtons.add (new RadioParamButton (*s.comp.ratio, i, juce::String (juce::roundToInt (FetCompressor::ratios[i]))));
        b->setTooltip ("Ratio " + b->getButtonText() + ":1");
        add (*b);
    }
    compAll.setTooltip ("All buttons in: about 60:1 with extra drive and distortion");
    redButton (compAll);
    compIn.setTooltip ("Compressor in");
    add (compAll);
    add (compIn);
    add (grMeter);

    for (int k = 0; k < Config::numAux; ++k)
    {
        auto* knob = auxKnobs.add (new Knob (*s.controls.auxLevel[k], "A" + juce::String (k + 1), auxCap));
        knob->setDialSize (26);
        auto* p = auxPre.add (new ParamButton (*s.controls.auxPre[k], "P"));
        p->setTooltip ("P = pre-fader send");
        add (*knob);
        add (*p);
    }

    add (mainButton);
    for (int k = 0; k < Config::numSubs; ++k)
    {
        auto* b = subButtons.add (new ParamButton (*s.controls.sub[k], "S" + juce::String (k + 1)));
        b->setTooltip ("Assign to sub " + juce::String (k + 1));
        add (*b);
    }
    pan.setEndLabels ("L", "R");
    add (pan);

    redButton (mute);
    add (mute);
    add (solo);
    add (fader);
    add (scribble);

    setupScribble (scribble, strip.name);
    scribble.setEditable (false, true, false);   // double-click to rename
    scribble.setTooltip ("Double-click to rename");
    scribble.onTextChange = [this] { strip.name = scribble.getText(); };

    fader.slider.setTitle (strip.name + " fader");
    setTitle (strip.name);
    setFocusContainerType (FocusContainerType::focusContainer);
    lastPops = s.gate.popCount.load();
}

void StripComponent::resized()
{
    meter.setBounds (juce::Rectangle<int> (10, 64).withCentre ({ stripW / 2, bridgeH / 2 }));

    sections.clear();
    const int bx = 5, bw = stripW - 10;
    const int left = bx + 2, right = bx + bw - 2 - Knob::preferredWidth;
    int y = bridgeH + 12;

    auto box = juce::Rectangle<int> (bx, y, bw, 66);
    sections.push_back ({ "Input", box });
    trim.setBounds (knobAt (left, box.getY() + 10));
    hpf.setBounds (knobAt (right, box.getY() + 10));
    y = box.getBottom() + 14;

    box = { bx, y, bw, 10 + 4 * 52 + 4 };
    sections.push_back ({ "Equaliser", box });
    for (int b = 0; b < FourBandEq::numBands; ++b)
    {
        eqKnobs[b * 2]->setBounds (knobAt (left, box.getY() + 10 + b * 52));
        eqKnobs[b * 2 + 1]->setBounds (knobAt (right, box.getY() + 10 + b * 52));
    }
    y = box.getBottom() + 14;

    box = { bx, y, bw, 10 + 50 + 4 + 18 + 6 };
    sections.push_back ({ "Gate", box });
    gateThr.setBounds (knobAt (left, box.getY() + 10));
    gateRange.setBounds (knobAt (right, box.getY() + 10));
    gateFast.setBounds (bx + 5, box.getY() + 64, bw - 10, 18);
    y = box.getBottom() + 14;

    box = { bx, y, bw, 10 + 2 * 52 + 2 + 17 + 4 + 16 + 6 };
    sections.push_back ({ "Compressor", box });
    compInput.setBounds (knobAt (left, box.getY() + 10));
    compOutput.setBounds (knobAt (right, box.getY() + 10));
    compAttack.setBounds (knobAt (left, box.getY() + 62));
    compRelease.setBounds (knobAt (right, box.getY() + 62));
    {
        auto row = juce::Rectangle<int> (bx + 4, box.getY() + 116, bw - 8, 17);
        const int w = row.getWidth() / 5;
        for (auto* b : ratioButtons)
            b->setBounds (row.removeFromLeft (w).reduced (1, 0));
        compAll.setBounds (row.reduced (1, 0));

        auto row2 = juce::Rectangle<int> (bx + 4, box.getY() + 137, bw - 8, 16);
        compIn.setBounds (row2.removeFromLeft (30));
        row2.removeFromLeft (4);
        grMeter.setBounds (row2.withSizeKeepingCentre (row2.getWidth(), 8));
    }
    y = box.getBottom() + 14;

    box = { bx, y, bw, 10 + 4 * 44 + 4 };
    sections.push_back ({ "Aux sends", box });
    for (int k = 0; k < Config::numAux; ++k)
    {
        const int col = k % 2, row = k / 2;
        const int x = bx + 2 + col * 48, ky = box.getY() + 8 + row * 44;
        auxKnobs[k]->setBounds (smallKnobAt (x, ky));
        auxPre[k]->setBounds (x + Knob::smallWidth - 1, ky + 7, 12, 15);
    }
    y = box.getBottom() + 14;

    box = { bx, y, bw, 12 + 20 + 4 + 20 + 2 + 50 + 2 };
    sections.push_back ({ "Routing", box });
    mainButton.setBounds (bx + 6, box.getY() + 10, bw - 12, 20);
    {
        auto row = juce::Rectangle<int> (bx + 4, box.getY() + 34, bw - 8, 20);
        const int w = row.getWidth() / Config::numSubs;
        for (auto* b : subButtons)
            b->setBounds (row.removeFromLeft (w).reduced (1, 0));
    }
    pan.setBounds (juce::Rectangle<int> (Knob::preferredWidth, Knob::preferredHeight).withCentre ({ stripW / 2, box.getY() + 82 }));

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
}

void StripComponent::update()
{
    // A loaded session may have renamed the strip.
    if (! scribble.isBeingEdited() && scribble.getText() != strip.name)
        scribble.setText (strip.name, juce::dontSendNotification);

    meter.setLevel (strip.takePeak());
    grMeter.setReduction (strip.comp.isIn() ? strip.comp.takeGainReduction() : 0.0f);

    const auto pops = strip.gate.popCount.load();
    if (pops != lastPops)
    {
        lastPops = pops;
        gateFast.flash();
    }
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
CentreSection::CentreSection (AudioEngine& e, PluginContext* plugins)
    : engine (e), tape (e.tape),
      recordLevel (*e.tape.recordLevel, "Level", juce::Colour (0xffd6392d)),
      wowButton (*e.tape.wowFlutter, "W&F"),
      mainFader (*e.mainFader),
      busPanel (e.busComp),
      inserts (e, plugins)
{
    cassette.projectName().referTo (engine.projectName);
    addAndMakeVisible (cassette);
    addAndMakeVisible (clock);
    clock.setTitle ("Tape clock");

    for (int i = 0; i < 3; ++i)
    {
        auto* b = speedButtons.add (new RadioParamButton (*tape.speed, i, TapeMachine::speeds[i].name));
        b->setTooltip (juce::String (TapeMachine::speeds[i].name) + " inches per second");
        addAndMakeVisible (b);
    }

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

    // Locators: one In/Out region for loop and auto-punch.
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
    punchButton.setColour (juce::TextButton::buttonOnColourId, muteRed);
    punchButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    for (auto* b : { &setIn, &setOut, &loopButton, &punchButton, (juce::TextButton*) &wowButton })
        addAndMakeVisible (b);

    recordLevel.setEndLabels ("-12", "+18");
    addAndMakeVisible (recordLevel);

    for (int t = 0; t < TapeMachine::numTracks; ++t)
    {
        auto* b = armButtons.add (new ParamButton (*tape.arm[(size_t) t], juce::String (t + 1)));
        redButton (*b);
        b->setTooltip ("Arm track " + juce::String (t + 1) + ": armed tracks monitor their input and record; others play back");
        addAndMakeVisible (b);
    }

    for (int k = 0; k < Config::numSubs; ++k)
    {
        for (int b = 0; b < FourBandEq::numBands; ++b)
        {
            const auto& spec = FourBandEq::bands[b];
            const auto cap = juce::Colour (spec.capColour);
            auto* g = groupEqKnobs.add (new Knob (*engine.groupEq[k]->gain[b], spec.label, cap));
            auto* f = groupEqKnobs.add (new Knob (*engine.groupEq[k]->freq[b], "freq", cap));
            g->setEndLabels ("-", "+");
            f->setFormatter ([] (float hz) { return formatHz (hz); });
            const auto [lo, hi] = freqEnds (spec);
            f->setEndLabels (lo, hi);
            addAndMakeVisible (g);
            addAndMakeVisible (f);
        }
    }

    for (auto& pair : subMeters)
        for (auto& m : pair)
            addAndMakeVisible (m);
    for (auto& m : auxMeters)
        addAndMakeVisible (m);
    for (auto& m : mainMeters)
        addAndMakeVisible (m);

    addAndMakeVisible (busPanel);
    addAndMakeVisible (inserts);

    for (int i = 0; i < Config::numSubs + Config::numAux; ++i)
    {
        const bool isSub = i < Config::numSubs;
        const auto name = isSub ? "Sub " + juce::String (i + 1) : "A" + juce::String (i - Config::numSubs + 1);
        auto* f = busFaders.add (new Fader (isSub ? *engine.subFader[(size_t) i] : *engine.auxMaster[(size_t) (i - Config::numSubs)], 16));
        f->slider.getProperties().set ("capWidth", 30);
        f->slider.setTitle (name + (isSub ? " fader" : " master"));
        addAndMakeVisible (f);
        auto* l = busScribbles.add (new juce::Label());
        setupScribble (*l, name);
        l->setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
        addAndMakeVisible (l);
    }

    addAndMakeVisible (mainFader);
    mainFader.slider.setTitle ("Main fader");
    mainFader.slider.getProperties().set ("capWidth", 46);
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
    // Bridge: stereo meters over the sub faders, mono over the aux faders, stereo over main.
    const int my = 8, mh = 60;
    for (int k = 0; k < Config::numSubs; ++k)
    {
        const int cx = juce::roundToInt (subColumnX (k) + colW * 0.5f);
        subMeters[(size_t) k][0].setBounds (cx - 9, my, 8, mh);
        subMeters[(size_t) k][1].setBounds (cx + 1, my, 8, mh);
    }
    for (int k = 0; k < Config::numAux; ++k)
    {
        const int cx = juce::roundToInt (auxColumnX (k) + colW * 0.5f);
        auxMeters[(size_t) k].setBounds (cx - 4, my, 8, mh);
    }
    const int mainX = juce::roundToInt (mainColumnX());
    const int mainCx = juce::roundToInt (mainColumnX() + colW * 0.9f);
    mainMeters[0].setBounds (mainCx - 12, my, 10, mh);
    mainMeters[1].setBounds (mainCx + 2, my, 10, mh);

    // Tape machine panel, left column.
    deckBox = { 12, bridgeH + 12, 300, 540 };
    auto d = deckBox.reduced (10);
    d.removeFromTop (18);   // title
    cassette.setBounds (d.removeFromTop (Cassette::height).withSizeKeepingCentre (Cassette::width, Cassette::height));
    d.removeFromTop (8);
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
    locateBox = d.removeFromTop (48);
    {
        auto locRow = locateBox.reduced (8, 0).withTrimmedTop (14).withHeight (26);
        const int lw = (locRow.getWidth() - 4 * 4) / 5;
        for (auto* b : { &setIn, &setOut, &loopButton, &punchButton, (juce::TextButton*) &wowButton })
        {
            b->setBounds (locRow.removeFromLeft (lw));
            locRow.removeFromLeft (4);
        }
    }

    d.removeFromTop (16);
    levelBox = d.removeFromTop (70);
    {
        auto inner = levelBox.reduced (10, 0).withTrimmedTop (10);
        recordLevel.setBounds (inner.removeFromRight (Knob::preferredWidth + 20).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        auto speedRow = inner.withSizeKeepingCentre (inner.getWidth(), 26);
        const int w = (speedRow.getWidth() - 8) / 3;
        for (auto* b : speedButtons)
        {
            b->setBounds (speedRow.removeFromLeft (w));
            speedRow.removeFromLeft (4);
        }
    }

    d.removeFromTop (14);
    armBox = d.removeFromTop (72);
    auto grid = armBox.reduced (8, 0).withTrimmedTop (14);
    const int cols = 8, cw = grid.getWidth() / cols;
    for (int t = 0; t < armButtons.size(); ++t)
        armButtons[t]->setBounds (grid.getX() + (t % cols) * cw + 1, grid.getY() + (t / cols) * 26, cw - 2, 22);

    // Group EQ: full centre width under the tape machine and (stage 5) the bus compressor.
    // One column per sub, four bands down, gain and frequency side by side.
    groupEqBox = { 12, bridgeH + 576, centreW - 24, 24 + 4 * 52 + 4 };
    const int colWidth = (groupEqBox.getWidth() - 8) / Config::numSubs;
    for (int k = 0; k < Config::numSubs; ++k)
    {
        const int x0 = groupEqBox.getX() + 4 + k * colWidth;
        for (int b = 0; b < FourBandEq::numBands; ++b)
        {
            const int y0 = groupEqBox.getY() + 24 + b * 52;
            const int idx = (k * FourBandEq::numBands + b) * 2;
            const int cx = x0 + colWidth / 2;
            groupEqKnobs[idx]->setBounds (knobAt (cx - Knob::preferredWidth - 4, y0));
            groupEqKnobs[idx + 1]->setBounds (knobAt (cx + 4, y0));
        }
    }

    // Bus compressor faceplate, right column.
    busPanel.setBounds (330 + ((centreW - 12) - 330 - BusCompPanel::width) / 2, bridgeH + 12, BusCompPanel::width, BusCompPanel::height);

    // Inserts and aux returns, along the bottom of the knob area.
    insertsBox = { 12, groupEqBox.getBottom() + 22, centreW - 24, bridgeH + knobH - 12 - (groupEqBox.getBottom() + 22) };
    inserts.setBounds (insertsBox.reduced (4, 8));

    // Main fader, far right of the fader area.
    const int f = bridgeH + knobH;
    for (int i = 0; i < busFaders.size(); ++i)
    {
        const int cx = juce::roundToInt (gridX + (float) i * colW);
        const int busW = juce::roundToInt (colW);
        busScribbles[i]->setBounds (cx + 2, getHeight() - 28, busW - 4, 22);
        busFaders[i]->setBounds (cx + 1, f + 36, busW - 2, getHeight() - 34 - (f + 36));
    }
    mainScribble.setBounds (mainX + 4, getHeight() - 28, getWidth() - mainX - 12, 22);
    mainFader.setBounds (mainX + 4, f + 36, getWidth() - mainX - 12, mainScribble.getY() - 6 - (f + 36));
}

void CentreSection::paint (juce::Graphics& g)
{
    paintColumn (g, getLocalBounds());

    // Bridge legends under the bus meters.
    g.setFont (ConsoleLookAndFeel::uiFont (10.0f));
    g.setColour (bridgeText.withAlpha (0.8f));
    for (int k = 0; k < Config::numSubs; ++k)
        g.drawText ("S" + juce::String (k + 1), juce::Rectangle<float> (subColumnX (k), 70.0f, colW, 10.0f), juce::Justification::centred, false);
    for (int k = 0; k < Config::numAux; ++k)
        g.drawText ("A" + juce::String (k + 1), juce::Rectangle<float> (auxColumnX (k), 70.0f, colW, 10.0f), juce::Justification::centred, false);
    g.drawText ("Main", juce::Rectangle<float> (mainColumnX(), 70.0f, colW * 1.8f, 10.0f), juce::Justification::centred, false);

    // Limiter lamp next to the main meters.
    {
        const float lx = mainColumnX() + colW * 1.8f - 14.0f;
        g.setColour (limitHold > 0 ? juce::Colour (0xffff4a3a) : juce::Colour (0xff3a1512));
        g.fillEllipse (lx - 4.0f, 30.0f, 8.0f, 8.0f);
        if (limitHold > 0)
        {
            g.setColour (juce::Colour (0x66ff4a3a));
            g.drawEllipse (lx - 6.0f, 28.0f, 12.0f, 12.0f, 2.0f);
        }
        g.setColour (bridgeText.withAlpha (0.8f));
        g.setFont (ConsoleLookAndFeel::uiFont (9.0f));
        g.drawText ("LIM", juce::Rectangle<float> (lx - 12.0f, 40.0f, 24.0f, 10.0f), juce::Justification::centred, false);
    }

    // Tape machine panel.
    g.setColour (faderArea);
    g.fillRoundedRectangle (deckBox.toFloat(), 2.0f);
    g.setColour (juce::Colour (0xff2a2d2f));
    g.drawRoundedRectangle (deckBox.toFloat().reduced (0.5f), 2.0f, 1.0f);
    g.setColour (juce::Colour (0xffaeb3b5));
    g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
    g.drawText ("Tape machine, " + juce::String (TapeMachine::numTracks) + " track",
                deckBox.reduced (10).withHeight (16), juce::Justification::centredLeft, false);

    paintSection (g, "Tape speed (ips) and record level", levelBox, faderArea);
    paintSection (g, "Record arm", armBox, faderArea);
    const auto t = [this] (juce::int64 s) { const auto sr = tape.getSampleRate(); const auto tenths = (juce::int64) ((double) s / (sr > 0 ? sr : 48000.0) * 10.0); return juce::String::formatted ("%02d:%02d.%d", (int) (tenths / 600), (int) ((tenths / 10) % 60), (int) (tenths % 10)); };
    paintSection (g, "Locate   In " + t (tape.getInPoint()) + "   Out " + t (tape.getOutPoint()), locateBox, faderArea);

    // Group EQ.
    paintSection (g, "Group EQ", groupEqBox, stripGrey);
    paintSection (g, "Inserts (VST3) and aux returns", insertsBox, stripGrey);
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    const int colWidth = (groupEqBox.getWidth() - 8) / Config::numSubs;
    for (int k = 0; k < Config::numSubs; ++k)
    {
        const int x0 = groupEqBox.getX() + 4 + k * colWidth;
        g.setColour (legend);
        g.drawText ("Sub " + juce::String (k + 1), juce::Rectangle<int> (x0, groupEqBox.getY() + 7, colWidth, 12), juce::Justification::centred, false);
        if (k > 0)
        {
            g.setColour (juce::Colour (0xff8e9598).withAlpha (0.5f));
            g.fillRect (x0 - 1, groupEqBox.getY() + 8, 1, groupEqBox.getHeight() - 14);
        }
    }

    // Divider before the main fader.
    const int mainX = juce::roundToInt (mainColumnX());
    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (mainX - 2, bridgeH + knobH, 1, faderH);

    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void CentreSection::update()
{
    busPanel.update();

    const int holdBefore = limitHold;
    if (engine.takeLimiting())
        limitHold = 10;
    else if (limitHold > 0)
        --limitHold;
    if ((holdBefore > 0) != (limitHold > 0))
        repaint (juce::roundToInt (mainColumnX() + colW * 1.8f) - 30, 20, 30, 34);

    for (int side = 0; side < 2; ++side)
    {
        mainMeters[(size_t) side].setLevel (engine.takeMainPeak (side));
        for (int k = 0; k < Config::numSubs; ++k)
            subMeters[(size_t) k][(size_t) side].setLevel (engine.takeSubPeak (k, side));
    }
    for (int k = 0; k < Config::numAux; ++k)
        auxMeters[(size_t) k].setLevel (engine.takeAuxPeak (k));

    bool recording = false;
    for (int t = 0; t < TapeMachine::numTracks; ++t)
        recording = recording || tape.isRecording (t);

    const double sr = tape.getSampleRate();
    clock.setTime (sr > 0 ? (double) tape.getPosition() / sr : 0.0, recording);

    const auto& sp = tape.currentSpeed();
    const auto len = tape.getLength();
    const float progress = len > (juce::int64) sr ? (float) ((double) tape.getPosition() / (double) len) : 0.0f;
    cassette.update (1.0 / 30.0, tape.isPlaying(), sp.secondsPerTurn, progress, juce::String (sp.name) + " ips");

    power.setToggleState (engine.isPowered(), juce::dontSendNotification);
    play.setToggleState (tape.isPlaying(), juce::dontSendNotification);
    rec.setToggleState (tape.isRecordEnabled(), juce::dontSendNotification);
    loopButton.setToggleState (tape.loopOn.load(), juce::dontSendNotification);
    punchButton.setToggleState (tape.punchOn.load(), juce::dontSendNotification);
    if (tape.getInPoint() != shownIn || tape.getOutPoint() != shownOut)
    {
        shownIn = tape.getInPoint();
        shownOut = tape.getOutPoint();
        repaint (locateBox.expanded (2, 8));
    }
}

//==============================================================================
Desk::Desk (AudioEngine& e, PluginContext* plugins) : engine (e), centre (e, plugins)
{
    for (auto* s : engine.inputStrips)
        addAndMakeVisible (inputs.add (new StripComponent (*s)));
    addAndMakeVisible (centre);
    for (auto* s : engine.returnStrips)
        addAndMakeVisible (returns.add (new StripComponent (*s)));

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
    for (auto* s : inputs)  s->update();
    for (auto* s : returns) s->update();
    centre.update();
}
