#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

ConsoleLookAndFeel::ConsoleLookAndFeel()
{
    setColourScheme ({ room, plate, plate, edge, legend, amber, juce::Colours::black, amber, legend });

    setColour (juce::ResizableWindow::backgroundColourId, room);
    setColour (juce::DocumentWindow::textColourId, bridgeText);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e30));
    setColour (juce::TextButton::buttonOnColourId, amber);
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcfd3d4));
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1200));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1f2224));
    setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
    setColour (juce::ComboBox::textColourId, legend);
    setColour (juce::ComboBox::focusedOutlineColourId, focus);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1f2224));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a3f42));
    setColour (juce::Label::textColourId, legend);
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1f2224));
    setColour (juce::ListBox::outlineColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, legend);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff050607));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::black);
    setColour (juce::ToggleButton::textColourId, legend);
    setColour (juce::ToggleButton::tickColourId, amber);
}

juce::Font ConsoleLookAndFeel::uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions ("Bahnschrift", height, bold ? juce::Font::bold : juce::Font::plain));
}

void ConsoleLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    if (slider.getProperties().contains ("busKnob"))
    {
        drawBusKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), startAngle + pos * (endAngle - startAngle), slider.hasKeyboardFocus (false));
        return;
    }

    // Small console knob: printed ticks around a dark skirt, white pointer line, coloured centre cap.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto c = bounds.getCentre();
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float skirt = outer - 5.0f;

    // Printed tick marks.
    g.setColour (juce::Colour (0xffc3c8ca));
    constexpr int ticks = 11;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (ticks - 1);
        const auto p1 = c.getPointOnCircumference (outer - 0.5f, a), p2 = c.getPointOnCircumference (outer - 3.5f, a);
        g.drawLine ({ p1, p2 }, 1.2f);
    }

    // Shadow and skirt.
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (skirt * 2, skirt * 2).withCentre (c.translated (0.0f, 1.5f)));
    juce::ColourGradient grad (juce::Colour (0xff53585b), c.x - skirt * 0.2f, c.y - skirt * 0.45f,
                               juce::Colour (0xff1b1d1f), c.x + skirt, c.y + skirt, true);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (skirt * 2, skirt * 2).withCentre (c));
    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (skirt * 2, skirt * 2).withCentre (c), 1.0f);

    // Coloured cap.
    const auto capColour = juce::Colour ((juce::uint32) (juce::int64) slider.getProperties().getWithDefault ("cap", (juce::int64) 0xff7b8185));
    const float capR = skirt * 0.52f;
    g.setGradientFill (juce::ColourGradient (capColour.brighter (0.15f), c.x, c.y - capR,
                                             capColour.darker (0.25f), c.x, c.y + capR, false));
    g.fillEllipse (juce::Rectangle<float> (capR * 2, capR * 2).withCentre (c));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (juce::Rectangle<float> (capR * 2, capR * 2).withCentre (c), 0.8f);

    // Pointer on the skirt.
    const float angle = startAngle + pos * (endAngle - startAngle);
    g.setColour (juce::Colour (0xfff2f2ee));
    g.drawLine ({ c.getPointOnCircumference (skirt - 0.5f, angle), c.getPointOnCircumference (capR + 0.5f, angle) }, 2.0f);

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (focus);
        g.drawEllipse (bounds.reduced (0.5f), 1.6f);
    }
}

int ConsoleLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.getSliderStyle() == juce::Slider::LinearVertical ? 15 : LookAndFeel_V4::getSliderThumbRadius (slider);
}

juce::Font ConsoleLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin (15.0f, (float) buttonHeight * 0.6f));
}

void ConsoleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool isMouseOver, bool isButtonDown)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    if (button.getProperties().contains ("faceplateIn"))
    {
        // Light grey push button; lit blue underneath when in.
        g.setColour (on ? juce::Colour (0x8c3ea0ff) : juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (on ? 2.0f : 0.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeceef0), r.getX(), r.getY(), juce::Colour (0xffaeb2b8), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (on ? juce::Colour (0xff3ea0ff) : juce::Colour (0xff6f747a));
        g.fillRect (r.getX() + 1.0f, r.getBottom() - 4.0f, r.getWidth() - 2.0f, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (focus);
            g.drawRoundedRectangle (r.expanded (1.0f), 3.0f, 2.0f);
        }
        juce::ignoreUnused (isMouseOver, isButtonDown);
        return;
    }

    auto base = on ? button.findColour (juce::TextButton::buttonOnColourId)
                   : button.findColour (juce::TextButton::buttonColourId);
    if (isMouseOver && ! on)
        base = base.brighter (0.15f);
    if (isButtonDown)
        base = base.darker (0.2f);

    g.setGradientFill (juce::ColourGradient (base.brighter (0.25f), r.getX(), r.getY(),
                                             base.darker (0.25f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 2.0f);

    g.setColour (juce::Colours::white.withAlpha (on ? 0.3f : 0.12f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 2, r.getRight() - 2);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);

    if (on)
    {
        g.setColour (base.withAlpha (0.3f));
        g.drawRoundedRectangle (r.expanded (1.5f), 3.0f, 2.0f);
    }

    if (button.hasKeyboardFocus (true))
    {
        g.setColour (focus);
        g.drawRoundedRectangle (r.reduced (1.0f), 2.0f, 2.0f);
    }
}

void ConsoleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

void ConsoleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // Long-throw fader: printed scale ticks either side, black slot, cap with a white line.
    // x/y/width/height is the travel of the cap centre (JUCE has already inset it by the thumb radius).
    juce::ignoreUnused (minSliderPos, maxSliderPos);
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float cx = area.getCentreX(), top = area.getY(), bottom = area.getBottom();
    // Cap width can be set per fader ("capWidth"): wide caps on the bus and main faders.
    const float capWidthProp = (float) slider.getProperties().getWithDefault ("capWidth", 0.0);
    const float scaleW = capWidthProp > 0.0f ? juce::jmin (area.getWidth(), capWidthProp + 10.0f) : juce::jmin (area.getWidth(), 44.0f);

    g.setColour (juce::Colour (0xffd6d9da).withAlpha (0.7f));
    const int ticks = juce::jmax (2, juce::roundToInt ((bottom - top) / 14.0f));
    for (int i = 0; i <= ticks; ++i)
    {
        const float ty = std::round (bottom - (bottom - top) * (float) i / (float) ticks);
        g.fillRect (cx - scaleW * 0.5f, ty, 6.0f, 1.0f);
        g.fillRect (cx + scaleW * 0.5f - 6.0f, ty, 6.0f, 1.0f);
    }

    g.setColour (juce::Colour (0xff050607));
    g.fillRoundedRectangle (cx - 2.0f, top - 6.0f, 4.0f, bottom - top + 12.0f, 2.0f);

    const float capW = scaleW - 10.0f, capH = 30.0f;
    auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, sliderPos });

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 2.0f);

    juce::ColourGradient grad (juce::Colour (0xff55595c), cap.getX(), cap.getY(),
                               juce::Colour (0xff17191a), cap.getX(), cap.getBottom(), false);
    grad.addColour (0.14, juce::Colour (0xff25282a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (juce::Colour (0xffe8e8e4));
    g.fillRect (cap.getX(), cap.getCentreY() - 1.5f, cap.getWidth(), 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (cap, 2.0f, 1.0f);

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (focus);
        g.drawRect (juce::Rectangle<float> (scaleW + 6.0f, bottom - top + 36.0f).withCentre (area.getCentre()), 2.0f);
    }
}

void ConsoleLookAndFeel::drawBusKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle, bool focused)
{
    // Large faceplate knob: knurled grey skirt, light grey body, blue cap, white pointer bar.
    const auto c = bounds.getCentre();
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c.translated (0.0f, 3.0f)));

    for (int i = 0; i < 120; ++i)   // knurling: 3-degree ridges
    {
        juce::Path ridge;
        const float a = (float) i * juce::MathConstants<float>::twoPi / 120.0f;
        ridge.addPieSegment (c.x - r, c.y - r, r * 2, r * 2, a, a + juce::MathConstants<float>::twoPi / 120.0f, 0.0f);
        g.setColour (i % 2 == 0 ? juce::Colour (0xffc3c7cb) : juce::Colour (0xff8f9499));
        g.fillPath (ridge);
    }
    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c), 1.0f);

    const float body = r - 3.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9dcdf), c.x - body * 0.2f, c.y - body * 0.4f,
                                             juce::Colour (0xff9ca1a6), c.x + body, c.y + body, true));
    g.fillEllipse (juce::Rectangle<float> (body * 2, body * 2).withCentre (c));

    const float cap = r * 0.72f;
    juce::ColourGradient blue (juce::Colour (0xff55b0f5), c.x - cap * 0.2f, c.y - cap * 0.45f,
                               juce::Colour (0xff165ea8), c.x + cap, c.y + cap, true);
    blue.addColour (0.62, juce::Colour (0xff1f78d0));
    g.setGradientFill (blue);
    g.fillEllipse (juce::Rectangle<float> (cap * 2, cap * 2).withCentre (c));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (juce::Rectangle<float> (cap * 2, cap * 2).withCentre (c), 0.8f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-2.0f, -(r - 2.0f), 4.0f, r * 0.95f, 1.0f);
    g.setColour (juce::Colour (0xfff6f8fa));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c));

    if (focused)
    {
        g.setColour (focus);
        g.drawEllipse (bounds.reduced (0.5f), 1.6f);
    }
}

juce::Font ConsoleLookAndFeel::getPopupMenuFont()
{
    auto f = LookAndFeel_V4::getPopupMenuFont();
    return f.withHeight (f.getHeight() * menuScale);
}

juce::PopupMenu::Options ConsoleLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label)
               .withStandardItemHeight (juce::roundToInt ((float) label.getHeight() * menuScale));
}
