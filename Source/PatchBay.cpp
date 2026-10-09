#include "PatchBay.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    constexpr int jackW = 28, groupGap = 16, pad = 14;
    constexpr int row1Top = 10, row1Bottom = 116, row2Top = 250, row2Bottom = 356, fieldH = 490;   // room below for hanging cables
    constexpr int dividerY = (row1Bottom + 58 + row2Top) / 2;
    const juce::Colour wiringColour { 0xffe8dcc0 };

    juce::String sourceName (int s)
    {
        using P = AudioEngine::Patch;
        if (s < P::returnOut) return "Channel " + juce::String (s + 1) + " direct out";
        if (s < P::tapeOut)   return "Return " + juce::String (s - P::returnOut + 1) + " direct out";
        if (s < P::auxOut)    return "Tape out " + juce::String (s - P::tapeOut + 1);
        if (s < P::subOut)    return "Aux out " + juce::String (s - P::auxOut + 1);
        if (s < P::mainL)     return "Sub out " + juce::String (s - P::subOut + 1);
        if (s == P::oscillator) return "Oscillator";
        if (s == P::tbLimiterOut) return "TB Limiter Out";
        return s == P::mainL ? "Main L" : "Main R";
    }

    juce::String destinationName (int d)
    {
        using P = AudioEngine::Patch;
        if (d < P::returnIn)  return "Tape in " + juce::String (d + 1);
        if (d < P::channelIn) return "Return strip in " + juce::String (d - P::returnIn + 1);
        if (d == P::talkbackIn)  return "TB In (the talkback mic)";
        if (d == P::tbLimiterIn) return "TB Limiter In (normalled from TB In)";
        if (d == P::tbSend)      return "TB Send (to the talkback auxes; normalled from TB Limiter Out)";
        if (d >= P::interfaceOut) return "Interface out " + juce::String (d - P::interfaceOut + 1);
        return "Channel strip in " + juce::String (d - P::channelIn + 1);
    }

    /** A cable hangs below both of its ends. */
    juce::Path cablePath (juce::Point<float> a, juce::Point<float> b)
    {
        const float low = juce::jmax (a.y, b.y) + 22.0f + 0.08f * a.getDistanceFrom (b);
        juce::Path p;
        p.startNewSubPath (a);
        p.cubicTo ({ a.x, low }, { b.x, low }, b);
        return p;
    }
}

//==============================================================================
class PatchBayField::Jack : public juce::Button
{
public:
    Jack (PatchBayField& f, Kind k, int i, const juce::String& name, juce::String number)
        : juce::Button (name), field (f), kind (k), index (i), label (std::move (number))
    {
        setTitle (getName());
        setTooltip (getName());
        setWantsKeyboardFocus (true);
    }

    void clicked (const juce::ModifierKeys& mods) override
    {
        if (mods.isPopupMenu() && kind != Kind::destination)
            field.pullCables (kind, index);   // right-click an output to pull all of its cables
        else
            field.jackClicked (kind, index);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && kind != Kind::destination)
        {
            field.pullCables (kind, index);
            return true;
        }
        return juce::Button::keyPressed (k);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool dim = ! isEnabled();
        if (dim)
            g.beginTransparencyLayer (0.35f);

        const auto hole = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ getWidth() * 0.5f, 12.0f });

        // TRS jack: nickel ring with a black hole, or a coloured plug when patched.
        juce::ColourGradient ring (juce::Colour (0xffc9cdcf), hole.getCentre(), juce::Colour (0xff6c7174),
                                   hole.getCentre().translated (10.0f, 0.0f), true);
        ring.addColour (0.62, juce::Colour (0xffc9cdcf));
        g.setGradientFill (ring);
        g.fillEllipse (hole);
        g.setColour (plugColour.isTransparent() ? juce::Colour (0xff050607) : plugColour);
        g.fillEllipse (hole.reduced (plugColour.isTransparent() ? 6.0f : 4.5f));
        if (over && ! dim)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawEllipse (hole.expanded (1.0f), 1.0f);
        }
        if (field.selected && field.selected->kind == kind && field.selected->index == index)
        {
            g.setColour (focus);
            g.drawEllipse (hole.expanded (2.0f), 2.0f);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (focus);
            g.drawRect (getLocalBounds().toFloat(), 1.5f);
        }

        // Paper designation strip.
        const auto strip = juce::Rectangle<float> (24.0f, 12.0f).withCentre ({ getWidth() * 0.5f, 31.0f });
        g.setColour (paper);
        g.fillRect (strip);
        g.setColour (juce::Colour (0xff111111));
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
        g.drawFittedText (label, strip.toNearestInt(), juce::Justification::centred, 1, 0.6f);   // SEND squeezes to fit

        if (dim)
            g.endTransparencyLayer();
    }

    PatchBayField& field;
    const Kind kind;
    const int index;
    const juce::String label;
    juce::Colour plugColour { juce::Colours::transparentBlack };
};

//==============================================================================
juce::Colour PatchBayField::sourceColour (int s)
{
    if (s == Patch::oscillator) return juce::Colour (0xff3fc1b8); // oscillator: teal
    if (s == Patch::tbLimiterOut) return juce::Colour (0xffff7a2e); // talkback limiter: orange
    if (s < Patch::returnOut) return juce::Colour (0xffd6392d);   // channel: red
    if (s < Patch::tapeOut)   return juce::Colour (0xff3a8fd6);   // return: blue
    if (s < Patch::auxOut)    return juce::Colour (0xffe0a83a);   // tape: amber
    if (s < Patch::subOut)    return juce::Colour (0xff5fb83f);   // aux: green
    if (s < Patch::mainL)     return juce::Colour (0xffa35bd6);   // sub: purple
    return juce::Colour (0xff9aa0a3);                             // main: grey
}

PatchBayField::PatchBayField (AudioEngine& e, std::function<void()> changedCallback)
    : engine (e), onChanged (std::move (changedCallback)),
      oscFreqKnob (*e.oscFreq, "Freq", juce::Colour (0xff3fc1b8)), oscLevelKnob (*e.oscLevel, "Level")
{
    for (int s = 0; s < Patch::numSources; ++s)
    {
        juce::String number;
        if (s == Patch::oscillator)     number = "OSC";
        else if (s == Patch::tbLimiterOut) number = "OUT";
        else if (s >= Patch::mainL)     number = s == Patch::mainL ? "L" : "R";
        else if (s >= Patch::subOut)    number = juce::String (s - Patch::subOut + 1);
        else if (s >= Patch::auxOut)    number = juce::String (s - Patch::auxOut + 1);
        else                            number = juce::String (s % 16 + 1);
        addAndMakeVisible (sources.add (new Jack (*this, Kind::source, s, sourceName (s), number)));
    }
    for (int d = 0; d < Patch::numDestinations; ++d)
    {
        juce::String number;
        if (d == Patch::talkbackIn || d == Patch::tbLimiterIn) number = "IN";
        else if (d == Patch::tbSend)                           number = "SEND";
        else if (d >= Patch::interfaceOut)                     number = juce::String (d - Patch::interfaceOut + 1);
        else                                                   number = juce::String (d % 16 + 1);
        addChildComponent (destinations.add (new Jack (*this, Kind::destination, d, destinationName (d), number)));
    }

    // Oscillator: waveform switch, frequency sweep (sine) and level.
    const char* const typeNames[] = { "Sine", "White", "Pink" };
    for (int t = 0; t < 3; ++t)
    {
        auto* b = oscTypeButtons.add (new RadioParamButton (*e.oscType, t, typeNames[t]));
        b->setTooltip (t == 0 ? "Sine wave, frequency set by the Freq knob" : juce::String (typeNames[t]) + " noise");
        addAndMakeVisible (b);
    }
    oscFreqKnob.setFormatter ([] (float hz)
    {
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                            : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    });
    oscFreqKnob.setEndLabels ("20", "20k");
    oscFreqKnob.setDialSize (40);
    oscLevelKnob.setFormatter ([] (float db) { return juce::String (juce::roundToInt (db)) + " dB"; });
    oscLevelKnob.setEndLabels ("-60", "0");
    oscLevelKnob.setDialSize (40);
    addAndMakeVisible (oscFreqKnob);
    addAndMakeVisible (oscLevelKnob);
    e.oscType->addListener (this);
    paramChanged (*e.oscType);

    refresh();
}

PatchBayField::~PatchBayField()
{
    engine.oscType->removeListener (this);
}

void PatchBayField::paramChanged (Param&)
{
    const bool sine = juce::roundToInt (engine.oscType->get()) == Dsp::Oscillator::sine;
    oscFreqKnob.setEnabled (sine);
    oscFreqKnob.setAlpha (sine ? 1.0f : 0.35f);
}

void PatchBayField::placeGroup (const juce::String& title, juce::OwnedArray<Jack>& jacks, int first, int count, int x, int y)
{
    if (count <= 0)
        return;
    // A group is never narrower than its title (a two-jack interface still reads "Interface in").
    const int titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (ConsoleLookAndFeel::uiFont (12.0f), title)) + 4;
    groups.push_back ({ title, { x, y, juce::jmax (count * jackW, titleW), 58 } });
    for (int k = 0; k < count; ++k)
        jacks[first + k]->setBounds (x + k * jackW, y + 18, jackW, 40);
}

void PatchBayField::rebuild (int interfaceJacks, int outputJacks)
{
    numInterfaceJacks = interfaceJacks;
    numOutputJacks = outputJacks;
    interfaceIns.clear();
    for (int i = 0; i < interfaceJacks; ++i)
    {
        const auto deviceName = interfaceNames[i];
        auto name = "Interface in " + juce::String (i + 1);
        if (deviceName.isNotEmpty() && deviceName != juce::String (i + 1))
            name << " (" << deviceName << ")";
        addAndMakeVisible (interfaceIns.add (new Jack (*this, Kind::interfaceIn, i, name, juce::String (i + 1))));
    }

    groups.clear();
    using P = Patch;

    // Pair 1. Tape in sits under the channel direct outs that are normalled to it.
    const int x2 = pad + juce::jmax (interfaceJacks, 16) * jackW + groupGap;
    placeGroup ("Interface in", interfaceIns, 0, interfaceJacks, pad, row1Top);
    placeGroup ("Channel direct out", sources, P::channelOut, 16, x2, row1Top);
    placeGroup ("Channel strip in", destinations, P::channelIn, 16, pad, row1Bottom);
    placeGroup ("Tape in", destinations, P::tapeIn, 16, x2, row1Bottom);

    // Next to them: Talkback (TB In, TB Send) on the top row, the TB Limiter (In, Out) under it.
    // Normals: TB In -> TB Limiter In, TB Limiter Out -> TB Send (the limiter cannot be patched into itself).
    const int x3 = x2 + 16 * jackW + groupGap;
    const auto pairOf = [this] (const juce::String& title, Jack& a, Jack& b, int x, int y)
    {
        groups.push_back ({ title, { x, y, 2 * jackW + 12, 58 } });
        a.setBounds (x, y + 18, jackW, 40);
        b.setBounds (x + jackW, y + 18, jackW, 40);
    };
    pairOf ("Talkback", *destinations[P::talkbackIn], *destinations[P::tbSend], x3, row1Top);
    pairOf ("TB Limiter", *destinations[P::tbLimiterIn], *sources[P::tbLimiterOut], x3, row1Bottom);

    // The oscillator at the end of pair 1: its output jack, then the waveform switch over the two knobs.
    const int x4 = x3 + 2 * jackW + 12 + groupGap;
    placeGroup ("Oscillator", sources, P::oscillator, 1, x4, row1Top);
    groups.back().area.setWidth (36 + 3 * 54);
    for (int t = 0; t < oscTypeButtons.size(); ++t)
        oscTypeButtons[t]->setBounds (x4 + 36 + t * 54, row1Top + 20, 50, 22);
    oscFreqKnob.setBounds (x4 + 44, row1Top + 52, 64, 56);
    oscLevelKnob.setBounds (x4 + 128, row1Top + 52, 64, 56);

    // Pair 2. Return strip in sits under the tape outs that are normalled to it.
    int x = pad;
    placeGroup ("Tape out", sources, P::tapeOut, 16, x, row2Top);          x += 16 * jackW + groupGap;
    placeGroup ("Return direct out", sources, P::returnOut, 16, x, row2Top); x += 16 * jackW + groupGap;
    placeGroup ("Aux out", sources, P::auxOut, 8, x, row2Top);             x += 8 * jackW + groupGap;
    placeGroup ("Sub out", sources, P::subOut, 4, x, row2Top);             x += 4 * jackW + groupGap;
    placeGroup ("Main", sources, P::mainL, 2, x, row2Top);
    placeGroup ("Return strip in", destinations, P::returnIn, 16, pad, row2Bottom);

    // Interface outs beside them, as many as the device has.
    placeGroup ("Interface out", destinations, P::interfaceOut, outputJacks, pad + 16 * jackW + groupGap, row2Bottom);
    for (auto* j : destinations)
        j->setVisible (isShown (Kind::destination, j->index));

    int width = 0;
    for (auto& gr : groups)
        width = juce::jmax (width, gr.area.getRight());
    setSize (width + pad, fieldH);
}

bool PatchBayField::isShown (Kind kind, int index) const
{
    return kind != Kind::destination || index < Patch::interfaceOut || index - Patch::interfaceOut < numOutputJacks;
}

juce::Point<float> PatchBayField::holeCentre (const Jack& j) const
{
    return j.getBounds().toFloat().getTopLeft().translated (jackW * 0.5f, 12.0f);
}

bool PatchBayField::isPlugged (Kind kind, int index) const
{
    if (kind == Kind::source)
    {
        for (int d = 0; d < Patch::numDestinations; ++d)
            if ((engine.getPatch (d) >> index) & 1u)
                return true;
        return false;
    }
    if (kind == Kind::interfaceIn)
    {
        for (int d = 0; d < Patch::numDestinations; ++d)
            if (engine.getInterfaceInput (d) == index)
                return true;
        return false;
    }
    return engine.getPatch (index) != 0 || engine.getInterfaceInput (index) >= 0;
}

void PatchBayField::refresh()
{
    juce::StringArray names, outNames;
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device != nullptr)
    {
        names = device->getInputChannelNames();
        outNames = device->getOutputChannelNames();
    }
    // The interface jacks follow the device: as many ins and outs as it has (16 / 8 with no device).
    const int wanted = device != nullptr ? juce::jmin (names.size(), maxInterfaceJacks) : 16;
    const int wantedOuts = device != nullptr ? juce::jmin (outNames.size(), Patch::numInterfaceOuts) : 8;
    if (wanted != numInterfaceJacks || names != interfaceNames || wantedOuts != numOutputJacks || outNames != outputNames)
    {
        interfaceNames = names;
        outputNames = outNames;
        rebuild (wanted, wantedOuts);
        for (int k = 0; k < Patch::numInterfaceOuts; ++k)
        {
            auto name = "Interface out " + juce::String (k + 1);
            if (outNames[k].isNotEmpty() && outNames[k] != juce::String (k + 1))
                name << " (" << outNames[k] << ")";
            destinations[Patch::interfaceOut + k]->setTooltip (name + ": anything patched here is added to this output");
        }
        if (auto* parent = getParentComponent())
            parent->resized();
    }
    for (auto* j : interfaceIns)
        j->setEnabled (device != nullptr);

    for (auto* j : sources)
        j->plugColour = isPlugged (Kind::source, j->index) ? sourceColour (j->index) : juce::Colours::transparentBlack;
    for (auto* j : interfaceIns)
        j->plugColour = isPlugged (Kind::interfaceIn, j->index) ? wiringColour : juce::Colours::transparentBlack;
    for (auto* j : destinations)
    {
        const auto mask = engine.getPatch (j->index);
        j->plugColour = juce::Colours::transparentBlack;
        if (engine.getInterfaceInput (j->index) >= 0)
            j->plugColour = wiringColour;
        for (int s = 0; s < Patch::numSources; ++s)
            if ((mask >> s) & 1u) { j->plugColour = sourceColour (s); break; }
    }
    repaint();
}

void PatchBayField::changed()
{
    refresh();
    if (onChanged) onChanged();
}

void PatchBayField::mapInterfaceOneToOne()
{
    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
        engine.setInputSource (c, c);
    changed();
}

void PatchBayField::pullCables (Kind kind, int index)
{
    if (kind == Kind::source)
        for (int d = 0; d < Patch::numDestinations; ++d)
            engine.setPatch (d, engine.getPatch (d) & ~(std::uint64_t (1) << index));
    else if (kind == Kind::interfaceIn)
        for (int d = 0; d < Patch::numDestinations; ++d)
            if (engine.getInterfaceInput (d) == index)
                engine.setInterfaceInput (d, -1);
    if (selected && selected->kind == kind && selected->index == index)
        selected.reset();
    changed();
}

bool PatchBayField::cancelSelection()
{
    if (! selected)
        return false;
    selected.reset();
    changed();
    return true;
}

void PatchBayField::mouseDown (const juce::MouseEvent&)
{
    cancelSelection();   // click the bare field to put a picked-up output down
}

void PatchBayField::jackClicked (Kind kind, int index)
{
    // Click an output (or an interface input) to pick it up, then click inputs below to plug cables from it.
    // It stays picked up so one output can feed several inputs; click it again (or press Escape) to put it down.
    // With an output picked up, clicking an input it already feeds pulls that one cable.
    if (kind != Kind::destination)
    {
        if (selected && selected->kind == kind && selected->index == index)
            selected.reset();
        else
            selected = Selection { kind, index };
        changed();
        return;
    }

    if (selected && selected->kind == Kind::source)
        engine.setPatch (index, engine.getPatch (index) ^ (std::uint64_t (1) << selected->index));
    else if (selected && selected->kind == Kind::interfaceIn)
        engine.setInterfaceInput (index, engine.getInterfaceInput (index) == selected->index ? -1 : selected->index);
    else if (engine.getPatch (index) != 0)
        engine.setPatch (index, 0);   // patch cables come out first, then the interface input
    else
        engine.setInterfaceInput (index, -1);

    changed();
}

void PatchBayField::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0f10));
    g.setColour (juce::Colour (0xff2a2d30));
    g.fillRect (0, dividerY - 1, getWidth(), 3);

    g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    for (auto& gr : groups)
    {
        g.setColour (juce::Colour (0xffc9cdcf));
        g.drawText (gr.title, gr.area.withHeight (14), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff3a3e41));
        g.fillRect (gr.area.getX(), gr.area.getY() + 15, gr.area.getWidth(), 1);
    }
}

void PatchBayField::paintOverChildren (juce::Graphics& g)
{
    // Normals: dashed, straight down from the output's label to the input below, unless a cable breaks them.
    const float dashes[] = { 3.0f, 4.0f };
    g.setColour (juce::Colour (0xff6c7174).withAlpha (0.8f));
    for (int i = 0; i < 16; ++i)
    {
        const std::pair<int, int> normals[] = { { Patch::channelOut + i, Patch::tapeIn + i }, { Patch::tapeOut + i, Patch::returnIn + i } };
        for (auto [s, d] : normals)
        {
            if (engine.getPatch (d) != 0 || engine.getInterfaceInput (d) >= 0)
                continue;
            const auto from = holeCentre (*sources[s]).translated (0.0f, 26.0f);
            const auto to = holeCentre (*destinations[d]).translated (0.0f, -11.0f);
            juce::Path line, dashed;
            line.startNewSubPath (from);
            line.lineTo (to);
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
            g.fillPath (dashed);
        }
    }

    // Interface inputs: thin wiring into whatever they feed.
    g.setColour (wiringColour.withAlpha (0.75f));
    for (int d = 0; d < Patch::numDestinations; ++d)
    {
        const int dev = engine.getInterfaceInput (d);
        if (juce::isPositiveAndBelow (dev, interfaceIns.size()) && destinations[d]->isVisible())
            g.strokePath (cablePath (holeCentre (*interfaceIns[dev]), holeCentre (*destinations[d])),
                          juce::PathStrokeType (1.5f));
    }

    // Cables: shadows first, then the cables, coloured by source.
    struct Cable { int s, d; };
    std::vector<Cable> cables;
    for (int d = 0; d < Patch::numDestinations; ++d)
        if (destinations[d]->isVisible())   // an interface out the current device does not have
            for (int s = 0; s < Patch::numSources; ++s)
                if ((engine.getPatch (d) >> s) & 1u)
                    cables.push_back ({ s, d });

    const juce::PathStrokeType shadowStroke (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType cableStroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    for (auto c : cables)
        g.strokePath (cablePath (holeCentre (*sources[c.s]), holeCentre (*destinations[c.d])), shadowStroke,
                      juce::AffineTransform::translation (2.0f, 4.0f));
    for (auto c : cables)
    {
        g.setColour (sourceColour (c.s).withAlpha (0.95f));
        g.strokePath (cablePath (holeCentre (*sources[c.s]), holeCentre (*destinations[c.d])), cableStroke);
    }
}

//==============================================================================
PatchBayPanel::PatchBayPanel (AudioEngine& e, std::function<void()> changed)
    : field (e, changed)
{
    addAndMakeVisible (field);

    help.setText ("Click an output, then as many inputs below it as you like (click the output again or press Escape when done). "
                  "Click an input to pull its cable; right-click an output to pull all of its cables. "
                  "Dashed lines are normals. Thin lines are interface inputs; into a Tape in they replace the channel direct out.",
                  juce::dontSendNotification);
    help.setFont (ConsoleLookAndFeel::uiFont (13.0f));
    help.setColour (juce::Label::textColourId, bridgeText.withAlpha (0.8f));
    help.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (help);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    clearButton.onClick = [this, &e, changed]
    {
        e.clearPatch();
        field.refresh();
        if (changed) changed();
    };
    clearButton.setTooltip ("Remove every patch cable (the normals and the interface inputs stay)");
    mapButton.onClick = [this] { field.mapInterfaceOneToOne(); };
    mapButton.setTooltip ("Channel strip 1 from interface input 1, channel strip 2 from input 2, and so on");
    addAndMakeVisible (closeButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (mapButton);

    setWantsKeyboardFocus (true);
    setTitle ("Patch bay");
}

void PatchBayPanel::visibilityChanged()
{
    if (isVisible())
    {
        field.refresh();
        grabKeyboardFocus();
    }
}

void PatchBayPanel::resized()
{
    constexpr int margin = 20, inset = 12, headerH = 48, helpH = 22;

    // The field scales down (never up) to fit the panel.
    const int availW = getWidth() - 2 * (margin + inset);
    const int availH = getHeight() - 2 * (margin + inset) - headerH - helpH;
    const float scale = juce::jlimit (0.3f, 1.0f, juce::jmin ((float) availW / (float) field.getWidth(),
                                                              (float) availH / (float) field.getHeight()));
    const int fieldW = juce::roundToInt ((float) field.getWidth() * scale);
    const int scaledH = juce::roundToInt ((float) field.getHeight() * scale);

    frame = juce::Rectangle<int> (juce::jmax (fieldW, 900) + 2 * inset, scaledH + headerH + helpH + 2 * inset)
                .withCentre (getLocalBounds().getCentre());

    auto r = frame.reduced (inset);
    auto header = r.removeFromTop (headerH);
    header.removeFromLeft (28);
    header.removeFromRight (28);
    closeButton.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 28));
    header.removeFromRight (8);
    clearButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 28));
    header.removeFromRight (8);
    mapButton.setBounds (header.removeFromRight (130).withSizeKeepingCentre (130, 28));
    help.setBounds (r.removeFromTop (helpH).withTrimmedLeft (16));

    const auto fieldArea = r.withSizeKeepingCentre (fieldW, scaledH);
    field.setTransform (juce::AffineTransform::scale (scale).translated ((float) fieldArea.getX(), (float) fieldArea.getY()));
}

void PatchBayPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.65f));

    const auto f = frame.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (f.translated (0.0f, 10.0f).expanded (6.0f), 6.0f);
    g.setColour (juce::Colour (0xff1a1c1e));
    g.fillRoundedRectangle (f, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (f, 3.0f, 2.0f);
    g.setColour (juce::Colour (0xff34383b));
    g.drawRoundedRectangle (f.reduced (2.0f), 3.0f, 2.0f);

    // Rack screws.
    for (auto p : { f.getTopLeft().translated (16.0f, 16.0f), f.getTopRight().translated (-16.0f, 16.0f) })
    {
        juce::ColourGradient screw (juce::Colour (0xffdddddd), p.translated (-2.0f, -2.0f), juce::Colour (0xff555555),
                                    p.translated (6.0f, 0.0f), true);
        g.setGradientFill (screw);
        g.fillEllipse (p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
    }

    g.setColour (bridgeText);
    g.setFont (ConsoleLookAndFeel::uiFont (17.0f, true));
    g.drawText ("Patch bay", frame.reduced (12).removeFromTop (48).withTrimmedLeft (28).withWidth (90), juce::Justification::centredLeft, false);
}

void PatchBayPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! frame.contains (e.getPosition()) && onClose)
        onClose();
}

bool PatchBayPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && field.cancelSelection())
        return true;   // the first Escape puts a picked-up output down
    if (k == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}
