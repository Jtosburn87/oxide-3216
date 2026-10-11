#include "Desk.h"
#include "ConsoleLookAndFeel.h"
#include "EraseDialog.h"
#include "PatchBay.h"
#include "TalkbackSettings.h"
#include "StripStyle.h"

using namespace ConsoleColours;
using namespace DeskLayout;
using namespace StripStyle;

namespace
{
    // Channel strips have a taller fader section than the centre: the assign buttons and the meter sit in a
    // column beside the fader (as on the reference console), and the knob area no longer holds them.
    // A row at the top of it holds the bank's Meters In/Out button (on the first strip).
    constexpr int meterRowH = 24;
    constexpr int stripFaderH = faderH + 84 + meterRowH;

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
      eqIn (*s.controls.eqIn, "In"),
      gateIn (*s.controls.gateIn, "In"),
      compPanel (s.comp),
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
    gateRange.setReversed (true);   // turning right cuts deeper
    gateRange.setEndLabels ("0", "-60");
    gateFast.setTooltip ("Fast attack: opens in 0.1 ms. A loud transient makes the gate pop.");
    gateFast.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9dbd8));
    gateFast.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b0b));
    add (gateThr);
    add (gateRange);
    add (gateFast);
    eqIn.setTooltip ("EQ in (out = bypassed)");
    gateIn.setTooltip ("Gate in (out = bypassed)");
    add (eqIn);
    add (gateIn);
    gateMeter.setTooltip ("Gate: how far it is turning the signal down (full = 60 dB)");
    add (gateMeter);

    // The compressor's controls follow its type (chosen in the channel window); its title shows which.
    compPanel.onTypeChanged = [this] { resized(); repaint(); };
    add (compPanel);
    compIn.setTooltip ("Compressor in");
    add (compIn);

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
        deMeter.setTooltip ("De-esser: how far it is turning the signal down (full = 20 dB)");
        for (juce::Component* c : { (juce::Component*) deFreq.get(), (juce::Component*) deReduction.get(),
                                    (juce::Component*) deWide.get(), (juce::Component*) deGentle.get(), (juce::Component*) deBypass.get(),
                                    (juce::Component*) &deMeter })
            addChildComponent (c);

        gateModeButton.setButtonText ("Gate");
        gateModeButton.setTooltip ("Switch this channel's gate to a de-esser");
        gateModeButton.onClick = [d] { d->on->toggle(); };
        add (gateModeButton);
    }

    // Double-clicking any knob opens the whole channel in a big window (Ctrl+click resets a knob).
    std::vector<Knob*> knobs { &trim, &hpf, &pan, &gateThr, &gateRange, deFreq.get(), deReduction.get() };
    compPanel.setKnobDoubleClick ([this] { if (onOpenStrip) onOpenStrip(); });
    for (auto* k : eqKnobs)  knobs.push_back (k);
    for (auto* k : auxKnobs) knobs.push_back (k);
    for (auto* k : knobs)
        if (k != nullptr)
            k->setOnDoubleClick ([this] { if (onOpenStrip) onOpenStrip(); });

    fader.slider.setTitle (strip.name + " fader");
    setTitle (strip.name);
    setFocusContainerType (FocusContainerType::focusContainer);
    lastPops = s.gate.popCount.load();
}

namespace
{
    // Expanded heights of the sections that can be minimised (in Collapsible bit order), and the
    // height a minimised one keeps: just its title line.
    constexpr int expandedH[] = { 10 + 4 * 52 + 4, 10 + 50 + 4 + 18 + 4 + 8 + 6,10 + 2 * 52 + 2 + 17 + 4 + 16 + 6, 10 + 4 * 44 + 4 };
    constexpr int collapsedH = 8;
    constexpr int inW = 20;   // In buttons on the EQ, gate and compressor title lines
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

void StripComponent::showAuxLink (int pair, bool linked)
{
    shownAuxLink[(size_t) pair] = linked;
    const int a = 2 * pair, b = a + 1;
    const auto odd = juce::String (a + 1), even = juce::String (b + 1);
    auto& level = *auxKnobs[a];
    auto& panKnob = *auxKnobs[b];
    level.setLegend (linked ? "A" + odd + "+" + even : "A" + odd);
    panKnob.setLegend (linked ? juce::String ("Pan") : "A" + even);
    panKnob.setEndLabels (linked ? "L" : "", linked ? "R" : "");
    panKnob.setResetValue (linked ? 0.5f : strip.controls.auxLevel[b]->def);
    auxPre[a]->setTooltip (linked ? "P = pre-fader send (both sides of the stereo aux " + odd + "+" + even + ")" : "P = pre-fader send");
    updateVisibility();
}

void StripComponent::updateVisibility()
{
    // Hidden when the section is minimised; in the gate slot, only the gate or the de-esser shows.
    const juce::Component* deEssParts[] = { deFreq.get(), deReduction.get(), deWide.get(), deGentle.get(), deBypass.get(), &deMeter };
    for (int i = 0; i < 4; ++i)
    {
        const bool folded = (collapsed & (1 << i)) != 0;
        for (auto* c : sectionComponents (1 << i))
        {
            const bool isDeEss = std::find (std::begin (deEssParts), std::end (deEssParts), c) != std::end (deEssParts);
            c->setVisible (! folded && (i != 1 || isDeEss == shownDeEss));
        }
    }
    gateIn.setVisible (! shownDeEss);   // the de-esser has its own Byp

    // A linked aux pair has one pre switch, the odd one.
    for (size_t j = 0; j < shownAuxLink.size(); ++j)
        if (shownAuxLink[j])
            auxPre[(int) (2 * j + 1)]->setVisible (false);
}


std::vector<juce::Component*> StripComponent::sectionComponents (int bit)
{
    std::vector<juce::Component*> list;
    if (bit == eqSection)
        for (auto* k : eqKnobs) list.push_back (k);
    if (bit == gateSection)
    {
        list = { &gateThr, &gateRange, &gateFast, &gateMeter };
        for (juce::Component* c : { (juce::Component*) deFreq.get(), (juce::Component*) deReduction.get(), (juce::Component*) deWide.get(),
                                    (juce::Component*) deGentle.get(), (juce::Component*) deBypass.get() })
            if (c != nullptr)
                list.push_back (c);
        if (deFreq != nullptr)
            list.push_back (&deMeter);
    }
    if (bit == compSection)
    {
        list = { &compPanel };
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
        // EQ, gate and compressor: the In button at the left of the title line (it stays when minimised),
        // the title after it.
        const bool hasIn = i < 3;
        sections.push_back ({ title, box, hasIn ? inW + 2 : 0 });
        foldButtons[(size_t) i].setBounds (bx + bw - 20, box.getY() - 7, 18, 14);
        if (hasIn)
        {
            ParamButton* const inButtons[] = { &eqIn, &gateIn, &compIn };
            inButtons[i]->setBounds (bx + 4, box.getY() - 7, inW, 14);
        }
        if (i == 1 && deFreq != nullptr)
        {
            // Up to the move button (chain unlocked) or the minimise button.
            const int end = bx + bw - (chainUnlocked ? 42 : 22);
            gateModeButton.setBounds (bx + 6 + inW, box.getY() - 7, end - (bx + 6 + inW), 14);
        }
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

    // Short titles: the In button and the minimise (and, unlocked, move) buttons share the title line.
    // On return strips, the gate slot's title is the gate/de-esser switch instead.
    const auto compTitle = chainUnlocked ? juce::String ("Comp") : "Comp " + compPanel.typeLabel();   // e.g. "Comp 76"
    const char* const stageTitles[] = { "EQ", deFreq != nullptr ? "" : "Gate", compTitle.toRawUTF8() };
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
            const auto meterRow = juce::Rectangle<int> (bx + 5, b.getY() + 86, bw - 10, 8);
            gateMeter.setBounds (meterRow);
            deMeter.setBounds (meterRow);
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
            compPanel.setBounds (bx, b.getY(), bw, CompressorPanel::preferredHeight);
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
    const int f = getHeight() - stripFaderH + meterRowH;   // the fader area is always at the bottom, under the meter row
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
        paintSection (g, s.title, s.box, stripGrey, s.titleIndent);

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

void StripComponent::update (const StripComponent* source)
{
    compPanel.update();   // the compressor type may have changed (channel window, or a loaded session)

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

    // Aux pairs linked or unlinked (centre section, or a loaded session): the send labels follow.
    for (int j = 0; j < (int) shownAuxLink.size(); ++j)
        if (strip.isAuxLinked (j) != shownAuxLink[(size_t) j])
            showAuxLink (j, ! shownAuxLink[(size_t) j]);

    // A loaded session may have changed the chain order.
    if (strip.chain() != shownChain)
    {
        resized();
        repaint();
    }

    // Readings are taken (and so reset) once a frame, by the console's bank; a copy shows the same ones.
    if (source != nullptr)
    {
        readings = source->readings;
    }
    else
    {
        // Take both peaks every frame, so flipping never shows a stale one.
        readings.out = strip.takePeak();
        readings.in = strip.takeInputPeak();
        readings.gr = strip.comp.isIn() ? strip.comp.takeGainReduction() : 0.0f;
        readings.gate = strip.gate.takeReduction();   // taken every frame, so a switch never shows a stale one
        readings.deEss = shownDeEss && strip.deEsser->isActive() ? strip.deEsser->takeReduction() : 0.0f;
    }
    meter.setLevel (meterInput ? readings.in : readings.out);
    compPanel.setReduction (readings.gr);
    if (shownDeEss)
        deMeter.setReduction (readings.deEss);
    else
        gateMeter.setReduction (readings.gate);

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
      pitchKnob (*e.tape.pitch, "Pitch", juce::Colour (0xffd9d6cc)),
      wowButton (*e.tape.wowFlutter, "W&F"),
      mainFader (*e.mainFader),
      busPanel (e.busComp),
      inserts (e, plugins),
      shuttle (e.tape),
      monitorLevel (*e.monitorLevel, "Level", juce::Colour (0xffd9d6cc)),
      tbLevel (*e.tbLevel, "TB", juce::Colour (0xffd6392d)),
      talkButton (e.talkbackOn),
      sceneReadout (e.scenes),
      context (plugins)
{
    addAndMakeVisible (appButtons);
    appButtons.setTitle ("App buttons");
    appButtons.onChildrenChanged = [this] { resized(); repaint(); };
    monitorLevel.setFormatter ([] (float db) { return db <= -59.5f ? juce::String ("off") : (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB"; });
    monitorLevel.setEndLabels ("off", "+10");
    addAndMakeVisible (monitorLevel);

    // Talkback: level, the Talk key, and its settings (destinations Aux 1-8 and the TB limiter).
    tbLevel.setFormatter ([] (float db) { return db <= -59.5f ? juce::String ("TB off") : "TB " + juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1); });
    tbLevel.setEndLabels ("off", "+10");
    addAndMakeVisible (tbLevel);
    addAndMakeVisible (talkButton);
    tbSetup.setTooltip ("Talkback settings: which auxes it goes to (1-8) and the TB limiter");
    tbSetup.onClick = [this] { TalkbackSettings::show (engine); };
    addAndMakeVisible (tbSetup);
    tbMic.setTitle ("Talkback mic in");
    tbMic.onPopupMenu = [this] { showTbMicMenu(); };
    addAndMakeVisible (tbMic);

    // Scene: the number, and under it previous / GO / next. The arrows only move the number; GO recalls it.
    sceneReadout.onPopupMenu = [this] { SceneMemoryPanel::show (engine.scenes); };
    addAndMakeVisible (sceneReadout);
    scenePrev.setButtonText (juce::CharPointer_UTF8 ("\xe2\x97\x80"));
    sceneNext.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));
    scenePrev.setTooltip ("Previous stored scene (GO recalls it)");
    sceneNext.setTooltip ("Next stored scene (GO recalls it)");
    sceneGo.setTooltip ("Recall the scene on the readout: its faders and knobs");
    scenePrev.setTitle ("Previous scene");
    sceneNext.setTitle ("Next scene");
    sceneGo.setTitle ("Go to scene");
    const auto step = [this] (int direction)
    {
        if (const int s = engine.scenes.nextStored (direction); s >= 0)
            engine.scenes.select (s);
        sceneReadout.repaint();
    };
    scenePrev.onClick = [step] { step (-1); };
    sceneNext.onClick = [step] { step (1); };
    sceneGo.onClick = [this] { engine.scenes.recall (engine.scenes.getSelected()); sceneReadout.repaint(); };
    for (auto* b : { &scenePrev, &sceneGo, &sceneNext })
        addAndMakeVisible (b);

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

    for (auto* b : { &power, &rew, &ff, &stop, &play, &rec, &erase })
        addAndMakeVisible (b);

    power.setTooltip ("Switches the audio interface on and off (off frees the driver for other programs)");
    rew.setTooltip ("Rewind: winds back to the start, speeding up like a real tape machine (Stop brakes)");
    ff.setTooltip ("Fast forward: winds on to the end of the recording (Stop brakes)");
    stop.setTooltip ("Stop");
    play.setTooltip ("Play");
    rec.setTooltip ("Record enable: armed tracks record while the tape plays");
    erase.setTooltip ("Erase: whole tracks, or a section of them");

    power.onClick = [this] { engine.setPower (! engine.isPowered()); };
    rew.onClick   = [this] { tape.rewind(); };
    ff.onClick    = [this] { tape.fastForward(); };
    stop.onClick  = [this] { tape.stop(); };
    play.onClick  = [this] { tape.play(); };
    rec.onClick   = [this] { tape.setRecordEnabled (! tape.isRecordEnabled()); };
    erase.onClick = [this] { openErase(); };

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
    pitchKnob.setEndLabels ("-", "+");
    pitchKnob.setFormatter ([] (float pc) { return std::abs (pc) < 0.05f ? juce::String ("Pitch") : (pc > 0 ? "+" : "") + juce::String (pc, 1) + " %"; });
    addAndMakeVisible (pitchKnob);

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
            // Fixed bands: one gain knob each, the frequency printed in the legend.
            auto hz = formatHz (spec.defHz);
            if (hz.endsWith (".0k"))
                hz = hz.dropLastCharacters (3) + "k";
            auto* g = groupEqKnobs.add (new Knob (*engine.groupEq[k]->gain[b], juce::String (spec.label) + " " + hz, cap));
            g->setEndLabels ("-", "+");
            addAndMakeVisible (g);
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
    addAndMakeVisible (shuttle);

    for (int i = 0; i < Config::numSubs + Config::numAux; ++i)
    {
        const bool isSub = i < Config::numSubs;
        const auto name = isSub ? AudioEngine::defaultSubName (i) : AudioEngine::defaultAuxName (i - Config::numSubs);
        auto* f = busFaders.add (new Fader (isSub ? *engine.subFader[(size_t) i] : *engine.auxMaster[(size_t) (i - Config::numSubs)], 16));
        f->slider.getProperties().set ("capWidth", 30);
        f->slider.setTitle (name + (isSub ? " fader" : " master"));
        addAndMakeVisible (f);
        auto* m = busMainButtons.add (new ParamButton (isSub ? *engine.subMain[(size_t) i] : *engine.auxMain[(size_t) (i - Config::numSubs)], "Main"));
        m->setTooltip (isSub ? "Assign this sub to the main mix (off: it still feeds its outputs and the patch bay)"
                             : "Assign this aux return (after its inserts) to the main mix. Off: the aux only feeds its output and the patch bay.");
        addAndMakeVisible (m);
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

void CentreSection::openErase()
{
    EraseDialog::show (tape);
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
    deckBox = { 12, 12, 300, 552 };
    auto d = deckBox.reduced (10);
    power.setBounds (d.removeFromTop (24).removeFromRight (90));   // above the cassette, right of the title
    d.removeFromTop (2);
    cassette.setBounds (d.removeFromTop (Cassette::height).withSizeKeepingCentre (Cassette::width, Cassette::height));
    d.removeFromTop (6);
    clock.setBounds (d.removeFromTop (44));
    d.removeFromTop (6);

    // Transport keys, each with its name printed above.
    auto row = d.removeFromTop (TransportKey::labelHeight + 30);
    const int gap = 5, bw = (row.getWidth() - 5 * gap) / 6;
    for (auto* b : { &rew, &ff, &stop, &play, &rec, &erase })
    {
        b->setBounds (b == &erase ? row : row.removeFromLeft (bw));
        row.removeFromLeft (gap);
    }

    d.removeFromTop (12);
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
        recordLevel.setBounds (inner.removeFromRight (Knob::preferredWidth + 14).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        pitchKnob.setBounds (inner.removeFromRight (Knob::preferredWidth + 14).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
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

    // Group EQ, under the tape machine: one column of fixed-band gain knobs straight over each sub fader.
    groupEqBox = { juce::roundToInt (subColumnX (0)) + 2, 576,
                   juce::roundToInt (subColumnX (Config::numSubs)) - 2 - (juce::roundToInt (subColumnX (0)) + 2), 24 + 4 * 52 + 4 };
    for (int k = 0; k < Config::numSubs; ++k)
    {
        const int cx = juce::roundToInt (subColumnX (k) + colW * 0.5f);
        for (int b = 0; b < FourBandEq::numBands; ++b)
            groupEqKnobs[k * FourBandEq::numBands + b]->setBounds (knobAt (cx - Knob::preferredWidth / 2, groupEqBox.getY() + 24 + b * 52));
    }

    // Jog / shuttle: the rest of that row, right of the group EQ.
    shuttleBox = { groupEqBox.getRight() + 18, groupEqBox.getY(), centreW - 10 - (groupEqBox.getRight() + 18), groupEqBox.getHeight() };
    shuttle.setBounds (shuttleBox.reduced (1));

    // Bus compressor faceplate, at the right edge.
    busPanel.setBounds (centreW - 12 - BusCompPanel::width, 12, BusCompPanel::width, BusCompPanel::height);

    // Between the tape machine and the bus compressor: the app buttons, then the monitor (headphone) level.
    const int panelX = deckBox.getRight() + 18, panelW = busPanel.getX() - 18 - panelX;
    const int numButtons = appButtons.getNumChildComponents();
    appBox = { panelX, 12, panelW, 28 + appButtons.heightFor (numButtons) + 10 };
    appButtons.setBounds (appBox.reduced (10).withTrimmedTop (18));
    // Scene: the 2-digit readout, then previous / GO / next.
    sceneBox = { panelX, appBox.getBottom() + 12, panelW, 24 + 40 + 4 + 24 + 8 };
    {
        auto m = sceneBox.reduced (8, 0).withTrimmedTop (24);
        sceneReadout.setBounds (m.removeFromTop (40));
        m.removeFromTop (4);
        auto buttonRow = m.removeFromTop (24);
        const int arrowW = (buttonRow.getWidth() - 8) / 4;
        scenePrev.setBounds (buttonRow.removeFromLeft (arrowW));
        sceneNext.setBounds (buttonRow.removeFromRight (arrowW));
        sceneGo.setBounds (buttonRow.reduced (4, 0));
    }
    // Monitor and talkback: the two levels side by side, Talk and Setup under them.
    // The talkback mic's XLR sits over the TB column; its input is named to the left (see paint).
    monitorBox = { panelX, sceneBox.getBottom() + 12, panelW, 28 + tbMicH + 4 + Knob::preferredHeight + 6 + 26 + 10 };
    {
        auto m = monitorBox.reduced (8, 0).withTrimmedTop (24);
        auto micRow = m.removeFromTop (tbMicH);
        tbMic.setBounds (micRow.removeFromRight (micRow.getWidth() / 2).withSizeKeepingCentre (tbMicH, tbMicH));
        m.removeFromTop (4);
        auto knobs = m.removeFromTop (Knob::preferredHeight);
        monitorLevel.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        tbLevel.setBounds (knobs.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        m.removeFromTop (6);
        auto buttons = m.removeFromTop (26);
        talkButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 + 4));
        buttons.removeFromLeft (4);
        tbSetup.setBounds (buttons);
    }

    // Inserts and aux returns, along the bottom of the knob area: each cell over its fader.
    insertsBox = { groupEqBox.getX(), groupEqBox.getBottom() + 22, centreW - 10 - groupEqBox.getX(), knobH - 12 - (groupEqBox.getBottom() + 22) };
    inserts.setBounds ((int) gridX, insertsBox.getY() + 8, centreW - 8 - (int) gridX, insertsBox.getHeight() - 16);
    inserts.setColumnWidth (colW);

    // Main fader, far right of the fader area.
    const int f = knobH;
    for (int i = 0; i < busFaders.size(); ++i)
    {
        const int cx = juce::roundToInt (gridX + (float) i * colW);
        const int busW = juce::roundToInt (colW);
        busScribbles[i]->setBounds (cx + 2, getHeight() - 28, busW - 4, 22);
        busMainButtons[i]->setBounds (cx + 5, f + 10, busW - 10, 20);
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
                deckBox.reduced (10).withHeight (24), juce::Justification::centredLeft, false);

    // App buttons and monitor level panels, in the same style.
    for (auto [box, title] : { std::pair { appBox, "Console" }, std::pair { sceneBox, "Scene" }, std::pair { monitorBox, "Monitor / TB" } })
    {
        g.setColour (faderArea);
        g.fillRoundedRectangle (box.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff2a2d2f));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 2.0f, 1.0f);
        g.setColour (juce::Colour (0xffaeb3b5));
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        g.drawText (title, box.reduced (10).withHeight (16), juce::Justification::centredLeft, false);
    }

    // Legend left of the talkback mic's XLR: what it is and which interface input feeds TB In.
    {
        auto legendArea = monitorBox.reduced (8, 0).withTrimmedTop (24).removeFromTop (tbMicH);
        legendArea = legendArea.removeFromLeft (legendArea.getWidth() / 2).withSizeKeepingCentre (legendArea.getWidth() / 2, 28);
        g.setColour (juce::Colour (0xffaeb3b5));
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        g.drawFittedText ("TB MIC", legendArea.removeFromTop (14), juce::Justification::centred, 1, 0.7f);
        const auto mic = tbMicInputName();
        g.setColour (mic.isNotEmpty() ? bridgeText : bridgeText.withAlpha (0.45f));
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f));
        g.drawFittedText (mic.isNotEmpty() ? mic : juce::String ("none"), legendArea, juce::Justification::centred, 1, 0.8f);
    }

    const float recordedIps = tape.getRecordedIps();
    paintSection (g, "Tape speed (ips), pitch, record level"
                     + (recordedIps > 0.0f ? "   |   recorded at " + juce::String (recordedIps, recordedIps < 10.0f ? 1 : 0) : juce::String()),
                  levelBox, faderArea);
    paintSection (g, "Record arm", armBox, faderArea);
    const auto t = [this] (juce::int64 s) { const auto sr = tape.getSampleRate(); const auto tenths = (juce::int64) ((double) s / (sr > 0 ? sr : 48000.0) * 10.0); return juce::String::formatted ("%02d:%02d.%d", (int) (tenths / 600), (int) ((tenths / 10) % 60), (int) (tenths % 10)); };
    paintSection (g, "Locate   In " + t (tape.getInPoint()) + "   Out " + t (tape.getOutPoint()), locateBox, faderArea);

    // Group EQ.
    paintSection (g, "Group EQ", groupEqBox, stripGrey);
    paintSection (g, "Jog / shuttle", shuttleBox, stripGrey);
    paintSection (g, "Inserts (VST3) and aux returns", insertsBox, stripGrey);
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    for (int k = 0; k < Config::numSubs; ++k)
    {
        const int x0 = juce::roundToInt (subColumnX (k)), x1 = juce::roundToInt (subColumnX (k + 1));
        g.setColour (legend);
        g.drawFittedText (engine.subNames[(size_t) k], juce::Rectangle<int> (x0 + 3, groupEqBox.getY() + 7, x1 - x0 - 6, 12), juce::Justification::centred, 1, 0.7f);
        if (k > 0)
        {
            g.setColour (juce::Colour (0xff8e9598).withAlpha (0.5f));
            g.fillRect (x0, groupEqBox.getY() + 8, 1, groupEqBox.getHeight() - 14);
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
    talkButton.update();

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

    // The speed the tape was recorded at, in the speed box's title (set by the first recording, cleared by Erase).
    if (tape.getRecordedIps() != shownRecordedIps)
    {
        shownRecordedIps = tape.getRecordedIps();
        repaint (levelBox);
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

    updateTapeDisplay (tape, cassette, clock);

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

    // The talkback mic's input can change in the patch bay too.
    if (const auto mic = tbMicInputName(); mic != shownTbMic)
    {
        shownTbMic = mic;
        tbMic.setTooltip ("Talkback mic in" + (mic.isNotEmpty() ? ": " + mic + " of the interface" : juce::String (": not patched"))
                          + ". Right-click to choose the interface input (patches it into TB In).");
        repaint (monitorBox);
    }
}

juce::String CentreSection::tbMicInputName() const
{
    const int dev = engine.getInterfaceInput (AudioEngine::Patch::talkbackIn);
    return dev >= 0 ? "Input " + juce::String (dev + 1) : juce::String();
}

void CentreSection::showTbMicMenu()
{
    // The interface's inputs (as many as the patch bay shows); the one chosen is wired into TB In.
    const int current = engine.getInterfaceInput (AudioEngine::Patch::talkbackIn);
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    const auto names = device != nullptr ? device->getInputChannelNames() : juce::StringArray();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Talkback mic (TB In)");
    if (names.isEmpty())
        menu.addItem (-1, "No audio interface open", false, false);
    for (int i = 0; i < juce::jmin (names.size(), PatchBayField::maxInterfaceJacks); ++i)
    {
        auto text = "Input " + juce::String (i + 1);
        if (names[i].isNotEmpty() && names[i] != juce::String (i + 1))
            text << " (" << names[i] << ")";
        menu.addItem (i + 1, text, true, current == i);
    }
    menu.addSeparator();
    menu.addItem (1000, "None", current >= 0, false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tbMic),
                        [safe = juce::Component::SafePointer<CentreSection> (this)] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;
        safe->engine.setInterfaceInput (AudioEngine::Patch::talkbackIn, result == 1000 ? -1 : result - 1);
        if (safe->context != nullptr && safe->context->changed)
            safe->context->changed();
        safe->update();
    });
}

TalkButton::TalkButton (std::atomic<bool>& t) : juce::TextButton ("Talk"), talking (t)
{
    setTooltip ("Talkback: hold to talk, double-click to latch (click again to stop)");
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void TalkButton::mouseDown (const juce::MouseEvent&)
{
    // Pressing while latched stops talking; otherwise talk for as long as it is held.
    if (latched)
    {
        latched = false;
        talking.store (false);
    }
    else
    {
        talking.store (true);
    }
    update();
}

void TalkButton::mouseUp (const juce::MouseEvent&)
{
    if (! latched)
        talking.store (false);
    update();
}

void TalkButton::mouseDoubleClick (const juce::MouseEvent&)
{
    // The second press of a double-click latches: talkback stays on after letting go.
    latched = true;
    talking.store (true);
    update();
}

void TalkButton::clicked()
{
    // Keyboard (Space / Enter): latch or stop.
    latched = ! talking.load();
    talking.store (latched);
    update();
}

void updateTapeDisplay (TapeMachine& tape, Cassette& cassette, TapeClock& clock)
{
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
}

//==============================================================================
StripBank::StripBank (const juce::OwnedArray<ChannelStrip>& bankStrips, const std::vector<Param*>& linkParams)
    : channelStrips (bankStrips), links (linkParams)
{
    for (auto* s : channelStrips)
    {
        auto* strip = strips.add (new StripComponent (*s));
        strip->onToggleSection = [this] (int bit)
        {
            setCollapsed (collapsed ^ bit);
            if (onCollapseChanged) onCollapseChanged();
        };
        strip->onOpenStrip = [this, i = strips.size() - 1] { if (onOpenStrip) onOpenStrip (i); };
        addAndMakeVisible (strip);
    }

    meterButton.setTooltip ("Meters show each channel's output (after the fader). Click to show the input (after the trim) instead");
    meterButton.setColour (juce::TextButton::buttonOnColourId, amber);
    meterButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    meterButton.onClick = [this] { setMeterInput (! meterInput); };
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

StripBank::~StripBank()
{
    if (original != nullptr)
        original->copies.removeFirstMatchingValue (this);
}

std::unique_ptr<StripBank> StripBank::makeCopy()
{
    auto copy = std::make_unique<StripBank> (channelStrips, links);
    copy->original = this;
    copy->onOpenStrip = onOpenStrip;
    copy->setCollapsed (collapsed);
    copy->setChainUnlocked (chainUnlocked);
    copy->setMeterInput (meterInput);
    copies.add (copy.get());
    return copy;
}

void StripBank::setMeterInput (bool input)
{
    meterInput = input;
    meterButton.setToggleState (meterInput, juce::dontSendNotification);
    meterButton.setTooltip (meterInput ? "Meters show each channel's input (after the trim). Click to show the output (after the fader) instead"
                                       : "Meters show each channel's output (after the fader). Click to show the input (after the trim) instead");
    for (auto* s : strips)
        s->setMeterInput (meterInput);
}

void StripBank::setChainUnlocked (bool unlocked)
{
    chainUnlocked = unlocked;
    for (auto* s : strips)
        s->setChainUnlocked (unlocked);
    for (auto* c : copies)
        c->setChainUnlocked (unlocked);
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
    meterButton.setBounds (6, getHeight() - stripFaderH + 6, stripW - 12, 18);   // the first strip's meter row
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
    for (auto* c : copies)
        c->updateFrom (*this);
}

void StripBank::updateFrom (const StripBank& source)
{
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->update (source.strips[i]);
}

//==============================================================================
ViewSwitch::ViewSwitch()
{
    const char* const names[] = { "Record", "Mix", "Full" };
    const char* const tips[] = { "Input channels 1-16 and the centre section, for tracking",
                                 "The centre section and the tape returns, for mixing",
                                 "Both banks either side of the centre section" };
    for (int i = 0; i < (int) buttons.size(); ++i)
    {
        auto& b = buttons[(size_t) i];
        b.setButtonText (names[i]);
        b.setTooltip (tips[i]);
        b.setColour (juce::TextButton::buttonOnColourId, amber);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff141516));
        b.onClick = [this, i] { setView (i); if (onChange) onChange (i); };
        // One segmented control: inner edges join.
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < (int) buttons.size() - 1 ? juce::Button::ConnectedOnRight : 0));
    }
}

void ViewSwitch::setView (int view)
{
    for (int i = 0; i < (int) buttons.size(); ++i)
        buttons[(size_t) i].setToggleState (i == view, juce::dontSendNotification);
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
    {
        addChildComponent (getSection ((Section) s));
        getSection ((Section) s).setVisible (isOnDesk ((Section) s));
    }

    // A bank gets shorter or taller when sections are minimised or restored.
    for (auto* bank : { &inputs, &returns })
    {
        bank->addComponentListener (this);
        bank->onCollapseChanged = [this] { if (onCollapseChanged) onCollapseChanged(); };
        const auto s = bank == &inputs ? inputSection : returnSection;
        bank->onOpenStrip = [this, s] (int i) { if (onOpenStrip) onOpenStrip (s, i); };
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
        relayout (false);   // a bank minimised or restored a section
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
    {
        addChildComponent (c);   // takes it back from its window
        c.setVisible (isOnDesk (s));
    }
    else
    {
        removeChildComponent (&c);
    }
    relayout();
}

void Desk::setView (View v)
{
    if (view == v)
        return;
    view = v;
    for (int s = 0; s < numSections; ++s)
        if (docked[(size_t) s])
            getSection ((Section) s).setVisible (isOnDesk ((Section) s));
    relayout();
}

void Desk::relayout (bool sectionMoved)
{
    int width = 2 * cheekW;
    for (int s = 0; s < numSections; ++s)
        if (isOnDesk ((Section) s))
            width += getSection ((Section) s).getWidth();
    if (width == 2 * cheekW)
        width += 600;   // everything is out: room for a short message

    // As tall as the tallest docked section; all sections hang from the top, so a bank with minimised
    // sections collapses upwards.
    int height = 0;
    for (int s = 0; s < numSections; ++s)
        if (isOnDesk ((Section) s))
            height = juce::jmax (height, getSection ((Section) s).getHeight());

    setSize (width, height > 0 ? height : totalH);
    resized();
    if (onSizeChanged)
        onSizeChanged (sectionMoved);
}

void Desk::resized()
{
    int x = cheekW;
    for (int s = 0; s < numSections; ++s)
    {
        if (! isOnDesk ((Section) s))
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

    if (! isOnDesk (inputSection) && ! isOnDesk (centreSection) && ! isOnDesk (returnSection))
    {
        const bool allOut = std::none_of (docked.begin(), docked.end(), [] (bool d) { return d; });
        g.setColour (bridgeText.withAlpha (0.7f));
        g.setFont (ConsoleLookAndFeel::uiFont (22.0f));
        g.drawFittedText (allOut ? "Every section is in its own window.\nUse Sections in the top bar to put them back."
                                 : "This view's sections are in their own windows.\nUse Record, Mix or Full to see the others.",
                          getLocalBounds().reduced (cheekW + 20, 0).withSizeKeepingCentre (getWidth() - 2 * cheekW - 40, 200),
                          juce::Justification::centred, 3);
    }
}

void Desk::timerCallback()
{
    // A bank the view hides rests, unless a copy or a channel window shows its readings.
    if (inputs.isShowing() || inputs.hasCopies() || inputs.readers > 0)    inputs.update();
    if (returns.isShowing() || returns.hasCopies() || returns.readers > 0) returns.update();
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
