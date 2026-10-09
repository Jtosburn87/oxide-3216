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

    // Channel strips have a taller fader section than the centre: the assign buttons and the meter sit in a
    // column beside the fader (as on the reference console), and the knob area no longer holds them.
    constexpr int stripFaderH = faderH + 84;

    void paintColumn (juce::Graphics& g, juce::Rectangle<int> r, int faderHeight = faderH)
    {
        // Mid-grey knob area (shorter when sections are minimised).
        const int knobs = r.getHeight() - faderHeight;
        g.setColour (stripGrey);
        g.fillRect (r.getX(), 0, r.getWidth(), knobs);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRect (r.getX(), 0, r.getWidth(), 1);

        // Black fader area.
        g.setColour (faderArea);
        g.fillRect (r.getX(), knobs, r.getWidth(), faderHeight);
        g.setColour (juce::Colours::black);
        g.fillRect (r.getX(), knobs, r.getWidth(), 1);
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

    gateThr.setEndLabels ("off", "-10");
    gateRange.setEndLabels ("-60", "0");
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
    solo.setTooltip ("Solo: the monitor outs play the soloed channels instead of the main mix. Main is not affected.");
    add (solo);
    solo.onStateChange = [this] { repaint (solo.getBounds().expanded (0, 14)); };   // the LED above it
    fader.slider.getProperties().set ("capWidth", 30);
    add (fader);
    add (scribble);

    setupScribble (scribble, strip.name);
    scribble.setEditable (false, true, false);   // double-click to rename
    scribble.setTooltip ("Double-click to rename");
    scribble.onTextChange = [this] { strip.name = scribble.getText(); };

    // Minimise buttons on the section titles; each one acts on the whole bank.
    const char* const foldNames[] = { "Equaliser", "Gate", "Compressor", "Aux sends" };
    for (int i = 0; i < (int) foldButtons.size(); ++i)
    {
        auto& b = foldButtons[(size_t) i];
        b.setButtonText (juce::CharPointer_UTF8 ("\xe2\x80\x93"));   // en dash
        b.setTitle ("Minimise " + juce::String (foldNames[i]));
        b.setTooltip ("Minimise the " + juce::String (foldNames[i]).toLowerCase() + " on this whole bank");
        b.onClick = [this, bit = 1 << i] { if (onToggleSection) onToggleSection (bit); };
        add (b);
    }

    // Chain order buttons (shown only when the chain is unlocked).
    for (int i = 0; i < (int) moveButtons.size(); ++i)
    {
        auto& b = moveButtons[(size_t) i];
        b.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb2"));   // up triangle
        b.setTitle ("Move " + juce::String (foldNames[i]) + " earlier");
        b.setTooltip ("Move the " + juce::String (foldNames[i]).toLowerCase() + " one place earlier in this channel's signal chain");
        b.onClick = [this, i] { moveEarlier (i); };
        addChildComponent (b);
    }

    // Return strips: a button on the gate's title line switches the slot between gate and de-esser.
    if (auto* d = s.deEsser.get())
    {
        deFreq = std::make_unique<Knob> (*d->frequency, "Freq", gateCap);
        deFreq->setFormatter ([] (float hz) { return formatHz (hz); });
        deFreq->setEndLabels ("2k", "12k");
        deReduction = std::make_unique<Knob> (*d->reduction, "Reduce", gateCap);
        deReduction->setEndLabels ("0", "20");
        deWide = std::make_unique<ParamButton> (*d->wide, "Wide");
        deWide->setTooltip ("Narrow (off): a band around the frequency. Wide (on): everything above it");
        deGentle = std::make_unique<ParamButton> (*d->gentle, "Gentle");
        deGentle->setTooltip ("Full (off): the whole signal comes down on an s. Gentle (on): only the sibilant band does");
        deBypass = std::make_unique<ParamButton> (*d->bypass, "Byp");
        deBypass->setTooltip ("Bypass the de-esser");
        redButton (*deBypass);
        for (juce::Component* c : { (juce::Component*) deFreq.get(), (juce::Component*) deReduction.get(),
                                    (juce::Component*) deWide.get(), (juce::Component*) deGentle.get(), (juce::Component*) deBypass.get() })
            addChildComponent (c);

        gateModeButton.setButtonText ("Gate");
        gateModeButton.setTooltip ("Switch this channel's gate to a de-esser");
        gateModeButton.onClick = [d] { d->on->toggle(); };
        add (gateModeButton);
    }

    fader.slider.setTitle (strip.name + " fader");
    setTitle (strip.name);
    setFocusContainerType (FocusContainerType::focusContainer);
    lastPops = s.gate.popCount.load();
}

namespace
{
    // Expanded heights of the sections that can be minimised (in Collapsible bit order), and the
    // height a minimised one keeps: just its title line.
    constexpr int expandedH[] = { 10 + 4 * 52 + 4, 10 + 50 + 4 + 18 + 6, 10 + 2 * 52 + 2 + 17 + 4 + 16 + 6, 10 + 4 * 44 + 4 };
    constexpr int collapsedH = 8;
}

void StripComponent::setChainUnlocked (bool unlocked)
{
    chainUnlocked = unlocked;
    resized();
}

void StripComponent::moveEarlier (int stage)
{
    auto c = strip.chain();
    for (size_t p = 1; p < c.size(); ++p)
        if (c[p] == stage)
            std::swap (c[p], c[p - 1]);
    strip.setChain (c);
    resized();
    repaint();
}

int StripComponent::heightFor (int mask)
{
    int h = totalH;
    for (int i = 0; i < 4; ++i)
        if (mask & (1 << i))
            h -= expandedH[i] - collapsedH;
    return h;
}

void StripComponent::setCollapsed (int mask)
{
    if (mask == collapsed)
        return;
    collapsed = mask;
    for (int i = 0; i < 4; ++i)
    {
        const bool folded = (mask & (1 << i)) != 0;
        auto& b = foldButtons[(size_t) i];
        b.setButtonText (folded ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")));
        b.setTitle ((folded ? "Restore " : "Minimise ") + b.getTitle().fromFirstOccurrenceOf (" ", false, false));
        b.setTooltip (folded ? "Show this section again on the whole bank" : "Minimise this section on the whole bank");
    }
    updateVisibility();
    resized();
    repaint();
}

void StripComponent::updateVisibility()
{
    // Hidden when the section is minimised; in the gate slot, only the gate or the de-esser shows.
    const juce::Component* deEssParts[] = { deFreq.get(), deReduction.get(), deWide.get(), deGentle.get(), deBypass.get() };
    for (int i = 0; i < 4; ++i)
    {
        const bool folded = (collapsed & (1 << i)) != 0;
        for (auto* c : sectionComponents (1 << i))
        {
            const bool isDeEss = std::find (std::begin (deEssParts), std::end (deEssParts), c) != std::end (deEssParts);
            c->setVisible (! folded && (i != 1 || isDeEss == shownDeEss));
        }
    }
}


std::vector<juce::Component*> StripComponent::sectionComponents (int bit)
{
    std::vector<juce::Component*> list;
    if (bit == eqSection)
        for (auto* k : eqKnobs) list.push_back (k);
    if (bit == gateSection)
    {
        list = { &gateThr, &gateRange, &gateFast };
        for (juce::Component* c : { (juce::Component*) deFreq.get(), (juce::Component*) deReduction.get(), (juce::Component*) deWide.get(),
                                    (juce::Component*) deGentle.get(), (juce::Component*) deBypass.get() })
            if (c != nullptr)
                list.push_back (c);
    }
    if (bit == compSection)
    {
        list = { &compInput, &compOutput, &compAttack, &compRelease, &compAll, &compIn, &grMeter };
        for (auto* b : ratioButtons) list.push_back (b);
    }
    if (bit == auxSection)
    {
        for (auto* k : auxKnobs) list.push_back (k);
        for (auto* b : auxPre) list.push_back (b);
    }
    return list;
}

void StripComponent::resized()
{
    sections.clear();
    const int bx = 5, bw = stripW - 10;
    const int left = bx + 2, right = bx + bw - 2 - Knob::preferredWidth;
    int y = 12;

    auto box = juce::Rectangle<int> (bx, y, bw, 66);
    sections.push_back ({ "Input", box });
    trim.setBounds (knobAt (left, box.getY() + 10));
    hpf.setBounds (knobAt (right, box.getY() + 10));
    y = box.getBottom() + 14;

    // The sections that can be minimised: a minimised one keeps only its title line (and its button).
    // EQ, gate and compressor are placed in the channel's chain order, so the strip shows the signal path.
    const auto chain = strip.chain();
    const auto foldable = [&] (int i, const char* title, const std::function<void (juce::Rectangle<int>)>& layout)
    {
        const bool folded = (collapsed & (1 << i)) != 0;
        box = { bx, y, bw, folded ? collapsedH : expandedH[i] };
        sections.push_back ({ title, box });
        foldButtons[(size_t) i].setBounds (bx + bw - 20, box.getY() - 7, 18, 14);
        if (i == 1 && deFreq != nullptr)
            gateModeButton.setBounds (bx + 4, box.getY() - 7, 54, 14);
        if (i < 3)
        {
            // Unlocked chain: a button to move this section one place earlier (not on the first one).
            auto& up = moveButtons[(size_t) i];
            up.setBounds (bx + bw - 40, box.getY() - 7, 18, 14);
            up.setVisible (chainUnlocked && chain[0] != i);
        }
        if (! folded)
            layout (box);
        y = box.getBottom() + 14;
    };

    // With the chain unlocked, two buttons share the title line: "Compressor" would not fit.
    // On return strips, the gate slot's title is the gate/de-esser switch instead.
    const char* const stageTitles[] = { "Equaliser", deFreq != nullptr ? "" : "Gate", chainUnlocked ? "Comp" : "Compressor" };
    const std::function<void (juce::Rectangle<int>)> stageLayouts[] =
    {
        [&] (juce::Rectangle<int> b)
        {
            for (int band = 0; band < FourBandEq::numBands; ++band)
            {
                eqKnobs[band * 2]->setBounds (knobAt (left, b.getY() + 10 + band * 52));
                eqKnobs[band * 2 + 1]->setBounds (knobAt (right, b.getY() + 10 + band * 52));
            }
        },
        [&] (juce::Rectangle<int> b)
        {
            gateThr.setBounds (knobAt (left, b.getY() + 10));
            gateRange.setBounds (knobAt (right, b.getY() + 10));
            gateFast.setBounds (bx + 5, b.getY() + 64, bw - 10, 18);
            if (deFreq != nullptr)
            {
                deFreq->setBounds (knobAt (left, b.getY() + 10));
                deReduction->setBounds (knobAt (right, b.getY() + 10));
                auto row = juce::Rectangle<int> (bx + 4, b.getY() + 64, bw - 8, 18);
                deWide->setBounds (row.removeFromLeft (30));
                row.removeFromLeft (2);
                deBypass->setBounds (row.removeFromRight (26));
                row.removeFromRight (2);
                deGentle->setBounds (row);
            }
        },
        [&] (juce::Rectangle<int> b)
        {
            compInput.setBounds (knobAt (left, b.getY() + 10));
            compOutput.setBounds (knobAt (right, b.getY() + 10));
            compAttack.setBounds (knobAt (left, b.getY() + 62));
            compRelease.setBounds (knobAt (right, b.getY() + 62));

            auto row = juce::Rectangle<int> (bx + 4, b.getY() + 116, bw - 8, 17);
            const int w = row.getWidth() / 5;
            for (auto* r : ratioButtons)
                r->setBounds (row.removeFromLeft (w).reduced (1, 0));
            compAll.setBounds (row.reduced (1, 0));

            auto row2 = juce::Rectangle<int> (bx + 4, b.getY() + 137, bw - 8, 16);
            compIn.setBounds (row2.removeFromLeft (30));
            row2.removeFromLeft (4);
            grMeter.setBounds (row2.withSizeKeepingCentre (row2.getWidth(), 8));
        }
    };
    for (const int stage : chain)
        foldable (stage, stageTitles[stage], stageLayouts[stage]);
    shownChain = chain;

    foldable (3, "Aux sends", [&] (juce::Rectangle<int> b)
    {
        for (int k = 0; k < Config::numAux; ++k)
        {
            const int col = k % 2, row = k / 2;
            const int x = bx + 2 + col * 48, ky = b.getY() + 8 + row * 44;
            auxKnobs[k]->setBounds (smallKnobAt (x, ky));
            auxPre[k]->setBounds (x + Knob::smallWidth - 1, ky + 7, 12, 15);
        }
    });

    // Pan, at the foot of the knob area. The assign buttons are in the fader column, as on the reference console.
    box = { bx, y, bw, 10 + Knob::preferredHeight + 6 };
    sections.push_back ({ "Pan", box });
    pan.setBounds (juce::Rectangle<int> (Knob::preferredWidth, Knob::preferredHeight).withCentre ({ stripW / 2, box.getY() + 10 + Knob::preferredHeight / 2 }));

    // Fader section: the fader on the left; down the right, mute, main and sub assigns, the meter, then solo.
    const int f = getHeight() - stripFaderH;   // the fader area is always at the bottom
    scribble.setBounds (4, getHeight() - 28, stripW - 8, 22);
    const int colX = 62, columnW = stripW - colX - 6;
    fader.setBounds (2, f + 8, colX - 6, scribble.getY() - 6 - (f + 8));

    int cy = f + 10;
    mute.setBounds (colX, cy, columnW, 20);
    cy += 28;
    mainButton.setBounds (colX, cy, columnW, 20);
    cy += 24;
    for (auto* b : subButtons)
    {
        b->setBounds (colX, cy, columnW, 18);
        cy += 20;
    }
    cy += 8;
    solo.setBounds (colX, scribble.getY() - 8 - 20, columnW, 20);
    // The meter fills the column down to just above the solo LED; its scale is printed to the left (paint).
    meter.setBounds (colX + columnW - 12, cy, 10, solo.getY() - 30 - cy);
}

void StripComponent::paint (juce::Graphics& g)
{
    paintColumn (g, getLocalBounds(), stripFaderH);

    for (auto& s : sections)
        paintSection (g, s.title, s.box, stripGrey);

    // The meter's scale, printed to its left, and what it shows (input or output) underneath.
    const auto m = meter.getBounds();
    g.setFont (ConsoleLookAndFeel::uiFont (8.5f));
    g.setColour (bridgeText.withAlpha (0.75f));
    for (const int db : { 0, -10, -20, -40 })
    {
        const float yy = (float) m.getBottom() - (float) m.getHeight() * (float) (db + 60) / 60.0f;
        g.drawText (juce::String (db), juce::Rectangle<float> ((float) m.getX() - 24.0f, yy - 5.0f, 21.0f, 10.0f), juce::Justification::centredRight, false);
    }
    g.setFont (ConsoleLookAndFeel::uiFont (9.0f));
    g.setColour (meterInput ? amber : bridgeText.withAlpha (0.6f));
    g.drawText (meterInput ? "in" : "out", juce::Rectangle<int> (m.getX() - 10, m.getBottom() + 2, m.getWidth() + 20, 9), juce::Justification::centred, false);

    // Solo LED above the solo button, lit while soloed.
    const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre (solo.getBounds().toFloat().getCentre().withY ((float) solo.getY() - 8.0f));
    g.setColour (solo.getToggleState() ? juce::Colour (0xffff3b2f) : juce::Colour (0xff4a1512));
    g.fillEllipse (led);

    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void StripComponent::setMeterInput (bool input)
{
    meterInput = input;
    repaint (meter.getBounds().expanded (12, 12));
}

void StripComponent::update()
{
    // A loaded session may have renamed the strip.
    if (! scribble.isBeingEdited() && scribble.getText() != strip.name)
        scribble.setText (strip.name, juce::dontSendNotification);

    // The gate slot switched between gate and de-esser (button, or a loaded session).
    if (strip.deEsser != nullptr && strip.deEsser->on->on() != shownDeEss)
    {
        shownDeEss = ! shownDeEss;
        gateModeButton.setButtonText (shownDeEss ? "De-esser" : "Gate");
        gateModeButton.setTooltip (shownDeEss ? "Switch this channel back to the gate" : "Switch this channel's gate to a de-esser");
        updateVisibility();
        repaint();
    }

    // A loaded session may have changed the chain order.
    if (strip.chain() != shownChain)
    {
        resized();
        repaint();
    }

    // Take both peaks every frame, so flipping never shows a stale one.
    const float out = strip.takePeak(), in = strip.takeInputPeak();
    meter.setLevel (meterInput ? in : out);
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
      inserts (e, plugins),
      monitorLevel (*e.monitorLevel, "Level", juce::Colour (0xffd9d6cc))
{
    addAndMakeVisible (appButtons);
    appButtons.setTitle ("App buttons");
    appButtons.onChildrenChanged = [this] { resized(); repaint(); };
    monitorLevel.setFormatter ([] (float db) { return db <= -59.5f ? juce::String ("off") : (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB"; });
    monitorLevel.setEndLabels ("off", "+10");
    addAndMakeVisible (monitorLevel);

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
    styleTransport (ff, juce::Colour (0xff8a9094));
    styleTransport (stop, juce::Colour (0xffd8dadb));
    styleTransport (play, juce::Colour (0xff3fae3f));
    styleTransport (rec, juce::Colour (0xffc8413a));
    styleTransport (erase, juce::Colour (0xff8a9094));

    power.setTooltip ("Switches the audio interface on and off (off frees the driver for other programs)");
    rew.setTooltip ("Rewind: winds back to the start, speeding up like a real tape machine (Stop brakes)");
    ff.setTooltip ("Fast forward: winds on to the end of the recording (Stop brakes)");
    rec.setTooltip ("Record enable: armed tracks record while the tape plays");
    erase.setTooltip ("Erase all tracks");

    power.onClick = [this] { engine.setPower (! engine.isPowered()); };
    rew.onClick   = [this] { tape.rewind(); };
    ff.onClick    = [this] { tape.fastForward(); };
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
        const auto name = isSub ? AudioEngine::defaultSubName (i) : AudioEngine::defaultAuxName (i - Config::numSubs);
        auto* f = busFaders.add (new Fader (isSub ? *engine.subFader[(size_t) i] : *engine.auxMaster[(size_t) (i - Config::numSubs)], 16));
        f->slider.getProperties().set ("capWidth", 30);
        f->slider.setTitle (name + (isSub ? " fader" : " master"));
        addAndMakeVisible (f);
        auto* l = busScribbles.add (new juce::Label());
        setupScribble (*l, name);
        l->setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
        l->setEditable (false, true, false);   // double-click to rename
        l->setTooltip ("Double-click to rename");
        l->onTextChange = [this, l, i]
        {
            auto& stored = i < Config::numSubs ? engine.subNames[(size_t) i] : engine.auxNames[(size_t) (i - Config::numSubs)];
            const auto text = l->getText().trim();
            stored = text.isNotEmpty() ? text : (i < Config::numSubs ? AudioEngine::defaultSubName (i) : AudioEngine::defaultAuxName (i - Config::numSubs));
            l->setText (stored, juce::dontSendNotification);
            repaint();
            inserts.repaint();
        };
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
    // Meter panel under the bus compressor: stereo subs, mono auxes, stereo main (the console has no meter bridge).
    meterBox = { 330, 12 + BusCompPanel::height + 8, centreW - 12 - 330, 72 };
    const int my = meterBox.getY() + 8, mh = 46;
    for (int k = 0; k < Config::numSubs; ++k)
    {
        const int cx = juce::roundToInt (meterSlotX (k));
        subMeters[(size_t) k][0].setBounds (cx - 8, my, 7, mh);
        subMeters[(size_t) k][1].setBounds (cx + 1, my, 7, mh);
    }
    for (int k = 0; k < Config::numAux; ++k)
    {
        const int cx = juce::roundToInt (meterSlotX (Config::numSubs + k));
        auxMeters[(size_t) k].setBounds (cx - 3, my, 7, mh);
    }
    const int mainCx = juce::roundToInt (meterSlotX (Config::numSubs + Config::numAux));
    mainMeters[0].setBounds (mainCx - 10, my, 9, mh);
    mainMeters[1].setBounds (mainCx + 1, my, 9, mh);
    const int mainX = juce::roundToInt (mainColumnX());

    // Tape machine panel, left column.
    deckBox = { 12, 12, 300, 540 };
    auto d = deckBox.reduced (10);
    d.removeFromTop (18);   // title
    cassette.setBounds (d.removeFromTop (Cassette::height).withSizeKeepingCentre (Cassette::width, Cassette::height));
    d.removeFromTop (8);
    clock.setBounds (d.removeFromTop (44));
    d.removeFromTop (8);

    auto row = d.removeFromTop (32);
    const int bw = (row.getWidth() - 6 * 3) / 7;
    for (auto* b : { &power, &rew, &ff, &stop, &play, &rec, &erase })
    {
        // FF needs less room than the others; Power gets it.
        const int w = b == &power ? bw + 8 : b == &ff ? bw - 8 : bw;
        b->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (3);
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
    groupEqBox = { 12, 576, centreW - 24, 24 + 4 * 52 + 4 };
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

    // Bus compressor faceplate, at the right edge.
    busPanel.setBounds (centreW - 12 - BusCompPanel::width, 12, BusCompPanel::width, BusCompPanel::height);

    // Between the tape machine and the bus compressor: the app buttons, then the monitor (headphone) level.
    const int panelX = deckBox.getRight() + 18, panelW = busPanel.getX() - 18 - panelX;
    const int numButtons = appButtons.getNumChildComponents();
    appBox = { panelX, 12, panelW, 28 + appButtons.heightFor (numButtons) + 10 };
    appButtons.setBounds (appBox.reduced (10).withTrimmedTop (18));
    monitorBox = { panelX, appBox.getBottom() + 16, panelW, 28 + Knob::preferredHeight + 10 };
    monitorLevel.setBounds (monitorBox.withTrimmedTop (24).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));

    // Inserts and aux returns, along the bottom of the knob area.
    insertsBox = { 12, groupEqBox.getBottom() + 22, centreW - 24, knobH - 12 - (groupEqBox.getBottom() + 22) };
    inserts.setBounds (insertsBox.reduced (4, 8));

    // Main fader, far right of the fader area.
    const int f = knobH;
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

    // Meter panel: labels under the bus meters, and the limiter lamp next to the main meters.
    g.setColour (faderArea);
    g.fillRoundedRectangle (meterBox.toFloat(), 2.0f);
    g.setColour (juce::Colour (0xff2a2d2f));
    g.drawRoundedRectangle (meterBox.toFloat().reduced (0.5f), 2.0f, 1.0f);
    {
        const float ly = (float) meterBox.getY() + 57.0f, slotW = (float) (meterBox.getWidth() - 50) / 13.0f;
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f));
        g.setColour (bridgeText.withAlpha (0.8f));
        for (int k = 0; k < Config::numSubs; ++k)
            g.drawText ("S" + juce::String (k + 1), juce::Rectangle<float> (meterSlotX (k) - slotW * 0.5f, ly, slotW, 10.0f), juce::Justification::centred, false);
        for (int k = 0; k < Config::numAux; ++k)
            g.drawText ("A" + juce::String (k + 1), juce::Rectangle<float> (meterSlotX (Config::numSubs + k) - slotW * 0.5f, ly, slotW, 10.0f), juce::Justification::centred, false);
        const float mainCx = meterSlotX (Config::numSubs + Config::numAux);
        g.drawText ("Main", juce::Rectangle<float> (mainCx - slotW, ly, slotW * 2.0f, 10.0f), juce::Justification::centred, false);

        const float lx = mainCx + 30.0f, lampY = (float) meterBox.getY() + 22.0f;
        g.setColour (limitHold > 0 ? juce::Colour (0xffff4a3a) : juce::Colour (0xff3a1512));
        g.fillEllipse (lx - 4.0f, lampY, 8.0f, 8.0f);
        if (limitHold > 0)
        {
            g.setColour (juce::Colour (0x66ff4a3a));
            g.drawEllipse (lx - 6.0f, lampY - 2.0f, 12.0f, 12.0f, 2.0f);
        }
        g.setColour (bridgeText.withAlpha (0.8f));
        g.setFont (ConsoleLookAndFeel::uiFont (9.0f));
        g.drawText ("LIM", juce::Rectangle<float> (lx - 12.0f, lampY + 10.0f, 24.0f, 10.0f), juce::Justification::centred, false);
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

    // App buttons and monitor level panels, in the same style.
    for (auto [box, title] : { std::pair { appBox, "Console" }, std::pair { monitorBox, "Monitor / Phones" } })
    {
        g.setColour (faderArea);
        g.fillRoundedRectangle (box.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff2a2d2f));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 2.0f, 1.0f);
        g.setColour (juce::Colour (0xffaeb3b5));
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        g.drawText (title, box.reduced (10).withHeight (16), juce::Justification::centredLeft, false);
    }

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
        g.drawFittedText (engine.subNames[(size_t) k], juce::Rectangle<int> (x0 + 2, groupEqBox.getY() + 7, colWidth - 4, 12), juce::Justification::centred, 1, 0.7f);
        if (k > 0)
        {
            g.setColour (juce::Colour (0xff8e9598).withAlpha (0.5f));
            g.fillRect (x0 - 1, groupEqBox.getY() + 8, 1, groupEqBox.getHeight() - 14);
        }
    }

    // Divider before the main fader.
    const int mainX = juce::roundToInt (mainColumnX());
    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (mainX - 2, knobH, 1, faderH);

    g.setColour (juce::Colour (0xff2c2f31));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void CentreSection::update()
{
    busPanel.update();

    // Names can change underneath (a session was opened).
    bool renamed = false;
    for (int i = 0; i < busScribbles.size(); ++i)
    {
        const auto& name = i < Config::numSubs ? engine.subNames[(size_t) i] : engine.auxNames[(size_t) (i - Config::numSubs)];
        if (! busScribbles[i]->isBeingEdited() && busScribbles[i]->getText() != name)
        {
            busScribbles[i]->setText (name, juce::dontSendNotification);
            renamed = true;
        }
    }
    if (renamed)
    {
        repaint();
        inserts.repaint();
    }

    const int holdBefore = limitHold;
    if (engine.takeLimiting())
        limitHold = 10;
    else if (limitHold > 0)
        --limitHold;
    if ((holdBefore > 0) != (limitHold > 0))
        repaint (meterBox);

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
    // Tape motion in play speeds: 1 playing, up to +-20 winding (negative = rewinding).
    const float windSpeed = tape.getWindSpeed();
    const float motion = windSpeed != 0.0f ? windSpeed : (tape.isPlaying() ? 1.0f : 0.0f);
    cassette.update (1.0 / 30.0, motion / sp.secondsPerTurn, progress, juce::String (sp.name) + " ips");

    power.setToggleState (engine.isPowered(), juce::dontSendNotification);
    play.setToggleState (tape.isPlaying(), juce::dontSendNotification);
    rew.setToggleState (tape.getWindDirection() < 0, juce::dontSendNotification);
    ff.setToggleState (tape.getWindDirection() > 0, juce::dontSendNotification);
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
StripBank::StripBank (const juce::OwnedArray<ChannelStrip>& channelStrips, const std::vector<Param*>& links)
{
    for (auto* s : channelStrips)
    {
        auto* strip = strips.add (new StripComponent (*s));
        strip->onToggleSection = [this] (int bit)
        {
            setCollapsed (collapsed ^ bit);
            if (onCollapseChanged) onCollapseChanged();
        };
        addAndMakeVisible (strip);
    }

    meterButton.setTooltip ("Meters show each channel's output (after the fader). Click to show the input (after the trim) instead");
    meterButton.setColour (juce::TextButton::buttonOnColourId, amber);
    meterButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    meterButton.onClick = [this]
    {
        meterInput = ! meterInput;
        meterButton.setButtonText (meterInput ? "IN" : "OUT");
        meterButton.setToggleState (meterInput, juce::dontSendNotification);
        meterButton.setTooltip (meterInput ? "Meters show each channel's input (after the trim). Click to show the output (after the fader) instead"
                                           : "Meters show each channel's output (after the fader). Click to show the input (after the trim) instead");
        for (auto* s : strips)
            s->setMeterInput (meterInput);
    };
    addAndMakeVisible (meterButton);

    for (size_t k = 0; k < links.size(); ++k)
    {
        auto* b = linkButtons.add (new ParamButton (*links[k], "Link"));
        b->setTooltip ("Links channels " + juce::String (2 * k + 1) + " and " + juce::String (2 * k + 2)
                       + ": every control moves both, and the pan knobs turn in opposite directions. "
                         "Linking copies channel " + juce::String (2 * k + 1) + "'s settings onto channel " + juce::String (2 * k + 2) + ".");
        b->setColour (juce::TextButton::buttonOnColourId, amber);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (b);
    }
    setSize (strips.size() * stripW, totalH);
}

void StripBank::setChainUnlocked (bool unlocked)
{
    for (auto* s : strips)
        s->setChainUnlocked (unlocked);
}

void StripBank::setCollapsed (int mask)
{
    collapsed = mask & 15;
    for (auto* s : strips)
        s->setCollapsed (collapsed);
    setSize (getWidth(), StripComponent::heightFor (collapsed));
}

void StripBank::resized()
{
    int x = 0;
    for (auto* s : strips)
    {
        s->setBounds (x, 0, stripW, getHeight());
        x += stripW;
    }
    meterButton.setBounds (5 + stripW - 10 - 36, 5, 34, 14);   // on the first strip's "Input" title line
    meterButton.toFront (false);

    // Each link button sits on the line between its two strips, level with their pan knobs.
    for (int k = 0; k < linkButtons.size() && 2 * k + 1 < strips.size(); ++k)
    {
        const auto panY = strips[2 * k]->getPanBounds().getCentreY();
        linkButtons[k]->setBounds (juce::Rectangle<int> (40, 18).withCentre ({ (2 * k + 1) * stripW, panY - 4 }));
        linkButtons[k]->toFront (false);
    }
}

void StripBank::update()
{
    for (auto* s : strips)
        s->update();
}

//==============================================================================
Desk::Desk (AudioEngine& e, PluginContext* plugins)
    : engine (e), inputs (e.inputStrips, { e.inputLink.begin(), e.inputLink.end() }),
      returns (e.returnStrips, { e.returnLink.begin(), e.returnLink.end() }), centre (e, plugins)
{
    centre.setSize (centreW, totalH);
    centre.setTitle ("Centre section");
    inputs.setTitle ("Input bank");
    returns.setTitle ("Return bank");
    for (int s = 0; s < numSections; ++s)
        addAndMakeVisible (getSection ((Section) s));

    // A bank gets shorter or taller when sections are minimised or restored.
    for (auto* bank : { &inputs, &returns })
    {
        bank->addComponentListener (this);
        bank->onCollapseChanged = [this] { if (onCollapseChanged) onCollapseChanged(); };
    }
    relayout();
    startTimerHz (30);
}

Desk::~Desk()
{
    inputs.removeComponentListener (this);
    returns.removeComponentListener (this);
}

void Desk::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (wasResized)
        relayout();
}

void Desk::setChainUnlocked (bool unlocked)
{
    inputs.setChainUnlocked (unlocked);
    returns.setChainUnlocked (unlocked);
}

int Desk::getCollapsed (Section s) const
{
    return s == inputSection ? inputs.getCollapsed() : s == returnSection ? returns.getCollapsed() : 0;
}

void Desk::setCollapsed (Section s, int mask)
{
    if (s == inputSection)  inputs.setCollapsed (mask);
    if (s == returnSection) returns.setCollapsed (mask);
}

juce::Component& Desk::getSection (Section s)
{
    if (s == inputSection)  return inputs;
    if (s == centreSection) return centre;
    return returns;
}

juce::String Desk::sectionName (Section s)
{
    if (s == inputSection)  return "Input bank";
    if (s == centreSection) return "Centre section";
    return "Return bank";
}

void Desk::setDocked (Section s, bool shouldBeDocked)
{
    if (docked[(size_t) s] == shouldBeDocked)
        return;

    docked[(size_t) s] = shouldBeDocked;
    auto& c = getSection (s);
    if (shouldBeDocked)
        addAndMakeVisible (c);   // takes it back from its window
    else
        removeChildComponent (&c);
    relayout();
}

void Desk::relayout()
{
    int width = 2 * cheekW;
    for (int s = 0; s < numSections; ++s)
        if (docked[(size_t) s])
            width += getSection ((Section) s).getWidth();
    if (width == 2 * cheekW)
        width += 600;   // everything is out: room for a short message

    // As tall as the tallest docked section; all sections hang from the top, so a bank with minimised
    // sections collapses upwards.
    int height = 0;
    for (int s = 0; s < numSections; ++s)
        if (docked[(size_t) s])
            height = juce::jmax (height, getSection ((Section) s).getHeight());

    setSize (width, height > 0 ? height : totalH);
    resized();
    if (onSizeChanged)
        onSizeChanged();
}

void Desk::resized()
{
    int x = cheekW;
    for (int s = 0; s < numSections; ++s)
    {
        if (! docked[(size_t) s])
            continue;
        auto& c = getSection ((Section) s);
        c.setBounds (x, 0, c.getWidth(), c.getHeight());
        x += c.getWidth();
    }
}

void Desk::paintCheeks (juce::Graphics& g, juce::Rectangle<int> area)
{
    for (auto cheek : { area.withWidth (cheekW), area.withTrimmedLeft (area.getWidth() - cheekW) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222324), (float) cheek.getX(), 0.0f,
                                                 juce::Colour (0xff060607), (float) cheek.getX() + cheekW * 0.45f, 0.0f, false));
        g.fillRect (cheek);
        g.setColour (juce::Colour (0xff3b3e41));
        g.fillRect (cheek.getX(), cheek.getY(), 1, cheek.getHeight());
        g.fillRect (cheek.getRight() - 1, cheek.getY(), 1, cheek.getHeight());
    }
}

void Desk::paint (juce::Graphics& g)
{
    g.fillAll (bridge);
    paintCheeks (g, getLocalBounds());

    if (std::none_of (docked.begin(), docked.end(), [] (bool d) { return d; }))
    {
        g.setColour (bridgeText.withAlpha (0.7f));
        g.setFont (ConsoleLookAndFeel::uiFont (22.0f));
        g.drawFittedText ("Every section is in its own window.\nUse Sections in the top bar to put them back.",
                          getLocalBounds().reduced (cheekW + 20, 0).withSizeKeepingCentre (getWidth() - 2 * cheekW - 40, 200),
                          juce::Justification::centred, 3);
    }
}

void Desk::timerCallback()
{
    // Sections keep updating whether they are docked or in their own windows.
    inputs.update();
    returns.update();
    centre.update();
}

//==============================================================================
SectionFrame::SectionFrame (juce::Component& s) : section (s)
{
    addAndMakeVisible (section);   // takes it from the console
    setSize (section.getWidth() + 2 * cheekW, section.getHeight());
    section.addComponentListener (this);
}

void SectionFrame::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (! wasResized)
        return;
    setSize (section.getWidth() + 2 * cheekW, section.getHeight());
    if (onSizeChanged)
        onSizeChanged();
}

SectionFrame::~SectionFrame()
{
    section.removeComponentListener (this);
    removeChildComponent (&section);   // the console owns the section, not the window
}

void SectionFrame::paint (juce::Graphics& g)
{
    g.fillAll (bridge);
    Desk::paintCheeks (g, getLocalBounds());
}

void SectionFrame::resized()
{
    section.setBounds (cheekW, 0, section.getWidth(), section.getHeight());
}
