#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Colours and drawing for the console. Values follow console-tape.html. */
namespace ConsoleColours
{
    const juce::Colour room      { 0xff161718 };
    const juce::Colour stripGrey { 0xff585e60 };
    const juce::Colour legend    { 0xffe4e7e8 };
    const juce::Colour faderArea { 0xff0b0c0d };
    const juce::Colour bridge    { 0xff0a0b0c };
    const juce::Colour bridgeText{ 0xffd6d9da };
    const juce::Colour plate     { 0xff222527 };
    const juce::Colour edge      { 0xff5d6467 };
    const juce::Colour amber     { 0xffe0a93a };
    const juce::Colour focus     { 0xffe0a83a };
    const juce::Colour paper     { 0xffe9e6da };
    const juce::Colour meterGreen{ 0xff2a8a3a };
    const juce::Colour meterAmber{ 0xffb89a2a };
    const juce::Colour meterRed  { 0xffb8372c };
}

class ConsoleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ConsoleLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOver, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isButtonDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    int getSliderThumbRadius (juce::Slider&) override;   // half the fader cap height, so the cap stays inside the travel
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    static juce::Font uiFont (float height, bool bold = false);
};
