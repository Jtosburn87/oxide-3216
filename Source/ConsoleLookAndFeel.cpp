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
    const float scaleW = juce::jmin (area.getWidth(), 44.0f);

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
