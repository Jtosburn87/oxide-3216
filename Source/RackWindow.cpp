#include "RackWindow.h"
#include "PluginWindows.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    /** Faceplate finishes, picked per manufacturer so one maker's gear looks alike. */
    struct Finish { juce::Colour plate, text; bool brushed; };
    const Finish finishes[] = {
        { juce::Colour (0xff1c1e20), juce::Colour (0xffd9dcdd), false },   // black anodised
        { juce::Colour (0xffb7babc), juce::Colour (0xff1b1c1d), true  },   // brushed aluminium
        { juce::Colour (0xff3b5163), juce::Colour (0xffeef1f2), false },   // blue-grey
        { juce::Colour (0xffd6ccb0), juce::Colour (0xff2a2622), false },   // cream
        { juce::Colour (0xff5b2622), juce::Colour (0xffeedfc6), false },   // oxblood
        { juce::Colour (0xff4b5a4e), juce::Colour (0xffe6e9e2), false },   // military green
        { juce::Colour (0xff8d8f91), juce::Colour (0xff111213), true  },   // dark brushed
        { juce::Colour (0xff2c3a52), juce::Colour (0xffd8c58e), false },   // navy with gold legend
    };
    const juce::Colour blankPlate { 0xff2a2c2e };

    const Finish& finishFor (const juce::String& key)
    {
        return finishes[(size_t) ((unsigned) key.hashCode() % (unsigned) std::size (finishes))];
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> c, float angle)
    {
        const float r = 4.2f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (c.x - r - 0.5f, c.y - r + 0.8f, 2 * r + 1.0f, 2 * r + 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e4e5), c.x - r, c.y - r,
                                                 juce::Colour (0xff6c7073), c.x + r, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        const auto d = juce::Point<float> (std::cos (angle), std::sin (angle)) * (r - 1.0f);
        g.setColour (juce::Colour (0xff2a2c2e));
        g.drawLine ({ c - d, c + d }, 1.4f);
    }
}

//==============================================================================
RackUnit::RackUnit (InsertChain& c, int s, PluginContext& ctx, Param* main, std::function<bool()> isIdle)
    : chain (c), slot (s), context (ctx), mainAssign (main), idle (std::move (isIdle))
{
    setTitle (chain.name + " slot " + juce::String (slot + 1));
    list.setSlot (slot);
    list.onClick = [this] { InsertSlot::choose (chain, slot, context, mainAssign, list); };
    bypass.setTooltip ("Bypass this plugin");
    bypass.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
    bypass.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    bypass.onClick = [this] { chain.setBypassed (slot, ! chain.isBypassed (slot)); update(); };
    addAndMakeVisible (list);
    addChildComponent (bypass);
    update();
}

RackUnit::Shown RackUnit::read() const
{
    Shown s;
    s.idle = idle != nullptr && idle();
    s.loaded = chain.getProcessor (slot) != nullptr;
    if (s.loaded)
    {
        s.name = chain.getDescription (slot).name;
        s.maker = chain.getDescription (slot).manufacturerName;
    }
    s.bypassed = s.loaded && chain.isBypassed (slot);
    s.refused = s.loaded && chain.isStereoRefused (slot);
    s.running = s.loaded && ! s.bypassed && ! s.refused && ! s.idle;
    return s;
}

void RackUnit::update()
{
    const auto s = read();
    if (initialised && s == shown)
        return;
    initialised = true;
    shown = s;

    list.setEnabled (! s.idle);
    bypass.setVisible (s.loaded && ! s.idle);
    bypass.setToggleState (s.bypassed, juce::dontSendNotification);
    setMouseCursor (s.idle ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    setTooltip (s.idle ? juce::String ("Not used while this aux pair is linked")
              : s.loaded ? "Click to open " + s.name + ". Right-click or the list button to change it"
                         : "Click to add a plugin to slot " + juce::String (slot + 1));
    repaint();
}

void RackUnit::resized()
{
    const auto area = getLocalBounds().reduced (earWidth, 0);
    list.setBounds (area.getX() + 8, (getHeight() - 24) / 2, 28, 24);
    bypass.setBounds (area.getRight() - 8 - 42, (getHeight() - 22) / 2, 42, 22);
}

void RackUnit::mouseUp (const juce::MouseEvent& e)
{
    if (shown.idle || ! e.mouseWasClicked())
        return;
    if (e.mods.isPopupMenu())
        InsertSlot::choose (chain, slot, context, mainAssign, *this);
    else
        InsertSlot::click (chain, slot, context, mainAssign, *this);
}

void RackUnit::paintEar (juce::Graphics& g, juce::Rectangle<float> ear, juce::Colour face) const
{
    g.setColour (face.darker (0.18f));
    g.fillRect (ear);
    // Slotted holes with a screw in each, as on 1U rack ears.
    const float seed = (float) (chain.name.hashCode() % 97 + slot * 13);
    for (int i = 0; i < 2; ++i)
    {
        const juce::Point<float> c (ear.getCentreX(), ear.getY() + ear.getHeight() * (i == 0 ? 0.27f : 0.73f));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (c.x - 7.0f, c.y - 4.5f, 14.0f, 9.0f, 4.5f);
        drawScrew (g, c, seed + (float) i * 1.7f);
    }
}

void RackUnit::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.0f, 0.5f);
    const bool blank = ! shown.loaded || shown.idle;
    const auto& finish = finishFor (shown.maker.isNotEmpty() ? shown.maker : shown.name);
    const auto plateColour = blank ? blankPlate : finish.plate;
    const auto text = blank ? legend.withAlpha (0.45f) : finish.text;

    // Faceplate: a slight top-to-bottom shade, brushed grain on metal finishes, bevelled edges.
    g.setGradientFill (juce::ColourGradient (plateColour.brighter (0.08f), 0, b.getY(), plateColour.darker (0.12f), 0, b.getBottom(), false));
    g.fillRect (b);
    if (! blank && finish.brushed)
    {
        juce::Random grain (finish.plate.getARGB() + (juce::uint32) slot);
        for (float y = b.getY() + 1.0f; y < b.getBottom() - 1.0f; y += 1.5f)
        {
            g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.03f + 0.04f * grain.nextFloat()));
            g.fillRect (b.getX(), y, b.getWidth(), 0.7f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect (b.getX(), b.getY(), b.getWidth(), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (b.getX(), b.getBottom() - 1.0f, b.getWidth(), 1.0f);

    paintEar (g, b.withWidth ((float) earWidth), plateColour);
    paintEar (g, b.withLeft (b.getRight() - (float) earWidth), plateColour);

    auto face = b.reduced ((float) earWidth, 0.0f);
    face.removeFromLeft (8.0f + 28.0f + 10.0f);                      // list button
    face.removeFromRight (shown.loaded && ! shown.idle ? 8.0f + 42.0f + 10.0f : 10.0f);   // BYP

    // LED and slot number.
    const auto ledArea = face.removeFromLeft (22.0f);
    const juce::Point<float> led (ledArea.getCentreX(), b.getCentreY() - 5.0f);
    const auto ledColour = shown.running  ? juce::Colour (0xff47e06a)
                         : shown.refused  ? amber
                         : shown.bypassed ? juce::Colour (0xffe0463a)
                                          : juce::Colour (0xff3a2a2a);
    if (shown.running || shown.bypassed || shown.refused)
    {
        g.setColour (ledColour.withAlpha (0.28f));
        g.fillEllipse (led.x - 7.5f, led.y - 7.5f, 15.0f, 15.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (led.x - 5.0f, led.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ledColour);
    g.fillEllipse (led.x - 3.6f, led.y - 3.6f, 7.2f, 7.2f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillEllipse (led.x - 2.0f, led.y - 2.6f, 2.4f, 2.0f);
    g.setColour (text.withMultipliedAlpha (0.7f));
    g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
    g.drawText (juce::String (slot + 1), juce::Rectangle<float> (ledArea.getX(), led.y + 6.0f, ledArea.getWidth(), 11.0f),
                juce::Justification::centred, false);
    face.removeFromLeft (8.0f);

    if (blank)
    {
        // Blank panel: a row of vent slots behind the legend.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (float x = face.getX(); x + 16.0f < face.getRight(); x += 22.0f)
        {
            g.fillRoundedRectangle (x, b.getCentreY() - 2.0f, 16.0f, 4.0f, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (x + 1.0f, b.getCentreY() + 2.0f, 14.0f, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
        }
        const auto legendText = shown.idle ? juce::String ("LINKED PAIR: NOT USED")
                              : isMouseOver (true) ? juce::String ("CLICK TO ADD A PLUGIN") : juce::String ("EMPTY");
        g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), legendText) + 16.0f;
        const auto chip = juce::Rectangle<float> (face.getX(), b.getCentreY() - 9.0f, w, 18.0f);
        g.setColour (plateColour);
        g.fillRect (chip);
        g.setColour (text);
        g.drawText (legendText, chip, juce::Justification::centred, false);
        return;
    }

    // Maker above, the plugin's name below, in the gear's legend colour.
    const auto top = face.removeFromTop (face.getHeight() * 0.42f);
    g.setColour (shown.refused ? amber : text.withMultipliedAlpha (0.7f));
    g.setFont (ConsoleLookAndFeel::uiFont (10.5f, true));
    g.drawFittedText (shown.refused ? juce::String ("NOT STEREO: OFF WHILE LINKED") : shown.maker.toUpperCase(),
                      top.toNearestInt().withTrimmedTop (4), juce::Justification::bottomLeft, 1, 0.8f);
    g.setColour (shown.bypassed || shown.refused ? text.withMultipliedAlpha (0.45f) : text);
    g.setFont (ConsoleLookAndFeel::uiFont (16.0f, true));
    g.drawFittedText (shown.name, face.toNearestInt().withTrimmedBottom (4), juce::Justification::topLeft, 1, 0.75f);

    if (isMouseOver (true))
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (b);
    }
}

//==============================================================================
RackView::RackView (AudioEngine& e, PluginContext& context) : engine (e)
{
    const auto addChain = [&] (Rack& rack, InsertChain* chain, Param* mainAssign,
                               std::function<juce::String()> label, std::function<bool()> idle)
    {
        rack.chains.push_back ({ chain, std::move (label), idle, {} });
        for (int s = 0; s < InsertChain::numSlots; ++s)
            addAndMakeVisible (units.add (new RackUnit (*chain, s, context, mainAssign, idle)));
    };

    Rack subs { "SUBS", {}, {} };
    for (int k = 0; k < Config::numSubs; ++k)
        addChain (subs, engine.subInserts[k], nullptr, [this, k] { return engine.subNames[(size_t) k]; }, nullptr);
    racks.push_back (std::move (subs));

    for (int first = 0; first < Config::numAux; first += 4)
    {
        Rack aux { "AUX " + juce::String (first + 1) + "-" + juce::String (juce::jmin (first + 4, Config::numAux)), {}, {} };
        for (int k = first; k < juce::jmin (first + 4, Config::numAux); ++k)
        {
            const int pair = k / 2;
            const bool odd = k % 2 == 0;   // A1, A3, ...: the one that carries a linked pair
            addChain (aux, engine.auxInserts[k], engine.auxMain[(size_t) k],
                      [this, k, pair, odd]
                      {
                          if (! engine.auxLink[(size_t) pair]->on())
                              return engine.auxNames[(size_t) k];
                          return odd ? engine.auxNames[(size_t) k] + " + " + engine.auxNames[(size_t) k + 1] + "  (stereo)"
                                     : engine.auxNames[(size_t) k] + "  (linked to " + engine.auxNames[(size_t) k - 1] + ")";
                      },
                      odd ? std::function<bool()>() : std::function<bool()> ([this, pair] { return engine.auxLink[(size_t) pair]->on(); }));
        }
        racks.push_back (std::move (aux));
    }

    Rack master { "MASTER", {}, {} };
    addChain (master, &engine.masterInserts, nullptr, [] { return juce::String ("Master"); }, nullptr);
    racks.push_back (std::move (master));

    shownLabels = labels();
    startTimerHz (5);
}

int RackView::rackHeight (const Rack& r) const
{
    return titleH + (int) r.chains.size() * (headerH + InsertChain::numSlots * RackUnit::height) + 16;
}

int RackView::widthForOneRow() const
{
    const int n = (int) racks.size();
    return 2 * margin + n * rackWidth + (n - 1) * gap;
}

void RackView::layoutFor (int width)
{
    width = juce::jmax (width, rackWidth + 2 * margin);
    int x = margin, y = margin, rowH = 0, unit = 0;
    for (auto& rack : racks)
    {
        if (x > margin && x + rackWidth > width - margin)
        {
            x = margin;
            y += rowH + gap;
            rowH = 0;
        }
        const int h = rackHeight (rack);
        rack.bounds = { x, y, rackWidth, h };
        int cy = y + titleH;
        for (auto& c : rack.chains)
        {
            c.header = { x, cy, rackWidth, headerH };
            cy += headerH;
            for (int s = 0; s < InsertChain::numSlots; ++s, cy += RackUnit::height)
                units[unit++]->setBounds (x, cy, rackWidth, RackUnit::height);
        }
        x += rackWidth + gap;
        rowH = juce::jmax (rowH, h);
    }
    setSize (width, y + rowH + margin);
    repaint();
}

juce::StringArray RackView::labels() const
{
    juce::StringArray all;
    for (auto& r : racks)
        for (auto& c : r.chains)
            all.add (c.label());
    return all;
}

void RackView::timerCallback()
{
    for (auto* u : units)
        u->update();
    const auto now = labels();
    if (now != shownLabels)
    {
        shownLabels = now;
        repaint();
    }
}

void RackView::paint (juce::Graphics& g)
{
    g.fillAll (room);
    for (auto& r : racks)
        paintRack (g, r);
}

void RackView::paintRack (juce::Graphics& g, const Rack& rack) const
{
    const auto b = rack.bounds.toFloat();

    // Cabinet: a dark case around the rails, with a title plate on top.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.expanded (8.0f).translated (0.0f, 4.0f), 7.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff26292b), 0, b.getY(), juce::Colour (0xff141617), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b.expanded (8.0f), 7.0f);
    g.setColour (edge.withAlpha (0.5f));
    g.drawRoundedRectangle (b.expanded (8.0f), 7.0f, 1.0f);

    const auto title = b.withHeight ((float) titleH).reduced (4.0f, 6.0f);
    g.setColour (plate);
    g.fillRoundedRectangle (title, 3.0f);
    g.setColour (legend);
    g.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
    g.drawText (rack.title, title, juce::Justification::centred, false);

    // Inside the rack: black behind the gear, and the two rails with their square holes.
    const auto inside = b.withTrimmedTop ((float) titleH).withTrimmedBottom (16.0f);
    g.setColour (juce::Colour (0xff070808));
    g.fillRect (inside);
    for (const auto side : { inside.withWidth ((float) rail), inside.withLeft (inside.getRight() - (float) rail) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e51), side.getX(), 0, juce::Colour (0xff2b2e30), side.getRight(), 0, false));
        g.fillRect (side);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        const float step = (float) RackUnit::height / 3.0f;
        for (float y = side.getY() + step * 0.5f; y < side.getBottom(); y += step)
            g.fillRect (side.getCentreX() - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }

    // Each chain starts with a strip of console tape carrying its name.
    for (auto& c : rack.chains)
    {
        const auto label = c.label();
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 20.0f;
        const auto tape = juce::Rectangle<float> ((float) c.header.getX() + (float) RackView::rail + 10.0f,
                                                  (float) c.header.getY() + 4.0f, w, (float) headerH - 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (tape.translated (1.0f, 1.5f));
        g.setColour (paper);
        g.fillRect (tape);
        g.setColour (juce::Colour (0xff1d1f22));
        g.drawText (label, tape, juce::Justification::centred, false);
    }
}

//==============================================================================
class RackWindow::Content : public juce::Component
{
public:
    Content (AudioEngine& e, PluginContext& ctx) : view (e, ctx)
    {
        viewport.setViewedComponent (&view, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        view.layoutFor (getWidth() - viewport.getScrollBarThickness());
    }

    juce::Viewport viewport;
    RackView view;
};

RackWindow::RackWindow (AudioEngine& e, PluginContext& ctx, juce::PropertiesFile& s)
    : juce::DocumentWindow ("Rack", room, juce::DocumentWindow::allButtons), context (ctx), settings (s)
{
    setUsingNativeTitleBar (true);
    auto* content = new Content (e, ctx);
    setContentOwned (content, false);
    setResizable (true, false);
    setResizeLimits (RackView::rackWidth + 2 * RackView::margin + 30, 300, 10000, 10000);

    const auto state = settings.getValue ("rackWindow");
    if (state.isEmpty() || ! restoreWindowStateFromString (state))
    {
        // First time: all racks side by side if the screen has room, as tall as the tallest rack.
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        content->view.layoutFor (content->view.widthForOneRow());
        const int w = juce::jmin (content->view.widthForOneRow() + content->viewport.getScrollBarThickness(), area.getWidth() * 95 / 100);
        const int h = juce::jmin (content->view.getHeight(), area.getHeight() * 85 / 100);
        centreWithSize (w, h);
    }
    context.windows.keepOnTop (*this);
}

RackWindow::~RackWindow()
{
    context.windows.stopKeepingOnTop (*this);
}

void RackWindow::show()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
}

void RackWindow::closeButtonPressed()
{
    saveState();
    setVisible (false);
}

void RackWindow::saveState()
{
    settings.setValue ("rackWindow", getWindowStateAsString());
    if (context.changed)
        context.changed();
}
