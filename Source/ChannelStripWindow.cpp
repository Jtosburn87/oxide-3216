#include "ChannelStripWindow.h"
#include "StripStyle.h"

using namespace StripStyle;

namespace
{
    // Design geometry: two rows of modules under the name strip. Top: Input, Equaliser, Gate (with its
    // side chain). Bottom: Compressor, Aux sends, Output. The last module of each row takes the spare width.
    constexpr int margin = 12, gap = 12, sideW = 110, eqW = 4 * 54 + 14, auxW = 4 * 48 + 10;
    constexpr int arrowW = 30;   // the previous / next channel buttons
    constexpr int row1Y = 46, row1H = 186, row2Y = row1Y + row1H + 24, row2H = 228;
    constexpr int gateW = ChannelStripPanel::designW - 2 * margin - 2 * gap - sideW - eqW;
    constexpr int outputW = ChannelStripPanel::designW - 2 * margin - 2 * gap - sideW - auxW;
    static_assert (row2Y + row2H + margin == ChannelStripPanel::designH);

    /** Every In button sits at the right end of its section's title line. */
    juce::Rectangle<int> inButtonAt (juce::Rectangle<int> section) { return { section.getRight() - 40, section.getY() - 7, 32, 14 }; }
}

//==============================================================================
/** The header's signal chain: EQ, gate (or de-esser) and compressor as blocks in processing order.
    Unlocked, a block can be dragged to another place; dropping it sets the channel's chain order. */
class ChannelStripPanel::ChainBlocks : public juce::Component
{
public:
    ChainBlocks (ChannelStrip& s, std::function<bool()> deEsser) : strip (s), isDeEsser (std::move (deEsser))
    {
        lockButton.setClickingTogglesState (true);
        lockButton.setToggleState (true, juce::dontSendNotification);
        lockButton.setTooltip ("Padlock closed: the chain order stays put. Click to open it, then drag the blocks into another order");
        lockButton.onClick = [this] { setMouseCursor (locked() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor); repaint(); };
        addAndMakeVisible (lockButton);
    }

    static constexpr int lockW = 26, blockW = 70, arrowW = 14, labelW = 40;
    static constexpr int preferredWidth = lockW + 6 + labelW + 3 * blockW + 2 * arrowW;

    void resized() override { lockButton.setBounds (0, 0, lockW, getHeight()); }

    void paint (juce::Graphics& g) override
    {
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
        g.setColour (ConsoleColours::legend);
        g.drawText ("Chain:", juce::Rectangle<int> (lockW + 6, 0, labelW, getHeight()), juce::Justification::centredLeft, false);

        const auto order = shownOrder();
        const char* const names[] = { "Equaliser", "Gate", "Compressor" };
        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                g.setColour (ConsoleColours::legend.withAlpha (0.7f));
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")),   // arrow
                            juce::Rectangle<int> (slotX (i) - arrowW, 0, arrowW, getHeight()), juce::Justification::centred, false);
            }
            if (order[(size_t) i] != draggedStage)
                paintBlock (g, order[(size_t) i], juce::Rectangle<int> (slotX (i), 0, blockW, getHeight()), names);
        }
        if (draggedStage >= 0)
            paintBlock (g, draggedStage, juce::Rectangle<int> (dragX, 0, blockW, getHeight()), names);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (locked())
            return;
        const auto order = strip.chain();
        for (int i = 0; i < 3; ++i)
            if (juce::Rectangle<int> (slotX (i), 0, blockW, getHeight()).contains (e.getPosition()))
            {
                draggedStage = order[(size_t) i];
                grabOffset = e.x - slotX (i);
                dragX = slotX (i);
                repaint();
            }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggedStage < 0)
            return;
        dragX = juce::jlimit (slotX (0), slotX (2), e.x - grabOffset);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggedStage < 0)
            return;
        const auto order = shownOrder();
        draggedStage = -1;
        if (order != strip.chain())
            strip.setChain (order);   // the panel sees the new order on its timer
        repaint();
    }

private:
    ChannelStrip& strip;
    std::function<bool()> isDeEsser;
    /** A padlock: closed (lit) while locked, open while unlocked. */
    struct LockButton : juce::TextButton
    {
        LockButton() { setTitle ("Lock chain order"); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const bool on = getToggleState();
            getLookAndFeel().drawButtonBackground (g, *this, findColour (on ? buttonOnColourId : buttonColourId), over, down);

            const auto c = getLocalBounds().toFloat().getCentre();
            const auto body = juce::Rectangle<float> (c.x - 5.5f, c.y - 1.5f, 11.0f, 8.0f);
            g.setColour (on ? juce::Colour (0xff141516) : ConsoleColours::legend);
            g.fillRoundedRectangle (body, 1.5f);

            // The shackle: closed into both sides of the body, or lifted and swung open on the right.
            juce::Path shackle;
            const float r = 3.5f, top = body.getY() - (on ? 4.0f : 6.0f);
            shackle.startNewSubPath (c.x - r, body.getY());
            shackle.lineTo (c.x - r, top);
            shackle.addCentredArc (c.x, top, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            shackle.lineTo (c.x + r, on ? body.getY() : top + 1.5f);
            g.strokePath (shackle, juce::PathStrokeType (1.6f));
        }
    };

    LockButton lockButton;
    int draggedStage = -1, grabOffset = 0, dragX = 0;

    bool locked() const { return lockButton.getToggleState(); }
    int slotX (int i) const { return lockW + 6 + labelW + i * (blockW + arrowW); }

    /** The chain as shown: while a block is dragged, it takes the slot nearest to it and the others close up. */
    ChannelStrip::Chain shownOrder() const
    {
        auto order = strip.chain();
        if (draggedStage < 0)
            return order;
        const int target = juce::jlimit (0, 2, juce::roundToInt ((float) (dragX - slotX (0)) / (float) (blockW + arrowW)));
        ChannelStrip::Chain result {};
        int from = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (i == target) { result[(size_t) i] = draggedStage; continue; }
            if (order[(size_t) from] == draggedStage) ++from;
            result[(size_t) i] = order[(size_t) from++];
        }
        return result;
    }

    void paintBlock (juce::Graphics& g, int stage, juce::Rectangle<int> r, const char* const* names) const
    {
        const bool dragged = stage == draggedStage;
        const auto box = r.toFloat().reduced (0.5f, 1.5f);
        g.setColour (dragged ? ConsoleColours::amber : juce::Colours::black.withAlpha (locked() ? 0.25f : 0.4f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (locked() ? juce::Colour (0xff8e9598) : ConsoleColours::amber);
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.setColour (dragged ? juce::Colours::black : ConsoleColours::legend);
        g.drawText (stage == 1 && isDeEsser() ? juce::String ("De-esser") : juce::String (names[stage]), r, juce::Justification::centred, false);
    }
};

//==============================================================================
ChannelStripPanel::ChannelStripPanel (ChannelStrip& s, StripBank& consoleBank, int stripIndex)
    : strip (s), bank (consoleBank), index (stripIndex),
      trim (*s.controls.trim, "Trim"),
      hpf (*s.controls.hpf, "HPF"),
      pan (*s.controls.pan, "Pan"),
      hfBell (*s.eq.bell[FourBandEq::HF], "Bell"),
      lfBell (*s.eq.bell[FourBandEq::LF], "Bell"),
      hmfQ (*s.eq.q[FourBandEq::HMF], "Q", juce::Colour (FourBandEq::bands[FourBandEq::HMF].capColour)),
      lmfQ (*s.eq.q[FourBandEq::LMF], "Q", juce::Colour (FourBandEq::bands[FourBandEq::LMF].capColour)),
      gateThr (*s.gate.threshold, "Thr", gateCap),
      gateRange (*s.gate.range, "Range", gateCap),
      gateFast (*s.gate.fast, "Fast atk"),
      eqIn (*s.controls.eqIn, "In"),
      gateIn (*s.controls.gateIn, "In"),
      gateRelease (*s.gate.release, "Release", gateCap),
      scIn (*s.gate.scIn, "In"),
      scFreq (*s.gate.scFreq, "Freq", gateCap),
      scQ (*s.gate.scQ, "Q", gateCap),
      compPanel (s.comp),
      compIn (*s.comp.in, "In"),
      mainButton (*s.controls.main, "Main"),
      mute (*s.controls.mute, "Mute"),
      solo (*s.controls.solo, "Solo"),
      fader (*s.controls.fader)
{
    ++bank.readers;   // keeps the bank taking meter readings while its view is hidden
    auto add = [this] (juce::Component& c) { addAndMakeVisible (c); };

    setupScribble (scribble, strip.name);
    scribble.setEditable (false, true, false);
    scribble.setTooltip ("Double-click to rename");
    scribble.onTextChange = [this] { strip.name = scribble.getText(); };
    add (scribble);

    // Previous / next channel in this bank (wrapping round). Asked for after the click has finished,
    // because the window replaces this panel.
    prevButton.setButtonText (juce::CharPointer_UTF8 ("\xe2\x97\x80"));   // left triangle
    nextButton.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));   // right triangle
    prevButton.setTitle ("Previous channel");
    nextButton.setTitle ("Next channel");
    prevButton.setTooltip ("Previous channel in this bank (1 goes round to 16)");
    nextButton.setTooltip ("Next channel in this bank (16 goes round to 1)");
    for (auto [button, delta] : { std::pair<juce::TextButton*, int> { &prevButton, -1 }, std::pair<juce::TextButton*, int> { &nextButton, 1 } })
    {
        button->onClick = [this, d = delta]
        {
            if (auto step = onStep)
                juce::MessageManager::callAsync ([step, d] { step (d); });
        };
        add (*button);
    }

    hpf.setFormatter ([] (float f) { return f < 20.5f ? juce::String ("off") : formatHz (f); });
    hpf.setEndLabels ("20", "300");
    add (trim);
    add (hpf);
    add (inMeter);

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
    hfBell.setTooltip ("HF band: lit = bell (Q 1) around the frequency; off = shelf above it");
    add (hfBell);
    lfBell.setTooltip ("LF band: lit = bell (Q 1) around the frequency; off = shelf below it");
    add (lfBell);
    eqIn.setTooltip ("EQ in (out = bypassed)");
    add (eqIn);
    gateIn.setTooltip ("Gate in (out = bypassed)");
    add (gateIn);
    for (auto* k : { &hmfQ, &lmfQ })
    {
        k->setFormatter ([] (float q) { return "Q " + juce::String (q, q < 1.0f ? 2 : 1); });
        k->setReversed (true);   // narrow on the left, wide on the right
        k->setEndLabels ("narr", "wide");
        add (*k);
    }

    gateRelease.setFormatter ([] (float sec) { return sec < 1.0f ? juce::String (juce::roundToInt (sec * 1000.0f)) + " ms" : juce::String (sec, 1) + " s"; });
    gateRelease.setEndLabels ("20m", "2s");
    add (gateRelease);
    scIn.setTooltip ("Side chain in: the gate listens through the filter below (the audio itself is not filtered)");
    add (scIn);
    const char* const scNames[] = { "HPF", "LPF", "BPF" };
    const char* const scTips[] = { "High-pass: the gate ignores everything below the frequency",
                                   "Low-pass: the gate ignores everything above the frequency",
                                   "Band-pass: the gate listens only around the frequency" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = scTypeButtons.add (new RadioParamButton (*s.gate.scType, i, scNames[i]));
        b->setTooltip (scTips[i]);
        add (*b);
    }
    scFreq.setFormatter ([] (float hz) { return formatHz (hz); });
    scFreq.setEndLabels ("20", "20k");
    scQ.setFormatter ([] (float q) { return "Q " + juce::String (q, q < 1.0f ? 2 : 1); });
    scQ.setEndLabels ("0.5", "8");
    add (scFreq);
    add (scQ);

    gateThr.setEndLabels ("off", "-10");
    gateRange.setReversed (true);   // turning right cuts deeper
    gateRange.setEndLabels ("0", "-60");
    gateFast.setTooltip ("Fast attack: opens in 0.1 ms. A loud transient makes the gate pop.");
    gateFast.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9dbd8));
    gateFast.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b0b));
    gateMeter.setTooltip ("Gate: how far it is turning the signal down (full = 60 dB)");
    for (juce::Component* c : { (juce::Component*) &gateThr, (juce::Component*) &gateRange, (juce::Component*) &gateFast, (juce::Component*) &gateMeter })
        add (*c);

    // The compressor: the chosen type's controls (as on the console strip), and the type buttons under them.
    compPanel.onTypeChanged = [this] { resized(); repaint(); };   // the block's title names the type
    add (compPanel);
    compIn.setTooltip ("Compressor in");
    add (compIn);
    const char* const typeTips[] = { "60: VCA compressor in the style of the dbx 160 (RMS, program-dependent attack, OverEasy)",
                                     "76: FET compressor in the style of the 1176 (Input, Output, very fast attack)",
                                     "LA: opto compressor in the style of the LA-2A (Peak Reduction, Gain, Comp / Limit)",
                                     "G: channel compressor in the style of the SSL G series (auto make-up, Fast attack)" };
    for (int t = 0; t < ChannelCompressor::numTypes; ++t)
    {
        auto* b = compTypeButtons.add (new RadioParamButton (*s.comp.type, t, ChannelCompressor::typeLabels[t]));
        b->setTooltip (typeTips[t]);
        add (*b);
    }

    for (int k = 0; k < Config::numAux; ++k)
    {
        auto* knob = auxKnobs.add (new Knob (*s.controls.auxLevel[k], "A" + juce::String (k + 1), auxCap));
        knob->setDialSize (26);
        auto* p = auxPre.add (new ParamButton (*s.controls.auxPre[k], "P"));
        p->setTooltip ("P = pre-fader send");
        add (*knob);
        add (*p);
    }

    pan.setEndLabels ("L", "R");
    add (pan);
    redButton (mute);
    add (mute);
    solo.setTooltip ("Solo: the monitor outs play the soloed channels instead of the main mix. Main is not affected.");
    add (solo);
    add (mainButton);
    for (int k = 0; k < Config::numSubs; ++k)
    {
        auto* b = subButtons.add (new ParamButton (*s.controls.sub[k], "S" + juce::String (k + 1)));
        b->setTooltip ("Assign to sub " + juce::String (k + 1));
        add (*b);
    }
    fader.slider.getProperties().set ("capWidth", 30);
    add (fader);
    add (outMeter);

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
        deIn = std::make_unique<ParamButton> (*d->bypass, "In");
        deIn->setInverted (true);   // lit = in (the setting itself is still the bypass)
        deIn->setTooltip ("De-esser in (out = bypassed)");
        deMeter.setTooltip ("De-esser: how far it is turning the signal down (full = 20 dB)");
        for (juce::Component* c : { (juce::Component*) deFreq.get(), (juce::Component*) deReduction.get(),
                                    (juce::Component*) deWide.get(), (juce::Component*) deGentle.get(), (juce::Component*) deIn.get(),
                                    (juce::Component*) &deMeter })
            addChildComponent (c);

        gateModeButton.setButtonText ("Gate");
        gateModeButton.setTooltip ("Switch this channel's gate to a de-esser");
        gateModeButton.onClick = [d] { d->on->toggle(); };
        add (gateModeButton);
    }

    chainBlocks = std::make_unique<ChainBlocks> (s, [this] { return shownDeEss; });
    add (*chainBlocks);

    lastPops = s.gate.popCount.load();
    setSize (designW, designH);
    timerCallback();
    startTimerHz (30);
}

ChannelStripPanel::~ChannelStripPanel()
{
    --bank.readers;
}

void ChannelStripPanel::showAuxLink (int pair, bool linked)
{
    shownAuxLink[(size_t) pair] = linked;
    const int a = 2 * pair, b = a + 1;
    const auto odd = juce::String (a + 1), even = juce::String (b + 1);
    auxKnobs[a]->setLegend (linked ? "A" + odd + "+" + even : "A" + odd);
    auxKnobs[b]->setLegend (linked ? juce::String ("Pan") : "A" + even);
    auxKnobs[b]->setEndLabels (linked ? "L" : "", linked ? "R" : "");
    auxKnobs[b]->setResetValue (linked ? 0.5f : strip.controls.auxLevel[b]->def);
    updateVisibility();
}

void ChannelStripPanel::updateVisibility()
{
    for (juce::Component* c : { (juce::Component*) &gateThr, (juce::Component*) &gateRange, (juce::Component*) &gateFast, (juce::Component*) &gateMeter,
                                (juce::Component*) &gateRelease, (juce::Component*) &gateIn, (juce::Component*) &scIn, (juce::Component*) &scFreq, (juce::Component*) &scQ })
        c->setVisible (! shownDeEss);
    for (auto* b : scTypeButtons)
        b->setVisible (! shownDeEss);
    for (juce::Component* c : { (juce::Component*) deFreq.get(), (juce::Component*) deReduction.get(), (juce::Component*) deWide.get(),
                                (juce::Component*) deGentle.get(), (juce::Component*) deIn.get() })
        if (c != nullptr)
            c->setVisible (shownDeEss);
    deMeter.setVisible (shownDeEss);

    for (size_t j = 0; j < shownAuxLink.size(); ++j)
        auxPre[(int) (2 * j + 1)]->setVisible (! shownAuxLink[j]);   // a linked pair has one pre switch, the odd one
}

void ChannelStripPanel::timerCallback()
{
    if (! scribble.isBeingEdited() && scribble.getText() != strip.name)
        scribble.setText (strip.name, juce::dontSendNotification);

    if (strip.deEsser != nullptr && strip.deEsser->on->on() != shownDeEss)
    {
        shownDeEss = ! shownDeEss;
        gateModeButton.setButtonText (shownDeEss ? "De-esser" : "Gate");
        gateModeButton.setTooltip (shownDeEss ? "Switch this channel back to the gate" : "Switch this channel's gate to a de-esser");
        updateVisibility();
        repaint();
    }

    for (int j = 0; j < (int) shownAuxLink.size(); ++j)
        if (strip.isAuxLinked (j) != shownAuxLink[(size_t) j])
            showAuxLink (j, ! shownAuxLink[(size_t) j]);

    if (strip.chain() != shownChain)
    {
        resized();
        repaint();
    }

    // The console's bank takes the readings (and resets them); this shows the same ones.
    const auto& r = bank.getStrip (index).getReadings();
    inMeter.setLevel (r.in);
    outMeter.setLevel (r.out);
    compPanel.update();
    compPanel.setReduction (r.gr);
    gateMeter.setReduction (r.gate);
    deMeter.setReduction (r.deEss);

    const auto pops = strip.gate.popCount.load();
    if (pops != lastPops)
    {
        lastPops = pops;
        gateFast.flash();
    }
}

void ChannelStripPanel::resized()
{
    modules.clear();
    // Header: the name between the two arrows; the chain order at the right.
    prevButton.setBounds (margin, 10, arrowW, 22);
    scribble.setBounds (prevButton.getRight() + 6, 10, 180, 22);
    nextButton.setBounds (scribble.getRight() + 6, 10, arrowW, 22);
    chainBlocks->setBounds (getWidth() - margin - ChainBlocks::preferredWidth, 10, ChainBlocks::preferredWidth, 22);

    int x = margin, y = row1Y, h = row1H;
    const auto module = [&] (const juce::String& title, int w)
    {
        const auto box = juce::Rectangle<int> (x, y, w, h);
        modules.push_back ({ title, box });
        x += w + gap;
        return box;
    };
    // Two knobs side by side in a 110-wide module, as on the console strip.
    const auto leftOf = [] (juce::Rectangle<int> b) { return b.getX() + 2; };
    const auto rightOf = [] (juce::Rectangle<int> b) { return b.getRight() - 2 - Knob::preferredWidth; };

    // Top row: Input, Equaliser, Gate.
    {
        // Trim over HPF, the input meter beside them.
        const auto b = module ("Input", sideW);
        trim.setBounds (knobAt (b.getX() + 10, b.getY() + 12));
        hpf.setBounds (knobAt (b.getX() + 10, b.getY() + 70));
        inMeter.setBounds (b.getRight() - 34, b.getY() + 14, 12, b.getBottom() - 16 - (b.getY() + 14));
    }
    {
        // One column per band, gain over frequency, LF on the left to HF on the right (low to high, as on a
        // frequency plot); under them the LF and HF bell switches on the outside and the LMF/HMF Q inside.
        const auto b = module ("Equaliser", eqW);
        const int x0 = b.getX() + (b.getWidth() - FourBandEq::numBands * 54 + 6) / 2;
        const auto column = [x0] (int band) { return x0 + (FourBandEq::numBands - 1 - band) * 54; };
        for (int band = 0; band < FourBandEq::numBands; ++band)
        {
            eqKnobs[band * 2]->setBounds (knobAt (column (band), b.getY() + 12));
            eqKnobs[band * 2 + 1]->setBounds (knobAt (column (band), b.getY() + 70));
        }
        hfBell.setBounds (column (FourBandEq::HF), b.getY() + 144, Knob::preferredWidth, 18);
        lfBell.setBounds (column (FourBandEq::LF), b.getY() + 144, Knob::preferredWidth, 18);
        eqIn.setBounds (inButtonAt (b));
        hmfQ.setBounds (knobAt (column (FourBandEq::HMF), b.getY() + 128));
        lmfQ.setBounds (knobAt (column (FourBandEq::LMF), b.getY() + 128));
    }
    {
        {
            // The gate's own controls on the left (as on the console strip, plus Release); its side chain on the right.
            const auto all = module (deFreq != nullptr ? "" : "Gate", gateW);
            const auto b = all.withWidth (sideW);
            if (deFreq != nullptr)
                gateModeButton.setBounds (b.getX() + 4, b.getY() - 7, 60, 14);
            gateThr.setBounds (knobAt (leftOf (b), b.getY() + 12));
            gateRelease.setBounds (knobAt (rightOf (b), b.getY() + 12));
            gateRange.setBounds (knobAt (leftOf (b), b.getY() + 68));
            gateFast.setBounds (rightOf (b), b.getY() + 84, Knob::preferredWidth, 18);
            gateIn.setBounds (inButtonAt (all));
            gateMeter.setBounds (b.getX() + 5, b.getY() + 130, b.getWidth() - 10, 8);
            deMeter.setBounds (b.getX() + 5, b.getY() + 96, b.getWidth() - 10, 8);

            scBox = { all.getX() + sideW + 8, all.getY() + 16, all.getWidth() - sideW - 14, all.getHeight() - 24 };
            scIn.setBounds (inButtonAt (scBox));
            auto typeRow = juce::Rectangle<int> (scBox.getX() + 6, scBox.getY() + 16, scBox.getWidth() - 12, 18);
            const int tw = typeRow.getWidth() / 3;
            for (auto* t : scTypeButtons)
                t->setBounds (typeRow.removeFromLeft (tw).reduced (1, 0));
            scFreq.setBounds (knobAt (scBox.getX() + 6, scBox.getY() + 48));
            scQ.setBounds (knobAt (scBox.getRight() - 6 - Knob::preferredWidth, scBox.getY() + 48));

            if (deFreq != nullptr)
            {
                deFreq->setBounds (knobAt (leftOf (b), b.getY() + 12));
                deReduction->setBounds (knobAt (rightOf (b), b.getY() + 12));
                auto row = juce::Rectangle<int> (b.getX() + 4, b.getY() + 70, b.getWidth() - 8, 18);
                deWide->setBounds (row.removeFromLeft (row.getWidth() / 2 - 1));
                row.removeFromLeft (2);
                deGentle->setBounds (row);
                deIn->setBounds (inButtonAt (all));
            }
        }
    }

    // Bottom row: Compressor, Aux sends, Output.
    x = margin;
    y = row2Y;
    h = row2H;
    {
        {
            // The title names the type (painted from modules, refreshed on a type change).
            const auto b = module ("Comp " + compPanel.typeLabel(), sideW);
            compPanel.setBounds (b.getX(), b.getY() + 2, b.getWidth(), CompressorPanel::preferredHeight);
            compIn.setBounds (inButtonAt (b));

            // The type buttons along the bottom of the block.
            auto row = juce::Rectangle<int> (b.getX() + 4, b.getBottom() - 28, b.getWidth() - 8, 20);
            const int w = row.getWidth() / ChannelCompressor::numTypes;
            for (auto* t : compTypeButtons)
                t->setBounds (row.removeFromLeft (w).reduced (1, 0));
        }
    }
    {
        // Two rows of four.
        const auto b = module ("Aux sends", auxW);
        for (int k = 0; k < Config::numAux; ++k)
        {
            const int col = k % 4, row = k / 4;
            const int kx = b.getX() + 4 + col * 48, ky = b.getY() + 12 + row * 52;
            auxKnobs[k]->setBounds (smallKnobAt (kx, ky));
            auxPre[k]->setBounds (kx + Knob::smallWidth - 1, ky + 7, 12, 15);
        }
    }
    {
        // Pan over the fader on the left; mute, solo and the assigns in the middle; the meter at the right.
        const auto b = module ("Output", outputW);
        pan.setBounds (knobAt (b.getX() + 12, b.getY() + 12));
        fader.setBounds (b.getX() + 8, b.getY() + 70, 60, b.getBottom() - 8 - (b.getY() + 70));

        const int colW = 64, colX = b.getCentreX() - colW / 2 + 6;
        int cy = b.getY() + 14;
        mute.setBounds (colX, cy, colW, 20);   cy += 24;
        solo.setBounds (colX, cy, colW, 20);   cy += 32;
        mainButton.setBounds (colX, cy, colW, 20);   cy += 24;
        for (auto* sb : subButtons)
        {
            sb->setBounds (colX, cy, colW, 18);
            cy += 20;
        }
        outMeter.setBounds (b.getRight() - 40, b.getY() + 14, 12, b.getBottom() - 24 - (b.getY() + 14));
    }
    shownChain = strip.chain();
}

void ChannelStripPanel::paint (juce::Graphics& g)
{
    g.fillAll (stripGrey);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRect (0, 0, getWidth(), 1);

    for (auto& m : modules)
        paintSection (g, m.title, m.box, stripGrey);
    if (! shownDeEss)
        paintSection (g, "Side chain", scBox, stripGrey);

    // What the two meters show, under each one.
    g.setFont (ConsoleLookAndFeel::uiFont (9.0f));
    g.setColour (bridgeText.withAlpha (0.6f));
    const auto under = [&g] (const juce::Component& meter, const char* text)
    {
        const auto m = meter.getBounds();
        g.drawText (text, juce::Rectangle<int> (m.getX() - 10, m.getBottom() + 2, m.getWidth() + 20, 10), juce::Justification::centred, false);
    };
    under (inMeter, "in");
    under (outMeter, "out");
}

//==============================================================================
/** Holds the panel at its design size and scales it to fit the window (same shape: no stretching). */
class ChannelStripWindow::Content : public juce::Component
{
public:
    Content (ChannelStrip& s, StripBank& b, int i, std::function<void (int)> step) : onStep (std::move (step))
    {
        setStrip (s, b, i);
        setSize (ChannelStripPanel::designW * 8 / 5, ChannelStripPanel::designH * 8 / 5);
    }

    /** A new panel for another channel (the old one goes). */
    void setStrip (ChannelStrip& s, StripBank& b, int i)
    {
        panel.reset();
        panel = std::make_unique<ChannelStripPanel> (s, b, i);
        panel->onStep = onStep;
        addAndMakeVisible (*panel);
        resized();
    }

    void paint (juce::Graphics& g) override { g.fillAll (ConsoleColours::stripGrey); }

    void resized() override
    {
        const float s = juce::jmin ((float) getWidth() / ChannelStripPanel::designW, (float) getHeight() / ChannelStripPanel::designH);
        panel->setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - s * ChannelStripPanel::designW) * 0.5f,
                                                                          (getHeight() - s * ChannelStripPanel::designH) * 0.5f));
    }

private:
    std::function<void (int)> onStep;
    std::unique_ptr<ChannelStripPanel> panel;
};

ChannelStripWindow::ChannelStripWindow (const juce::String& title, ChannelStrip& s, StripBank& b, int i,
                                        std::function<bool (const juce::KeyPress&)> keyHandler)
    : juce::DocumentWindow (title, ConsoleColours::stripGrey, juce::DocumentWindow::allButtons),
      keys (std::move (keyHandler))
{
    setUsingNativeTitleBar (true);
    // The arrows ask after the click has finished; by then this window may have closed.
    content = new Content (s, b, i, [safe = juce::Component::SafePointer<ChannelStripWindow> (this)] (int delta)
    {
        if (safe != nullptr && safe->onStep)
            safe->onStep (delta);
    });
    setContentOwned (content, true);

    // Fit the screen if the usual size (160 %) is too big for it.
    const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    const float z = juce::jmin (1.6f, (float) screen.getWidth() * 0.95f / ChannelStripPanel::designW,
                                (float) screen.getHeight() * 0.9f / ChannelStripPanel::designH);
    // Any shape: the panel scales to fit and keeps its proportions (Content::resized).
    constrainer.setSizeLimits (ChannelStripPanel::designW / 2, ChannelStripPanel::designH / 2, 10000, 10000);
    setConstrainer (&constrainer);
    setResizable (true, false);
    centreWithSize (juce::roundToInt (ChannelStripPanel::designW * z), juce::roundToInt (ChannelStripPanel::designH * z));
    setVisible (true);
}

void ChannelStripWindow::showStrip (const juce::String& title, ChannelStrip& s, StripBank& b, int i)
{
    setName (title);
    content->setStrip (s, b, i);
}
