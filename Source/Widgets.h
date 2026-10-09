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
    void setLegend (juce::String text) { legend = std::move (text); updateLegend(); }
    /** The value a double-click returns the knob to (normally the control's default). */
    void setResetValue (float v) { slider.setDoubleClickReturnValue (true, v); }

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Default 34 px dial in a 48 x 50 box; small knobs use a 26 px dial in a 36 x 42 box. */
    void setDialSize (int px) { dialSize = px; resized(); }

    static constexpr int preferredWidth = 48, preferredHeight = 50;
    static constexpr int smallWidth = 36, smallHeight = 42;

private:
    ParamSlider slider;
    juce::String legend, shown, endLeft, endRight;
    std::function<juce::String (float)> formatter;
    int dialSize = 34;

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

    /** Briefly light the button in red (the gate pop indicator). */
    void flash();

private:
    bool flashing = false;
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void paramChanged (Param&) override;
    void clicked() override;
};

/** Long-throw fader with a printed dB scale to its left. */
class Fader : public juce::Component
{
public:
    /** labelWidth: room for the printed dB scale on the left. */
    explicit Fader (Param&, int labelWidth = 20);
    void paint (juce::Graphics&) override;
    void resized() override;

    ParamSlider slider;

private:
    const int labelWidth;
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

/** One button of a radio group bound to a stepped Param: lit when the param equals its index. */
class RadioParamButton : public juce::TextButton,
                         private Param::Listener
{
public:
    RadioParamButton (Param&, int index, const juce::String& text);
    ~RadioParamButton() override;

private:
    Param& param;
    const int index;
    void paramChanged (Param&) override;
    void clicked() override;
};

/** Horizontal gain-reduction bar, 0..20 dB (or fullScaleDb), amber to red. */
class GrMeter : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    explicit GrMeter (float fullScaleDb = 20.0f) : fullScale (fullScaleDb) {}

    void setReduction (float db);
    void paint (juce::Graphics&) override;

private:
    const float fullScale;
    float shown = 0.0f;
};

/** A transport key in the style of a 1980s cassette multitrack: a dark key with a light top edge and a
    white symbol, its name printed above, and a status LED beside the name. The power key is a red square
    with its name to the left, lit while on. The toggle state lights the LED (or the power key). */
class TransportKey : public juce::Button
{
public:
    enum Icon { rewind, fastForward, stop, play, record, erase, power };

    /** led: the status LED's colour; transparent = no LED. */
    TransportKey (const juce::String& label, Icon, juce::Colour led = juce::Colour (0xff47e06a));

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    static constexpr int labelHeight = 12;

private:
    const Icon icon;
    const juce::Colour ledColour;

    void paintIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;
};
