#include "ShuttlePanel.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    constexpr float ringTravel = juce::MathConstants<float>::pi / 3.0f;   // the ring turns 60 degrees each way
    const juce::Colour ledRed { 0xffff6a3d };
}

//==============================================================================
ShuttleWheel::ShuttleWheel (TapeMachine& t) : tape (t)
{
    setTitle ("Jog / shuttle wheel");
    setTooltip ("Outer ring: turn to wind either way (further = faster), let go to stop. Inner wheel: jog. "
                "Mouse wheel: jog. Double-click the ring: stop.");
    startTimerHz (30);
}

float ShuttleWheel::speedForStep (int step) noexcept
{
    static constexpr float speeds[steps + 1] = { 0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
    const int a = juce::jmin (std::abs (step), steps);
    return step < 0 ? -speeds[a] : speeds[a];
}

float ShuttleWheel::outerRadius() const noexcept
{
    return (float) juce::jmin (getWidth(), getHeight()) * 0.5f - 10.0f;
}

float ShuttleWheel::angleAt (juce::Point<float> p) const noexcept
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return std::atan2 (p.y - c.y, p.x - c.x);
}

void ShuttleWheel::sendShuttle()
{
    const int step = juce::roundToInt (ring * (float) steps);
    if (step != sentStep)
    {
        sentStep = step;
        tape.setShuttle (speedForStep (step));
    }
}

void ShuttleWheel::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = outerRadius(), rIn = innerRadius();

    // Speed marks round the ring, fixed on the panel: centre, then the steps each way.
    for (int i = -steps; i <= steps; ++i)
    {
        const float a = -juce::MathConstants<float>::halfPi + ringTravel * (float) i / (float) steps;
        const float r1 = R + 3.0f, r2 = R + (i == 0 ? 8.0f : 6.0f);
        g.setColour (legend.withAlpha (i == 0 ? 1.0f : 0.55f));
        g.drawLine (c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, c.x + std::cos (a) * r2, c.y + std::sin (a) * r2, 1.2f);
    }

    // The shuttle ring: dark rubber with grip ridges, an amber pointer and the wind arrows.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (ring * ringTravel, c.x, c.y));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f53), c.x - R * 0.3f, c.y - R * 0.4f,
                                                 juce::Colour (0xff16181a), c.x + R, c.y + R, true));
        g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        for (int i = 0; i < 36; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.drawLine (c.x + std::cos (a) * (R - 2.0f), c.y + std::sin (a) * (R - 2.0f),
                        c.x + std::cos (a) * (R - 10.0f), c.y + std::sin (a) * (R - 10.0f), 2.0f);
        }
        juce::Path pointer;
        pointer.addTriangle (c.x - 4.0f, c.y - R + 2.0f, c.x + 4.0f, c.y - R + 2.0f, c.x, c.y - R + 13.0f);
        g.setColour (amber);
        g.fillPath (pointer);
        g.setColour (juce::Colour (0xffaeb3b5));
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        const auto arrows = [&] (const char* utf8, float x)
        {
            g.drawText (juce::String (juce::CharPointer_UTF8 (utf8)), juce::Rectangle<float> (24.0f, 14.0f).withCentre ({ x, c.y }),
                        juce::Justification::centred, false);
        };
        arrows ("\xe2\x97\x80\xe2\x97\x80", c.x - (R + rIn) * 0.5f);
        arrows ("\xe2\x96\xb6\xe2\x96\xb6", c.x + (R + rIn) * 0.5f);
    }

    // The jog wheel: brushed metal in a dark well, with a finger dimple.
    g.setColour (faderArea);
    g.fillEllipse (juce::Rectangle<float> ((rIn + 3.0f) * 2.0f, (rIn + 3.0f) * 2.0f).withCentre (c));
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (jogAngle, c.x, c.y));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9dcde), c.x - rIn * 0.35f, c.y - rIn * 0.45f,
                                                 juce::Colour (0xff5f6569), c.x + rIn, c.y + rIn, true));
        g.fillEllipse (juce::Rectangle<float> (rIn * 2.0f, rIn * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (float r = rIn * 0.2f; r < rIn; r += 3.0f)
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);
        const juce::Point<float> dimple (c.x, c.y - rIn * 0.62f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (rIn * 0.28f, rIn * 0.28f).withCentre (dimple));
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (rIn * 0.2f, rIn * 0.2f).withCentre (dimple.translated (-rIn * 0.02f, -rIn * 0.02f)));
    }

    // Centre cap.
    g.setColour (juce::Colour (0xff2b2f31));
    g.fillEllipse (juce::Rectangle<float> (rIn * 0.44f, rIn * 0.44f).withCentre (c));
    g.setColour (legend);
    g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
    g.drawText ("JOG", juce::Rectangle<float> (40.0f, 12.0f).withCentre (c), juce::Justification::centred, false);
}

void ShuttleWheel::mouseDown (const juce::MouseEvent& e)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float d = e.position.getDistanceFrom (c);
    drag = d > outerRadius() + 4.0f ? Drag::none : d > innerRadius() + 2.0f ? Drag::ring : Drag::jog;
    if (drag == Drag::ring)
        heldByKeys = false;   // the hand takes over: it springs back when let go
    lastAngle = angleAt (e.position);
}

void ShuttleWheel::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none)
        return;
    const float a = angleAt (e.position);
    float da = a - lastAngle;
    if (da > juce::MathConstants<float>::pi)  da -= juce::MathConstants<float>::twoPi;
    if (da < -juce::MathConstants<float>::pi) da += juce::MathConstants<float>::twoPi;
    lastAngle = a;

    if (drag == Drag::ring)
    {
        ring = juce::jlimit (-1.0f, 1.0f, ring + da / ringTravel);
        sendShuttle();
    }
    else
    {
        jogAngle += da;
        tape.jog ((double) da / juce::MathConstants<double>::twoPi * tape.getSampleRate());   // one turn = 1 s of tape
    }
    repaint();
}

void ShuttleWheel::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;   // the ring springs back (timerCallback)
}

void ShuttleWheel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (getLocalBounds().toFloat().getCentre()) > innerRadius() + 2.0f)
    {
        ring = 0.0f;
        sendShuttle();
        repaint();
    }
}

void ShuttleWheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY == 0.0f)
        return;
    jogNotch ((w.deltaY < 0.0f) != w.isReversed ? 1 : -1);   // scroll down = forward
}

void ShuttleWheel::jogNotch (int direction)
{
    const float da = (float) direction * juce::MathConstants<float>::twoPi / 24.0f;
    jogAngle += da;
    tape.jog ((double) da / juce::MathConstants<double>::twoPi * tape.getSampleRate());
    repaint();
}

void ShuttleWheel::shuttleStep (int direction)
{
    if (drag == Drag::ring)
        return;   // the mouse has it
    const int step = juce::jlimit (-steps, steps, sentStep + direction);
    heldByKeys = step != 0;
    ring = (float) step / (float) steps;
    sendShuttle();
    repaint();
}

void ShuttleWheel::timerCallback()
{
    // Something else took the tape (Stop, Play, a locate, a jog): the ring goes back to the centre without
    // sending anything.
    if (sentStep != 0 && tape.getShuttle() != speedForStep (sentStep))
    {
        ring = 0.0f;
        sentStep = 0;
        heldByKeys = false;
        repaint();
        return;
    }

    // Let go of the ring and it springs back to the centre, slowing the tape through the steps as it goes.
    if (drag != Drag::ring && ! heldByKeys && ring != 0.0f)
    {
        ring *= 0.72f;
        if (std::abs (ring) < 0.02f)
            ring = 0.0f;
        sendShuttle();
        repaint();
    }
}

//==============================================================================
void ShuttlePanel::MemoryButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    juce::TextButton::paintButton (g, isMouseOver, isButtonDown);
    if (stored)
    {
        g.setColour (amber);
        g.fillRect (getLocalBounds().reduced (3, 0).removeFromBottom (3));
    }
}

ShuttlePanel::ShuttlePanel (TapeMachine& t) : tape (t), wheel (t)
{
    addAndMakeVisible (wheel);

    for (auto* b : { &jogMode, &scrubMode })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x5c7b);
        addAndMakeVisible (b);
    }
    jogMode.setToggleState (! tape.scrubOn.load(), juce::dontSendNotification);
    scrubMode.setToggleState (tape.scrubOn.load(), juce::dontSendNotification);
    jogMode.setTooltip ("The wheel and ring move the tape silently");
    scrubMode.setTooltip ("You hear the tape while the wheel or ring moves it, at that speed and direction");
    jogMode.onClick   = [this] { tape.scrubOn = false; };
    scrubMode.onClick = [this] { tape.scrubOn = true; };

    setIn.setTooltip ("Set the In point here");
    setOut.setTooltip ("Set the Out point here");
    goIn.setTooltip ("Wind the tape to the In point");
    goOut.setTooltip ("Wind the tape to the Out point");
    loop.setTooltip ("Loop between In and Out");
    punch.setTooltip ("Auto-punch: record armed tracks only between In and Out (you hear the old take outside)");
    setIn.onClick  = [this] { tape.setInPoint (tape.getPosition()); };
    setOut.onClick = [this] { tape.setOutPoint (tape.getPosition()); };
    goIn.onClick   = [this] { tape.windTo (tape.getInPoint()); };
    goOut.onClick  = [this] { tape.windTo (tape.getOutPoint()); };
    loop.onClick   = [this] { tape.loopOn = ! tape.loopOn.load(); };
    punch.onClick  = [this] { tape.punchOn = ! tape.punchOn.load(); };
    loop.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a8fd6));
    punch.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
    punch.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    for (auto* b : { &setIn, &setOut, &loop, &goIn, &goOut, &punch })
        addAndMakeVisible (b);

    store.setTooltip ("Press Store, then a number, to keep the tape position there");
    store.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
    store.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    store.onClick = [this]
    {
        storing = ! storing;
        info = storing ? "Now press 1-6" : "Store, then 1-6";
        timerCallback();
        repaint();
    };
    rtz.setTooltip ("Return to zero: the tape winds back to the start");
    rtz.onClick = [this] { tape.windTo (0); };
    addAndMakeVisible (store);
    addAndMakeVisible (rtz);
    for (int m = 0; m < TapeMachine::numMemories; ++m)
    {
        auto& b = memories[(size_t) m];
        b.setButtonText (juce::String (m + 1));
        b.setTitle ("Locate memory " + juce::String (m + 1));
        b.onClick = [this, m] { memoryClicked (m); };
        addAndMakeVisible (b);
    }

    setTitle ("Jog / shuttle");
    timerCallback();
    startTimerHz (15);
}

void ShuttlePanel::press (Key k)
{
    switch (k)
    {
        case Key::jogScrub: (tape.scrubOn.load() ? jogMode : scrubMode).triggerClick(); break;
        case Key::store:    store.triggerClick(); break;
        case Key::rtz:      rtz.triggerClick(); break;
        case Key::setIn:    setIn.triggerClick(); break;
        case Key::setOut:   setOut.triggerClick(); break;
        case Key::loop:     loop.triggerClick(); break;
        case Key::goIn:     goIn.triggerClick(); break;
        case Key::goOut:    goOut.triggerClick(); break;
        case Key::punch:    punch.triggerClick(); break;
        case Key::jogBack:        wheel.jogNotch (-1); break;
        case Key::jogForward:     wheel.jogNotch (1); break;
        case Key::shuttleBack:    wheel.shuttleStep (-1); break;
        case Key::shuttleForward: wheel.shuttleStep (1); break;
    }
}

void ShuttlePanel::pressMemory (int m)
{
    if (juce::isPositiveAndBelow (m, TapeMachine::numMemories))
        memories[(size_t) m].triggerClick();
}

juce::String ShuttlePanel::formatTime (juce::int64 s) const
{
    const auto sr = tape.getSampleRate();
    const auto tenths = (juce::int64) ((double) juce::jmax ((juce::int64) 0, s) / (sr > 0 ? sr : 48000.0) * 10.0);
    return juce::String::formatted ("%02d:%02d.%d", (int) (tenths / 600), (int) ((tenths / 10) % 60), (int) (tenths % 10));
}

void ShuttlePanel::memoryClicked (int m)
{
    if (storing)
    {
        tape.setMemory (m, tape.getPosition());
        storing = false;
        info = juce::String (m + 1) + " = " + formatTime (tape.getMemory (m));
    }
    else if (tape.getMemory (m) >= 0)
    {
        tape.windTo (tape.getMemory (m));
        info = "To " + juce::String (m + 1) + " (" + formatTime (tape.getMemory (m)) + ")";
    }
    else
    {
        info = juce::String (m + 1) + " is empty";
    }
    timerCallback();
    repaint();
}

void ShuttlePanel::timerCallback()
{
    const auto time = formatTime (tape.getPosition());
    const float speed = tape.getWindSpeed();
    juce::String speedText ("Stop");
    if (tape.isPlaying())
        speedText = "Play";
    else if (std::abs (speed) >= 0.05f)
        speedText = juce::String (juce::CharPointer_UTF8 (speed < 0.0f ? "\xe2\x97\x80\xe2\x97\x80 " : "\xe2\x96\xb6\xe2\x96\xb6 "))
                  + juce::String (std::abs (speed), std::abs (speed) < 9.95f ? 1 : 0) + "x";
    const auto inOut = "In " + formatTime (tape.getInPoint()) + "   Out " + formatTime (tape.getOutPoint());
    if (time != shownTime || speedText != shownSpeed || inOut != shownInOut)
    {
        shownTime = time;
        shownSpeed = speedText;
        shownInOut = inOut;
        repaint();
    }

    loop.setToggleState (tape.loopOn.load(), juce::dontSendNotification);
    punch.setToggleState (tape.punchOn.load(), juce::dontSendNotification);
    store.setToggleState (storing, juce::dontSendNotification);
    for (int m = 0; m < TapeMachine::numMemories; ++m)
    {
        auto& b = memories[(size_t) m];
        const auto p = tape.getMemory (m);
        if (b.stored != (p >= 0))
        {
            b.stored = p >= 0;
            b.repaint();
        }
        b.setTooltip (p >= 0 ? "Memory " + juce::String (m + 1) + ": " + formatTime (p) + ". Click to wind the tape there."
                             : "Memory " + juce::String (m + 1) + ": empty. Press Store, then this, to keep the tape position here.");
    }
}

void ShuttlePanel::resized()
{
    // Left column: readouts, Jog / Scrub, the In / Out keys. Centre: the wheel. Right: the locate memories.
    constexpr int lx = 12, lw = 176, rx = 430, rw = 84, gap = 4, bh = 24;
    clockWell = { lx, 15, lw, 52 };
    speedWell = { lx, 73, lw, 26 };

    const int half = (lw - gap) / 2;
    jogMode.setBounds (lx, 107, half, bh);
    scrubMode.setBounds (lx + half + gap, 107, lw - half - gap, bh);

    const int third = (lw - 2 * gap) / 3;
    const std::array<juce::TextButton*, 6> keys { &setIn, &setOut, &loop, &goIn, &goOut, &punch };
    for (int i = 0; i < 6; ++i)
        keys[(size_t) i]->setBounds (lx + (i % 3) * (third + gap), 153 + (i / 3) * (bh + gap), third, bh);

    wheel.setBounds (juce::Rectangle<int> (228, 228).withCentre ({ 309, 119 }));

    store.setBounds (rx, 29, rw, bh);
    const int mw = (rw - gap) / 2;
    for (int m = 0; m < TapeMachine::numMemories; ++m)
        memories[(size_t) m].setBounds (rx + (m % 2) * (mw + gap), 59 + (m / 2) * (bh + gap), mw, bh);
    rtz.setBounds (rx, 161, rw, bh);
}

void ShuttlePanel::paint (juce::Graphics& g)
{
    // Readout wells, in the style of the tape clock.
    for (auto well : { clockWell, speedWell })
    {
        g.setColour (juce::Colour (0xff050607));
        g.fillRect (well);
        g.setColour (juce::Colour (0xff1d1f20));
        g.drawRect (well, 1);
    }
    g.setColour (juce::Colour (0xffaeb3b5));
    g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
    g.drawText ("TAPE POSITION", clockWell.reduced (8, 3).removeFromTop (12), juce::Justification::centredLeft, false);
    g.drawText ("SPEED", speedWell.reduced (8, 0), juce::Justification::centredLeft, false);

    const auto lcd = [&g] (const juce::String& text, juce::Rectangle<int> area, float height, juce::Justification j)
    {
        g.setFont (ConsoleLookAndFeel::uiFont (height, true).withExtraKerningFactor (0.08f));
        g.setColour (ledRed.withAlpha (0.25f));
        g.drawText (text, area.toFloat().translated (0.0f, 0.5f).expanded (1.0f), j, false);
        g.setColour (ledRed);
        g.drawText (text, area, j, false);
    };
    lcd (shownTime, clockWell.withTrimmedTop (14), 30.0f, juce::Justification::centred);
    lcd (shownSpeed, speedWell.reduced (8, 0), 15.0f, juce::Justification::centredRight);

    g.setColour (juce::Colour (0xffaeb3b5));
    g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
    g.drawText ("IN / OUT", juce::Rectangle<int> (12, 139, 176, 12), juce::Justification::centredLeft, false);
    g.drawText ("LOCATE", juce::Rectangle<int> (430, 15, 84, 12), juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xff8e9598).brighter (0.3f));
    g.setFont (ConsoleLookAndFeel::uiFont (10.5f));
    g.drawText (shownInOut, juce::Rectangle<int> (12, 211, 176, 14), juce::Justification::centredLeft, false);
    g.drawFittedText (info, juce::Rectangle<int> (430, 192, 88, 30), juce::Justification::topLeft, 2, 0.8f);
}
