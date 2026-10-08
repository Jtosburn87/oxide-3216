#include "PatchBay.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    constexpr int jackW = 28, groupGap = 16, pad = 14, rowH = 64;
    constexpr int topRowY = 10, bottomRowY = 250, fieldH = 330;

    juce::String sourceName (int s)
    {
        using P = AudioEngine::Patch;
        if (s < P::returnOut) return "Channel " + juce::String (s + 1) + " direct out";
        if (s < P::tapeOut)   return "Return " + juce::String (s - P::returnOut + 1) + " direct out";
        if (s < P::auxOut)    return "Tape out " + juce::String (s - P::tapeOut + 1);
        if (s < P::subOut)    return "Aux out " + juce::String (s - P::auxOut + 1);
        if (s < P::mainL)     return "Sub out " + juce::String (s - P::subOut + 1);
        return s == P::mainL ? "Main L" : "Main R";
    }

    juce::String destinationName (int d)
    {
        using P = AudioEngine::Patch;
        if (d < P::returnIn)  return "Tape in " + juce::String (d + 1);
        if (d < P::channelIn) return "Return strip in " + juce::String (d - P::returnIn + 1);
        return "Channel strip in " + juce::String (d - P::channelIn + 1);
    }
}

//==============================================================================
class PatchBayField::Jack : public juce::Button
{
public:
    Jack (PatchBayField& f, bool src, int i, juce::String number)
        : juce::Button (src ? sourceName (i) : destinationName (i)), field (f), isSource (src), index (i), label (std::move (number))
    {
        setTitle (getName());
        setTooltip (getName());
        setWantsKeyboardFocus (true);
    }

    void clicked() override { field.jackClicked (isSource, index); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto hole = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ getWidth() * 0.5f, 12.0f });

        // TRS jack: nickel ring with a black hole, or a coloured plug when patched.
        juce::ColourGradient ring (juce::Colour (0xffc9cdcf), hole.getCentre(), juce::Colour (0xff6c7174),
                                   hole.getCentre().translated (10.0f, 0.0f), true);
        ring.addColour (0.62, juce::Colour (0xffc9cdcf));
        g.setGradientFill (ring);
        g.fillEllipse (hole);
        g.setColour (plugColour.isTransparent() ? juce::Colour (0xff050607) : plugColour);
        g.fillEllipse (hole.reduced (plugColour.isTransparent() ? 6.0f : 4.5f));
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawEllipse (hole.expanded (1.0f), 1.0f);
        }
        if (selected)
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
        g.drawText (label, strip, juce::Justification::centred, false);
    }

    PatchBayField& field;
    const bool isSource;
    const int index;
    const juce::String label;
    juce::Colour plugColour { juce::Colours::transparentBlack };
    bool selected = false;
};

//==============================================================================
juce::Colour PatchBayField::sourceColour (int s)
{
    if (s < Patch::returnOut) return juce::Colour (0xffd6392d);   // channel: red
    if (s < Patch::tapeOut)   return juce::Colour (0xff3a8fd6);   // return: blue
    if (s < Patch::auxOut)    return juce::Colour (0xffe0a83a);   // tape: amber
    if (s < Patch::subOut)    return juce::Colour (0xff5fb83f);   // aux: green
    if (s < Patch::mainL)     return juce::Colour (0xffa35bd6);   // sub: purple
    return juce::Colour (0xff9aa0a3);                             // main: grey
}

PatchBayField::PatchBayField (AudioEngine& e, std::function<void()> changed)
    : engine (e), onChanged (std::move (changed))
{
    for (int s = 0; s < Patch::numSources; ++s)
    {
        juce::String number;
        if (s >= Patch::mainL)          number = s == Patch::mainL ? "L" : "R";
        else if (s >= Patch::subOut)    number = juce::String (s - Patch::subOut + 1);
        else if (s >= Patch::auxOut)    number = juce::String (s - Patch::auxOut + 1);
        else                            number = juce::String (s % 16 + 1);
        addAndMakeVisible (sources.add (new Jack (*this, true, s, number)));
    }
    for (int d = 0; d < Patch::numDestinations; ++d)
        addAndMakeVisible (destinations.add (new Jack (*this, false, d, juce::String (d % 16 + 1))));

    groups.clear();
    layoutRow (sources, { { "Channel direct out", 16 }, { "Return direct out", 16 }, { "Tape out", 16 },
                          { "Aux out", 8 }, { "Sub out", 4 }, { "Main", 2 } }, topRowY);
    layoutRow (destinations, { { "Tape in", 16 }, { "Return strip in", 16 }, { "Channel strip in", 16 } }, bottomRowY);

    int width = 0;
    for (auto& gr : groups)
        width = juce::jmax (width, gr.area.getRight());
    setSize (width + pad, fieldH);
    refresh();
}

PatchBayField::~PatchBayField() = default;

void PatchBayField::layoutRow (juce::OwnedArray<Jack>& jacks, const std::vector<std::pair<juce::String, int>>& groupSizes, int y)
{
    int x = pad, i = 0;
    for (auto& [title, count] : groupSizes)
    {
        groups.push_back ({ title, { x, y, count * jackW, rowH } });
        for (int k = 0; k < count; ++k, ++i)
            jacks[i]->setBounds (x + k * jackW, y + 18, jackW, 40);
        x += count * jackW + groupGap;
    }
}

juce::Point<float> PatchBayField::holeCentre (const Jack& j) const
{
    return j.getBounds().toFloat().getTopLeft().translated (jackW * 0.5f, 12.0f);
}

void PatchBayField::refresh()
{
    for (auto* j : sources)
    {
        bool used = false;
        for (int d = 0; d < Patch::numDestinations; ++d)
            used = used || ((engine.getPatch (d) >> j->index) & 1u);
        j->plugColour = used ? sourceColour (j->index) : juce::Colours::transparentBlack;
        j->selected = j->index == selectedSource;
    }
    for (auto* j : destinations)
    {
        const auto mask = engine.getPatch (j->index);
        j->plugColour = juce::Colours::transparentBlack;
        for (int s = 0; s < Patch::numSources; ++s)
            if ((mask >> s) & 1u) { j->plugColour = sourceColour (s); break; }
    }
    repaint();
}

void PatchBayField::jackClicked (bool isSource, int index)
{
    // As in the prototype: click an output, then an input, to plug a cable.
    // Click a plugged jack to pull its cables (unless an output is waiting for an input).
    if (isSource)
    {
        bool plugged = false;
        for (int d = 0; d < Patch::numDestinations; ++d)
            plugged = plugged || ((engine.getPatch (d) >> index) & 1u);

        if (plugged && selectedSource < 0)
        {
            for (int d = 0; d < Patch::numDestinations; ++d)
                engine.setPatch (d, engine.getPatch (d) & ~(std::uint64_t (1) << index));
        }
        else
        {
            selectedSource = selectedSource == index ? -1 : index;
        }
    }
    else
    {
        if (selectedSource >= 0)
        {
            engine.setPatch (index, engine.getPatch (index) | (std::uint64_t (1) << selectedSource));
            selectedSource = -1;
        }
        else if (engine.getPatch (index) != 0)
        {
            engine.setPatch (index, 0);
        }
    }

    refresh();
    if (onChanged) onChanged();
}

void PatchBayField::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0f10));
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
    const auto cablePath = [this] (juce::Point<float> a, juce::Point<float> b)
    {
        juce::Path p;
        p.startNewSubPath (a);
        p.cubicTo (a.translated (0.0f, 80.0f), b.translated (0.0f, -80.0f), b);
        return p;
    };

    // Normals (dashed) where no cable breaks them.
    for (int i = 0; i < 16; ++i)
    {
        const std::pair<int, int> normals[] = { { Patch::channelOut + i, Patch::tapeIn + i }, { Patch::tapeOut + i, Patch::returnIn + i } };
        for (auto [s, d] : normals)
        {
            if (engine.getPatch (d) != 0)
                continue;
            juce::Path dashed;
            const float dashes[] = { 3.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, cablePath (holeCentre (*sources[s]), holeCentre (*destinations[d])), dashes, 2);
            g.setColour (juce::Colour (0xff6c7174).withAlpha (0.7f));
            g.fillPath (dashed);
        }
    }

    // Cables: shadows first, then the cables, coloured by source.
    struct Cable { int s, d; };
    std::vector<Cable> cables;
    for (int d = 0; d < Patch::numDestinations; ++d)
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
    viewport.setViewedComponent (&field, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    help.setText ("Click an output on the top row, then an input on the bottom row. Click a plugged jack to pull its cable. "
                  "Dashed lines are the normalled paths; a cable into a normalled input breaks the normal.",
                  juce::dontSendNotification);
    help.setFont (ConsoleLookAndFeel::uiFont (13.0f));
    help.setColour (juce::Label::textColourId, bridgeText.withAlpha (0.8f));
    addAndMakeVisible (help);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    clearButton.onClick = [this, &e, changed]
    {
        e.clearPatch();
        field.refresh();
        if (changed) changed();
    };
    clearButton.setTooltip ("Remove every cable (the normals stay)");
    addAndMakeVisible (closeButton);
    addAndMakeVisible (clearButton);

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
    const int w = juce::jmin (getWidth() - 40, field.getWidth() + 24);
    frame = juce::Rectangle<int> (w, fieldH + 84).withCentre (getLocalBounds().getCentre());

    auto r = frame.reduced (12);
    auto header = r.removeFromTop (48);
    header.removeFromLeft (28);
    header.removeFromRight (28);
    closeButton.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 28));
    header.removeFromRight (8);
    clearButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 28));
    header.removeFromLeft (90);
    help.setBounds (header);

    viewport.setBounds (r.withTrimmedTop (4));
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
    if (k == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}
