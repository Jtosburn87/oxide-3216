#include "Cassette.h"
#include "ConsoleLookAndFeel.h"

namespace
{
    using juce::Colour;
    constexpr float pi = juce::MathConstants<float>::pi;

    // Window and hub positions (cassette coordinates).
    const juce::Rectangle<float> window { 58.0f, 102.0f, 156.0f, 42.0f };
    const juce::Point<float> leftHub { 102.0f, 123.0f }, rightHub { 170.0f, 123.0f };

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

    void radialSpot (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> centre, float radius, Colour c)
    {
        juce::ColourGradient grad (c, centre, c.withAlpha (0.0f), centre.translated (radius, 0.0f), true);
        g.setGradientFill (grad);
        g.fillRect (area);
    }
}

Cassette::Cassette()
{
    name = "Untitled song";

    label.setMultiLine (false);
    label.setReturnKeyStartsNewLine (false);
    label.setInputRestrictions (30);
    label.setJustification (juce::Justification::centredLeft);
    label.setFont (juce::Font (juce::FontOptions (handwritingFont(), 25.0f, juce::Font::bold)));
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
    // Written slightly askew across the label.
    label.setBounds (22, 38, 228, 34);
    label.setTransform (juce::AffineTransform::rotation (-1.4f * pi / 180.0f, 22.0f, 55.0f));
}

void Cassette::update (double seconds, bool running, float secondsPerTurn, float progress, const juce::String& speedText)
{
    if (! label.hasKeyboardFocus (true) && label.getText() != name.toString())
        label.setText (name.toString(), false);

    bool changed = false;
    if (running && secondsPerTurn != 0.0f)   // negative: the reels turn the other way (rewinding)
    {
        hubAngle = std::fmod (hubAngle - (float) seconds / secondsPerTurn * 2.0f * pi, 2.0f * pi);   // turns anticlockwise
        changed = true;
    }
    progress = juce::jlimit (0.0f, 1.0f, progress);
    if (std::abs (progress - packProgress) > 0.001f) { packProgress = progress; changed = true; }
    if (speedText != speed) { speed = speedText; changed = true; }

    if (changed)
        repaint();
}

void Cassette::drawHub (juce::Graphics& g, juce::Point<float> c) const
{
    const float r = 13.0f;
    // Spokes: alternating light and dark wedges, turned by the hub angle.
    for (int i = 0; i < 12; ++i)
    {
        juce::Path wedge;
        const float a0 = hubAngle + (float) i * pi / 6.0f;
        wedge.addPieSegment (c.x - r, c.y - r, 2 * r, 2 * r, a0, a0 + pi / 6.0f, 0.27f);
        g.setColour (i % 2 == 0 ? Colour (0xffd9d4c0) : Colour (0xff7a7565));
        g.fillPath (wedge);
    }
    g.setColour (Colour (0xff0a0a0a));
    g.fillEllipse (c.x - r * 0.25f, c.y - r * 0.25f, r * 0.5f, r * 0.5f);
    g.setColour (Colour (0xffe6e2d2));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 2.0f);
}

void Cassette::paint (juce::Graphics& g)
{
    const auto body = juce::Rectangle<float> (1.0f, 1.0f, width - 2.0f, height - 2.0f);

    // Drop shadow and worn plastic shell.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (body.translated (0.0f, 3.0f), 9.0f);

    juce::Path shell;
    shell.addRoundedRectangle (body, 9.0f);
    g.saveState();
    g.reduceClipRegion (shell);
    g.setGradientFill (juce::ColourGradient (Colour (0xffbab29a), 0.0f, 0.0f, Colour (0xffa49c85), 0.0f, (float) height, false));
    g.fillRect (body);
    radialSpot (g, body, { 49.0f, 21.0f }, 150.0f, juce::Colours::white.withAlpha (0.14f));
    radialSpot (g, body, { 239.0f, 158.0f }, 160.0f, Colour (0xff462d14).withAlpha (0.4f));
    g.setGradientFill (juce::ColourGradient (Colour (0x00462d14), 0.0f, height - 22.0f, Colour (0x4d462d14), 0.0f, (float) height, false));
    g.fillRect (body);
    g.restoreState();
    g.setColour (Colour (0xff23211d));
    g.drawRoundedRectangle (body, 9.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (body.reduced (2.0f), 7.5f, 1.0f);

    // Stained paper label with ruled lines and a faded red stripe.
    const auto lab = juce::Rectangle<float> (14.0f, 14.0f, 244.0f, 80.0f);
    g.saveState();
    g.reduceClipRegion (lab.toNearestInt());
    g.setColour (Colour (0xffe6dcc0));
    g.fillRect (lab);
    radialSpot (g, lab, { lab.getX() + lab.getWidth() * 0.12f, lab.getY() + 64.0f }, 70.0f, Colour (0x476e5023));
    radialSpot (g, lab, { lab.getX() + lab.getWidth() * 0.92f, lab.getY() + 8.0f }, 60.0f, Colour (0x385a3c19));
    g.setColour (Colour (0x42284682));
    for (float y : { 45.0f, 65.0f })
        g.fillRect (lab.getX(), lab.getY() + y, lab.getWidth(), 1.0f);
    g.setColour (Colour (0xffb04a3c).withAlpha (0.78f));
    g.fillRect (lab.withHeight (11.0f));
    g.setColour (Colour (0x403c2d14));
    g.fillRect (lab.getX() + lab.getWidth() * 0.38f, lab.getY(), 1.0f, lab.getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (lab.getX() + lab.getWidth() * 0.38f + 1.0f, lab.getY(), 1.0f, lab.getHeight());

    g.setColour (Colour (0xff5a4d36));
    g.setFont (ConsoleLookAndFeel::uiFont (10.5f, true).withExtraKerningFactor (0.06f));
    const auto meta = lab.withTrimmedLeft (8.0f).withTrimmedRight (8.0f).removeFromBottom (15.0f);
    g.drawText ("SIDE A", meta, juce::Justification::centredLeft, false);
    g.drawText (speed.toUpperCase(), meta, juce::Justification::centredRight, false);
    g.restoreState();
    g.setColour (Colour (0x593c2d14));
    g.drawRoundedRectangle (lab, 2.0f, 1.0f);

    // Window: dark tape packs that move across as the tape plays, and the two hubs.
    g.saveState();
    juce::Path win;
    win.addRoundedRectangle (window, 4.0f);
    g.reduceClipRegion (win);
    g.setColour (Colour (0xff17120d));
    g.fillRect (window);
    g.setColour (Colour (0xff3b2615));
    g.fillRect (window.getX(), window.getBottom() - 8.0f, window.getWidth(), 2.0f);   // the tape itself

    const float leftD = 56.0f - 28.0f * packProgress, rightD = 30.0f + 26.0f * packProgress;
    for (auto [c, d] : { std::pair { leftHub, leftD }, std::pair { rightHub, rightD } })
    {
        juce::ColourGradient pack (Colour (0xff2b1a0c), c, Colour (0xff22150a), c.translated (d * 0.5f, 0.0f), true);
        pack.addColour (0.55, Colour (0xff2b1a0c));
        pack.addColour (0.58, Colour (0xff3a2410));
        g.setGradientFill (pack);
        g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
    }
    drawHub (g, leftHub);
    drawHub (g, rightHub);

    // Glare across the window.
    juce::ColourGradient glare (juce::Colours::transparentWhite, window.getX(), window.getY(),
                                juce::Colours::transparentWhite, window.getRight(), window.getBottom(), false);
    glare.addColour (0.44, juce::Colours::white.withAlpha (0.16f));
    g.setGradientFill (glare);
    g.fillRect (window);
    g.restoreState();
    g.setColour (Colour (0xff0a0a0a));
    g.drawRoundedRectangle (window, 4.0f, 2.0f);

    // Bottom ridge with the two capstan holes.
    juce::Path ridge;
    ridge.addQuadrilateral (65.5f, 150.0f, 206.5f, 150.0f, 218.0f, (float) height - 1.0f, 54.0f, (float) height - 1.0f);
    g.setColour (Colour (0xff1c1b18));
    g.fillPath (ridge);
    for (float x : { 80.0f, 184.0f })
    {
        g.setColour (Colour (0xff070707));
        g.fillEllipse (x, 158.0f, 8.0f, 8.0f);
        g.setColour (Colour (0xff3a3833));
        g.drawEllipse (x, 158.0f, 8.0f, 8.0f, 1.0f);
    }

    // Corner screws.
    for (auto p : { juce::Point<float> (9.5f, 9.5f), juce::Point<float> (262.5f, 9.5f),
                    juce::Point<float> (9.5f, 162.5f), juce::Point<float> (262.5f, 162.5f) })
    {
        juce::ColourGradient screw (Colour (0xff9b9b92), p, Colour (0xff4a4a44), p.translated (4.5f, 0.0f), true);
        g.setGradientFill (screw);
        g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
        g.setColour (Colour (0xff2a2a2a));
        g.drawLine (p.x - 2.5f, p.y - 2.5f, p.x + 2.5f, p.y + 2.5f, 1.2f);
    }
}

void Cassette::paintOverChildren (juce::Graphics& g)
{
    // Wear on top of everything (after the prototype's SVG): coffee ring, cracks, chipped corner,
    // a strip of old tape, scratches and smudges.
    g.setColour (Colour (0xff6b4a1f).withAlpha (0.28f));
    g.drawEllipse (196.0f - 17.0f, 48.0f - 17.0f, 34.0f, 34.0f, 3.0f);
    juce::Path arc;
    arc.startNewSubPath (178.0f, 40.0f);
    arc.quadraticTo (186.0f, 30.0f, 200.0f, 34.0f);
    g.setColour (Colour (0xff6b4a1f).withAlpha (0.18f));
    g.strokePath (arc, juce::PathStrokeType (2.0f));

    // Chipped corner: a piece of the shell is missing.
    juce::Path chip;
    chip.addTriangle (0.0f, 0.0f, 30.0f, 0.0f, 0.0f, 26.0f);
    g.setColour (Colour (0xff0b0c0d));
    g.fillPath (chip);
    juce::Path chipEdge;
    chipEdge.startNewSubPath (30.0f, 0.0f);
    chipEdge.lineTo (12.0f, 14.0f);
    chipEdge.lineTo (22.0f, 20.0f);
    chipEdge.lineTo (0.0f, 26.0f);
    g.setColour (Colour (0xff2b2924));
    g.strokePath (chipEdge, juce::PathStrokeType (1.2f));

    juce::Path crack;
    crack.startNewSubPath (272.0f, 120.0f);
    crack.lineTo (258.0f, 126.0f);
    crack.lineTo (264.0f, 134.0f);
    crack.lineTo (254.0f, 140.0f);
    crack.lineTo (259.0f, 147.0f);
    g.strokePath (crack, juce::PathStrokeType (1.2f));

    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-3.0f * pi / 180.0f, 204.0f, 96.0f));
    g.setColour (Colour (0xff8b6b3c).withAlpha (0.35f));
    g.fillRect (204.0f, 96.0f, 46.0f, 13.0f);
    g.restoreState();

    struct Line { float x1, y1, x2, y2; bool light; float alpha; };
    for (auto l : { Line { 20, 132, 120, 148, true, 0.35f }, Line { 40, 160, 180, 112, false, 0.3f },
                    Line { 96, 98, 250, 90, true, 0.22f },   Line { 8, 86, 70, 104, false, 0.28f },
                    Line { 150, 168, 262, 140, true, 0.3f }, Line { 60, 118, 88, 150, false, 0.22f },
                    Line { 214, 160, 246, 108, true, 0.25f }, Line { 120, 6, 170, 12, false, 0.25f } })
    {
        g.setColour ((l.light ? juce::Colours::white : juce::Colours::black).withAlpha (l.alpha));
        g.drawLine (l.x1, l.y1, l.x2, l.y2, 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillEllipse (38.0f - 14.0f, 150.0f - 5.0f, 28.0f, 10.0f);
    g.fillEllipse (236.0f - 18.0f, 156.0f - 6.0f, 36.0f, 12.0f);
    g.fillEllipse (250.0f - 12.0f, 22.0f - 7.0f, 24.0f, 14.0f);
}
