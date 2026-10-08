#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Params.h"

/** Slider bound to a Param, with keyboard control:
    arrows step, Page Up/Down step x5, Home/Delete reset to default. */
class ParamSlider : public juce::Slider,
                    private Param::Listener
{
public:
    explicit ParamSlider (Param&);
    ~ParamSlider() override;

    Param& param;

    bool keyPressed (const juce::KeyPress&) override;

private:
    void paramChanged (Param&) override;
    void valueChanged() override;
    void stepBy (double normalisedDelta);
};

/** Small console knob with a legend below. Optional: cap colour, range labels at the ends of
    the sweep, and a formatter that shows the live value in place of the legend. */
class Knob : public juce::Component,
             private Param::Listener
{
public:
    Knob (Param&, juce::String legend, juce::Colour cap = juce::Colour (0xff7b8185));
    ~Knob() override;

    void setFormatter (std::function<juce::String (float)> f) { formatter = std::move (f); updateLegend(); }
    void setEndLabels (juce::String left, juce::String right) { endLeft = std::move (left); endRight = std::move (right); repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 48, preferredHeight = 50;

private:
    ParamSlider slider;
    juce::String legend, shown, endLeft, endRight;
    std::function<juce::String (float)> formatter;

    void paramChanged (Param&) override { updateLegend(); }
    void updateLegend();
};

/** Latching console button bound to a toggle Param. */
class ParamButton : public juce::TextButton,
                    private Param::Listener
{
public:
    ParamButton (Param&, const juce::String& text);
    ~ParamButton() override;

    Param& param;

private:
    void paramChanged (Param&) override;
    void clicked() override;
};

/** Long-throw fader with a printed dB scale to its left. */
class Fader : public juce::Component
{
public:
    explicit Fader (Param&);
    void paint (juce::Graphics&) override;
    void resized() override;

    ParamSlider slider;
};

/** Vertical LED peak meter, -60 to 0 dBFS, with a short peak hold. */
class LedMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    float shownDb = -100.0f, holdDb = -100.0f;
    int holdFrames = 0;
};
