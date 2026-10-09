#include "MeterBridge.h"
#include "ConsoleLookAndFeel.h"

namespace
{
    // The bar's marks, top to bottom: one LED each, lit when the level reaches the mark.
    constexpr float marks[] = { 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -5, -7, -10, -15, -20 };
    constexpr int numMarks = (int) std::size (marks);

    constexpr float margin = 20, headerH = 32, rowY0 = 52, rowH = 300, rowGap = 14, columnW = 64;
    constexpr float ledX = 33, ledW = 8, segTop = 40, segPitch = 13, segH = 10;   // within a column / row

    const juce::Colour panel { 0xff151617 };
    const juce::Colour silk { 0xffdfe1e1 };      // white panel printing
    const juce::Colour logoRed { 0xffe0563a };
    const juce::Colour ledRed { 0xffff3b2a }, ledAmber { 0xffffb22e }, ledGreen { 0xff42e35a };

    juce::String markText (float m) { return m > 0 ? "+" + juce::String ((int) m) : juce::String ((int) m); }
    juce::Colour segColour (float m) { return m > 0 ? ledRed : m == 0 ? ledAmber : ledGreen; }
}

MeterBridge::MeterBridge (TapeMachine& t) : tape (t)
{
    shownDb.fill (-100.0f);
    setSize (designW, designH);
    startTimerHz (30);
}

juce::Rectangle<float> MeterBridge::columnBounds (int track) const
{
    const int row = track / 8, col = track % 8;
    return { margin + 10.0f + (float) col * columnW, rowY0 + (float) row * (rowH + rowGap), columnW, rowH };
}

juce::Rectangle<float> MeterBridge::switchBounds (int track) const
{
    const auto c = columnBounds (track);
    return { c.getX() + ledX + ledW * 0.5f - 7.0f, c.getY() + 250.0f, 14.0f, 34.0f };
}

int MeterBridge::switchAt (juce::Point<float> local) const
{
    const auto p = local.transformedBy (toScreen().inverted());
    for (int t = 0; t < TapeMachine::numTracks; ++t)
        if (switchBounds (t).expanded (8.0f, 4.0f).contains (p))
            return t;
    return -1;
}

void MeterBridge::mouseDown (const juce::MouseEvent& e)
{
    const int t = switchAt (e.position);
    if (t >= 0)
    {
        auto& arm = *tape.arm[(size_t) t];
        arm.set (arm.on() ? 0.0f : 1.0f);
        timerCallback();
    }
}

void MeterBridge::mouseMove (const juce::MouseEvent& e)
{
    const int t = switchAt (e.position);
    setMouseCursor (t >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip (t >= 0 ? "Record function: arm track " + juce::String (t + 1) : juce::String());
}

void MeterBridge::timerCallback()
{
    if (! isShowing())
    {
        wasHidden = true;
        return;
    }
    if (std::exchange (wasHidden, false))
        for (int t = 0; t < TapeMachine::numTracks; ++t)
            tape.takeOutputPeak (t);   // forget what built up while the window was hidden

    ++frame;
    bool changed = false;
    const bool blinkOn = (frame / 8) % 2 == 0;   // about twice a second
    for (int t = 0; t < TapeMachine::numTracks; ++t)
    {
        // Peak bar: jumps up, falls about 27 dB a second.
        const float pk = tape.takeOutputPeak (t);
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) - referenceDbfs : -100.0f;
        auto& shown = shownDb[(size_t) t];
        const float before = shown;
        shown = db > shown ? db : juce::jmax (db, shown - 0.9f);

        // Record LED: blinks while the track is armed, steady while it records.
        const bool armed = tape.arm[(size_t) t]->on();
        const bool led = tape.isRecording (t) || (armed && blinkOn);

        const auto litCount = [] (float level) { int n = 0; for (float m : marks) n += level >= m ? 1 : 0; return n; };
        changed = changed || litCount (before) != litCount (shown) || armed != shownArmed[(size_t) t] || led != shownLed[(size_t) t];
        shownArmed[(size_t) t] = armed;
        shownLed[(size_t) t] = led;
    }
    if (changed)
        repaint();
}

void MeterBridge::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.addTransform (toScreen());
    const juce::Rectangle<float> all (0, 0, (float) designW, (float) designH);

    // Panel: faint top light and a fine horizontal grain.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222426), 0, 0, panel, 0, 120, false));
    g.fillRect (all.withHeight (120));
    for (float y = 0; y < (float) designH; y += 3.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (((int) y / 3) % 2 == 0 ? 0.012f : 0.0f));
        g.fillRect (0.0f, y, (float) designW, 1.0f);
    }

    // Header: the machine's name in red, what this is in white, the meter reference on the right.
    auto header = all.reduced (margin, 0).withY (12).withHeight (headerH - 8);
    g.setColour (logoRed);
    g.setFont (ConsoleLookAndFeel::uiFont (22.0f, true));
    g.drawText ("TAPE 16", header, juce::Justification::centredLeft, false);
    g.setColour (silk);
    g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
    g.drawText ("TAPE OUT", header.withTrimmedLeft (98), juce::Justification::centredLeft, false);
    g.setColour (silk.withAlpha (0.6f));
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
    g.drawText (juce::String::fromUTF8 ("0 = \xe2\x88\x92") + juce::String ((int) -referenceDbfs) + " dBFS", header, juce::Justification::centredRight, false);

    for (int row = 0; row < 2; ++row)
    {
        // White frame around each row, with "RECORD FUNCTION" printed into its bottom edge.
        const auto frameBox = juce::Rectangle<float> (margin, rowY0 + (float) row * (rowH + rowGap), (float) designW - 2 * margin, rowH).reduced (0, 2);
        g.setColour (silk.withAlpha (0.75f));
        g.drawRoundedRectangle (frameBox, 3.0f, 1.0f);
        const juce::String legend = row == 0 ? "RECORD FUNCTION  1 - 8" : "RECORD FUNCTION  9 - 16";
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), legend) + 14.0f;
        const auto chip = juce::Rectangle<float> (frameBox.getCentreX() - w / 2, frameBox.getBottom() - 6.0f, w, 12.0f);
        g.setColour (panel);
        g.fillRect (chip);
        g.setColour (silk);
        g.drawText (legend, chip, juce::Justification::centred, false);
    }

    for (int t = 0; t < TapeMachine::numTracks; ++t)
        paintColumn (g, t);
}

void MeterBridge::paintColumn (juce::Graphics& g, int t) const
{
    const auto c = columnBounds (t);
    const float lx = c.getX() + ledX, cx = lx + ledW * 0.5f;

    // Number tab.
    const auto tab = juce::Rectangle<float> (cx - 9.0f, c.getY() + 12.0f, 18.0f, 17.0f);
    g.setColour (juce::Colour (0xffcfd1d0));
    g.fillRoundedRectangle (tab, 1.5f);
    g.setColour (juce::Colour (0xff121314));
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
    g.drawText (juce::String (t + 1), tab, juce::Justification::centred, false);

    // Scale and LED bar.
    const float level = shownDb[(size_t) t];
    g.setFont (ConsoleLookAndFeel::uiFont (8.5f, true));
    for (int i = 0; i < numMarks; ++i)
    {
        const float m = marks[i];
        const float y = c.getY() + segTop + (float) i * segPitch;
        g.setColour (silk.withAlpha (0.85f));
        g.drawText (markText (m), juce::Rectangle<float> (c.getX() + 2.0f, y, 21.0f, segH), juce::Justification::centredRight, false);
        g.fillRect (c.getX() + 25.0f, y + segH * 0.5f - 0.5f, 5.0f, 1.0f);

        const auto seg = juce::Rectangle<float> (lx, y, ledW, segH);
        const auto colour = segColour (m);
        g.setColour (juce::Colours::black);
        g.fillRect (seg.expanded (1.0f));
        if (level >= m)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillRect (seg.expanded (2.5f, 1.5f));
            g.setColour (colour);
            g.fillRect (seg);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (seg.withWidth (2.0f).translated (1.0f, 0.0f));
        }
        else
        {
            g.setColour (colour.withMultipliedBrightness (0.18f).withMultipliedSaturation (0.7f));
            g.fillRect (seg);
        }
    }

    // Record LED.
    const juce::Point<float> led (cx, c.getY() + 238.0f);
    g.setColour (juce::Colours::black);
    g.fillEllipse (led.x - 4.5f, led.y - 4.5f, 9.0f, 9.0f);
    if (shownLed[(size_t) t])
    {
        g.setColour (ledRed.withAlpha (0.3f));
        g.fillEllipse (led.x - 7.0f, led.y - 7.0f, 14.0f, 14.0f);
    }
    g.setColour (shownLed[(size_t) t] ? ledRed : juce::Colour (0xff3a1512));
    g.fillEllipse (led.x - 3.2f, led.y - 3.2f, 6.4f, 6.4f);

    // Record function switch: a slot with a grey cap, up = armed.
    const auto slot = switchBounds (t);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (slot, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (slot.expanded (0.5f), 2.0f, 1.0f);
    const bool armed = shownArmed[(size_t) t];
    const auto cap = juce::Rectangle<float> (slot.getX() + 1.5f, armed ? slot.getY() + 1.5f : slot.getBottom() - 17.5f, slot.getWidth() - 3.0f, 16.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a9d9f), 0, cap.getY(), juce::Colour (0xff55585a), 0, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    for (int i = 0; i < 3; ++i)
        g.fillRect (cap.getX() + 2.0f, cap.getY() + 4.0f + (float) i * 3.5f, cap.getWidth() - 4.0f, 1.0f);
}

//==============================================================================
MeterBridgeWindow::MeterBridgeWindow (TapeMachine& tape, juce::PropertiesFile& s, std::function<void()> save)
    : juce::DocumentWindow ("Meter bridge", juce::Colour (0xff151617), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      settings (s), saveSettings (std::move (save))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MeterBridge (tape), true);
    constrainer.setFixedAspectRatio ((double) MeterBridge::designW / MeterBridge::designH);
    constrainer.setSizeLimits (MeterBridge::designW / 2, MeterBridge::designH / 2, MeterBridge::designW * 3, MeterBridge::designH * 3);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto state = settings.getValue ("meterBridgeWindow");
    if (state.isEmpty() || ! restoreWindowStateFromString (state))
        centreWithSize (getWidth(), getHeight());
}

void MeterBridgeWindow::show()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
}

void MeterBridgeWindow::closeButtonPressed()
{
    settings.setValue ("meterBridgeWindow", getWindowStateAsString());
    if (saveSettings)
        saveSettings();
    setVisible (false);
}
