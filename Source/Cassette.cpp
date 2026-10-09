#include "Cassette.h"
#include "ConsoleLookAndFeel.h"

namespace
{
    using juce::Colour;
    using Pt = juce::Point<float>;
    constexpr float pi = juce::MathConstants<float>::pi;

    // Layout in cassette coordinates, roughly to the proportions of a real compact cassette.
    const juce::Rectangle<float> shellBox { 1.0f, 1.0f, Cassette::width - 2.0f, Cassette::height - 2.0f };
    const juce::Rectangle<float> labelBox { 18.0f, 8.0f, 236.0f, 116.0f };
    const juce::Rectangle<float> window   { 50.0f, 50.0f, 172.0f, 52.0f };
    const Pt hubs[2]   { { 84.0f, 76.0f }, { 188.0f, 76.0f } };
    const Pt guides[2] { { 26.0f, 155.0f }, { 246.0f, 155.0f } };   // guide rollers in the bottom corners
    constexpr float hubR = 17.0f, packMax = 50.0f, guideR = 5.0f;

    // Openings along the bottom edge, where the heads, pinch rollers and guides reach the tape.
    const juce::Rectangle<float> openings[] { { 60, 160, 12, 11 }, { 92, 160, 16, 11 }, { 122, 150, 28, 21 },
                                              { 164, 160, 16, 11 }, { 200, 160, 12, 11 } };

    juce::String handwritingFont()
    {
        static const juce::String name = []
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (auto* candidate : { "Ink Free", "Segoe Print", "Segoe Script", "Comic Sans MS" })
                if (names.contains (candidate))
                    return juce::String (candidate);
            return juce::String();
        }();
        return name;
    }

    /** A point at radius r and angle a (clockwise from 12 o'clock, as Path::addPieSegment). */
    Pt polar (Pt c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

    /** Where a line from p touches the circle (c, r), on its outer side (left or right). */
    Pt tangentPoint (Pt c, float r, Pt p, bool leftSide)
    {
        const auto d = p - c;
        const float dist = d.getDistanceFromOrigin();
        if (dist <= r)
            return c + d * (r / juce::jmax (dist, 0.001f));
        const float base = std::atan2 (d.y, d.x), off = std::acos (r / dist);
        const Pt a { c.x + r * std::cos (base + off), c.y + r * std::sin (base + off) };
        const Pt b { c.x + r * std::cos (base - off), c.y + r * std::sin (base - off) };
        return (leftSide ? a.x < b.x : a.x > b.x) ? a : b;
    }

    juce::Path roundedBox (juce::Rectangle<float> r, float corner)
    {
        juce::Path p;
        p.addRoundedRectangle (r, corner);
        return p;
    }
}

Cassette::Cassette()
{
    name = "Untitled song";

    label.setMultiLine (false);
    label.setReturnKeyStartsNewLine (false);
    label.setInputRestrictions (30);
    label.setJustification (juce::Justification::centredLeft);
    label.setFont (juce::Font (juce::FontOptions (handwritingFont(), 22.0f, juce::Font::bold)));
    label.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    label.setColour (juce::TextEditor::textColourId, Colour (0xff1d2f6b));
    label.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    label.setColour (juce::TextEditor::focusedOutlineColourId, ConsoleColours::focus);
    label.setColour (juce::TextEditor::highlightColourId, Colour (0x55e0a83a));
    label.setColour (juce::CaretComponent::caretColourId, Colour (0xff1d2f6b));
    label.setIndents (2, 0);
    label.setTitle ("Project name");
    label.setTooltip ("Click to rename the project");
    label.setText (name.toString(), false);
    label.onTextChange = [this] { name = label.getText(); };
    label.onReturnKey = [this] { unfocusAllComponents(); };
    label.onEscapeKey = [this] { label.setText (name.toString(), false); unfocusAllComponents(); };
    addAndMakeVisible (label);

    setSize (width, height);
}

void Cassette::resized()
{
    // Written slightly askew across the top of the label.
    label.setBounds (24, 18, 224, 30);
    label.setTransform (juce::AffineTransform::rotation (-1.2f * pi / 180.0f, 24.0f, 33.0f));
}

void Cassette::setAnimated (bool shouldAnimate)
{
    animated = shouldAnimate;
    blur = { 0.0f, 0.0f };
    repaint();
}

float Cassette::packRadius (int reel) const
{
    // The tape on a pack is proportional to its area (minus the hub's).
    const float share = reel == 0 ? 1.0f - packProgress : packProgress;
    return std::sqrt (hubR * hubR + (packMax * packMax - hubR * hubR) * share);
}

void Cassette::update (double seconds, float hubTurnsPerSecond, float progress, const juce::String& speedText)
{
    if (! label.hasKeyboardFocus (true) && label.getText() != name.toString())
        label.setText (name.toString(), false);

    bool changed = false;
    progress = juce::jlimit (0.0f, 1.0f, progress);
    if (std::abs (progress - packProgress) > (animated ? 0.001f : 0.01f)) { packProgress = progress; changed = true; }
    if (speedText != speed) { speed = speedText; changed = true; }

    std::array<float, 2> newBlur { 0.0f, 0.0f };
    if (animated && hubTurnsPerSecond != 0.0f)
    {
        // The tape moves at one speed; each reel turns at that speed over its own radius. Above about
        // 25 degrees a frame the reels would strobe, so they are held there and blurred instead.
        const float tapeSpeed = 2.0f * pi * hubR * hubTurnsPerSecond;   // px per second
        for (int i = 0; i < 2; ++i)
        {
            const float step = tapeSpeed / packRadius (i) * (float) seconds;
            newBlur[(size_t) i] = juce::jlimit (0.0f, 1.0f, (std::abs (step) - 0.2f) / 0.3f);
            reelAngle[(size_t) i] = std::fmod (reelAngle[(size_t) i] - juce::jlimit (-0.45f, 0.45f, step), 2.0f * pi);   // play: anticlockwise
        }
        changed = true;
    }
    if (newBlur != blur) { blur = newBlur; changed = true; }

    if (changed)
        repaint();
}

void Cassette::drawReel (juce::Graphics& g, int reel) const
{
    const auto c = hubs[reel];
    const float r = packRadius (reel), a = reelAngle[(size_t) reel], b = blur[(size_t) reel];

    // Tape pack: dark brown oxide, a lighter edge, faint winding rings and a sheen that turns with it.
    if (r > hubR + 0.5f)
    {
        juce::ColourGradient pack (Colour (0xff24170c), c, Colour (0xff4a3019), c.translated (r, 0.0f), true);
        pack.addColour (0.85, Colour (0xff3a2512));
        g.setGradientFill (pack);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (Colour (0xff8a6a48).withAlpha (0.08f));
        for (float f : { 0.33f, 0.52f, 0.71f, 0.87f })
        {
            const float rr = hubR + (r - hubR) * f;
            g.drawEllipse (c.x - rr, c.y - rr, 2 * rr, 2 * rr, 0.8f);
        }
        g.setColour (Colour (0xff9a7650).withAlpha (0.4f));
        g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, 2 * r - 1.0f, 2 * r - 1.0f, 1.0f);

        g.setColour (Colour (0xffd8b48a).withAlpha (0.07f * (1.0f - b)));
        for (int k = 0; k < 3; ++k)
        {
            juce::Path sheen;
            const float a0 = a + (float) k * 2.0f * pi / 3.0f;
            sheen.addPieSegment (c.x - r, c.y - r, 2 * r, 2 * r, a0, a0 + 0.35f, hubR / r);
            g.fillPath (sheen);
        }
    }

    // White hub: the drive hole with six teeth, and the clamp that holds the tape leader.
    g.setColour (Colour (0xffe9e5d9));
    g.fillEllipse (c.x - hubR, c.y - hubR, 2 * hubR, 2 * hubR);
    g.setColour (Colour (0xffb9b4a6));
    g.drawEllipse (c.x - hubR + 0.5f, c.y - hubR + 0.5f, 2 * hubR - 1.0f, 2 * hubR - 1.0f, 1.0f);
    g.drawEllipse (c.x - 12.5f, c.y - 12.5f, 25.0f, 25.0f, 0.8f);

    const float hole = 9.5f;
    g.setColour (Colour (0xff0b0b0b));
    g.fillEllipse (c.x - hole, c.y - hole, 2 * hole, 2 * hole);

    g.setColour (Colour (0xffe9e5d9).withAlpha (1.0f - 0.65f * b));
    for (int k = 0; k < 6; ++k)
    {
        const float ak = a + (float) k * pi / 3.0f;
        g.drawLine ({ polar (c, hole + 0.5f, ak), polar (c, 6.0f, ak) }, 2.6f);
    }
    if (b > 0.0f)   // spinning too fast to see the teeth: a grey blur ring instead
    {
        g.setColour (Colour (0xffe9e5d9).withAlpha (0.35f * b));
        g.drawEllipse (c.x - 7.8f, c.y - 7.8f, 15.6f, 15.6f, 3.4f);
    }

    g.setColour (Colour (0xff7d786b).withAlpha (1.0f - 0.7f * b));
    g.drawLine ({ polar (c, 13.0f, a + 0.5f), polar (c, hubR - 0.5f, a + 0.5f) }, 3.0f);
}

void Cassette::paint (juce::Graphics& g)
{
    const auto shell = roundedBox (shellBox, 8.0f);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (shellBox.translated (0.0f, 3.0f), 8.0f);

    g.saveState();
    g.reduceClipRegion (shell);

    // Inside the shell: the packs, the guide rollers and the tape path between them.
    g.setColour (Colour (0xff121314));
    g.fillRect (shellBox);
    drawReel (g, 0);
    drawReel (g, 1);

    const auto tapePath = [this]
    {
        const Pt outL { guides[0].x - guideR, guides[0].y }, outR { guides[1].x + guideR, guides[1].y };
        juce::Path p;
        p.startNewSubPath (tangentPoint (hubs[0], packRadius (0), outL, true));
        p.lineTo (outL);
        p.quadraticTo (outL.x, guides[0].y + guideR, guides[0].x, guides[0].y + guideR);
        p.lineTo (guides[1].x, guides[1].y + guideR);
        p.quadraticTo (outR.x, guides[1].y + guideR, outR.x, outR.y);
        p.lineTo (tangentPoint (hubs[1], packRadius (1), outR, false));
        return p;
    }();
    g.setColour (Colour (0xff7a4e26));
    g.strokePath (tapePath, juce::PathStrokeType (1.8f));
    for (auto p : guides)
    {
        g.setColour (Colour (0xffd5d1c6));
        g.fillEllipse (p.x - guideR + 1.0f, p.y - guideR + 1.0f, 2 * guideR - 2.0f, 2 * guideR - 2.0f);
        g.setColour (Colour (0xff6e6a60));
        g.fillEllipse (p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f);
    }

    // Smoked plastic over everything but the clear window.
    juce::Path smoke;
    smoke.addRoundedRectangle (shellBox, 8.0f);
    smoke.addRoundedRectangle (window, 6.0f);
    smoke.setUsingNonZeroWinding (false);
    g.setGradientFill (juce::ColourGradient (Colour (0xb8383b3f), 0.0f, 0.0f, Colour (0xc41d1f22), 0.0f, (float) height, false));
    g.fillPath (smoke);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.08f), 0.0f, 0.0f, juce::Colours::transparentWhite, 120.0f, 90.0f, false);
    g.setGradientFill (sheen);
    g.fillPath (smoke);

    // Head area along the bottom: a raised trapezoid with the openings, the felt pressure pad, capstan holes.
    juce::Path ridge;
    ridge.addQuadrilateral (60.0f, 138.0f, 212.0f, 138.0f, 222.0f, (float) height - 1.0f, 50.0f, (float) height - 1.0f);
    g.setColour (Colour (0xff1a1b1d));
    g.fillPath (ridge);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawLine (60.0f, 138.5f, 212.0f, 138.5f, 1.0f);
    for (auto o : openings)
    {
        g.setColour (Colour (0xff050505));
        g.fillRect (o);
        g.saveState();
        g.reduceClipRegion (o.toNearestInt());
        g.setColour (Colour (0xff6a4423));
        g.fillRect (o.getX(), guides[0].y + guideR - 0.8f, o.getWidth(), 1.8f);
        g.restoreState();
    }
    g.setColour (Colour (0xffb59a72));
    g.fillRect (130.0f, 152.0f, 12.0f, 6.0f);   // felt pad, behind the tape
    g.setColour (Colour (0xff8a8a86));
    g.fillRect (133.0f, 150.0f, 6.0f, 2.0f);    // its spring
    for (float x : { 82.0f, 190.0f })
    {
        g.setColour (Colour (0xff050505));
        g.fillEllipse (x - 3.5f, 148.5f, 7.0f, 7.0f);
        g.setColour (Colour (0xff3d3f42));
        g.drawEllipse (x - 3.5f, 148.5f, 7.0f, 7.0f, 1.0f);
    }
    g.restoreState();

    g.setColour (Colour (0xff0d0e0f));
    g.drawRoundedRectangle (shellBox, 8.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (shellBox.reduced (2.5f), 6.5f, 1.0f);

    // Paper label, with the window cut out of it.
    juce::Path paper;
    paper.addRoundedRectangle (labelBox, 3.0f);
    paper.addRoundedRectangle (window.expanded (4.0f), 8.0f);
    paper.setUsingNonZeroWinding (false);
    g.saveState();
    g.reduceClipRegion (paper);
    g.setColour (Colour (0xffece4cd));
    g.fillRect (labelBox);
    juce::ColourGradient age (Colour (0x00000000), labelBox.getCentreX(), labelBox.getCentreY(),
                              Colour (0x1e6e5023), labelBox.getX(), labelBox.getY(), true);
    g.setGradientFill (age);
    g.fillRect (labelBox);

    g.setColour (Colour (0xffc0392b));
    g.fillRect (labelBox.withHeight (8.0f));
    g.setColour (Colour (0xffe08a2e));
    g.fillRect (labelBox.getX(), labelBox.getY() + 8.0f, labelBox.getWidth(), 2.5f);
    g.setColour (Colour (0x40284682));
    g.fillRect (labelBox.getX() + 4.0f, 46.5f, labelBox.getWidth() - 8.0f, 0.8f);   // the line the name is written on

    const auto sideCol = juce::Rectangle<float> (labelBox.getX(), window.getY(), window.getX() - 4.0f - labelBox.getX(), window.getHeight());
    const auto speedCol = juce::Rectangle<float> (window.getRight() + 4.0f, window.getY(), labelBox.getRight() - window.getRight() - 4.0f, window.getHeight());
    g.setColour (Colour (0xff3b3328));
    g.setFont (ConsoleLookAndFeel::uiFont (30.0f, true));
    g.drawText ("A", sideCol, juce::Justification::centred, false);
    const auto ips = speed.upToFirstOccurrenceOf (" ", false, false);
    g.setFont (ConsoleLookAndFeel::uiFont (ips.length() > 2 ? 14.0f : 17.0f, true));
    g.drawText (ips, speedCol.withTrimmedBottom (20.0f), juce::Justification::centredBottom, false);
    g.setFont (ConsoleLookAndFeel::uiFont (9.0f, true));
    g.drawText ("IPS", speedCol.withTrimmedTop (speedCol.getHeight() - 18.0f), juce::Justification::centredTop, false);

    g.setColour (Colour (0xff6a5c45));
    g.setFont (ConsoleLookAndFeel::uiFont (8.5f, true).withExtraKerningFactor (0.08f));
    const auto meta = labelBox.withTrimmedTop (window.getBottom() + 6.0f - labelBox.getY()).reduced (8.0f, 2.0f);
    g.drawText ("NORMAL BIAS  TYPE I", meta, juce::Justification::centredLeft, false);
    g.drawText ("CONSOLE 32", meta, juce::Justification::centredRight, false);
    g.restoreState();
    g.setColour (Colour (0x50362a14));
    g.drawRoundedRectangle (labelBox, 3.0f, 1.0f);

    // Window: moulded frame and a glare across the clear plastic.
    g.saveState();
    g.reduceClipRegion (roundedBox (window, 6.0f));
    juce::ColourGradient glare (juce::Colours::transparentWhite, window.getX(), window.getY(),
                                juce::Colours::transparentWhite, window.getRight(), window.getBottom(), false);
    glare.addColour (0.38, juce::Colours::white.withAlpha (0.13f));
    glare.addColour (0.46, juce::Colours::transparentWhite);
    g.setGradientFill (glare);
    g.fillRect (window);
    g.restoreState();
    g.setColour (Colour (0xff101112));
    g.drawRoundedRectangle (window, 6.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (window.reduced (1.5f), 5.0f, 0.8f);

    // Screws: four corners and one below the label.
    for (auto p : { Pt (9.5f, 12.0f), Pt (262.5f, 12.0f), Pt (9.5f, 160.0f), Pt (262.5f, 160.0f), Pt (136.0f, 131.0f) })
    {
        juce::ColourGradient screw (Colour (0xffb8b8b0), p.translated (-1.5f, -1.5f), Colour (0xff4a4a44), p.translated (4.5f, 0.0f), true);
        g.setGradientFill (screw);
        g.fillEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
        g.setColour (Colour (0xff2a2a2a));
        g.drawLine (p.x - 2.2f, p.y, p.x + 2.2f, p.y, 1.1f);
        g.drawLine (p.x, p.y - 2.2f, p.x, p.y + 2.2f, 1.1f);
    }
}

void Cassette::paintOverChildren (juce::Graphics& g)
{
    // A little wear: scuffs on the plastic and a smudge on the label.
    struct Line { float x1, y1, x2, y2; float alpha; };
    for (auto l : { Line { 30, 140, 46, 132, 0.18f }, Line { 228, 150, 250, 138, 0.15f }, Line { 120, 4, 160, 6, 0.14f } })
    {
        g.setColour (juce::Colours::white.withAlpha (l.alpha));
        g.drawLine (l.x1, l.y1, l.x2, l.y2, 0.8f);
    }
    g.setColour (Colour (0xff6e5023).withAlpha (0.08f));
    g.fillEllipse (214.0f, 108.0f, 30.0f, 12.0f);
}
