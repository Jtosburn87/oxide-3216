#include "Widgets.h"
#include "ConsoleLookAndFeel.h"
#include "Dsp.h"

using namespace ConsoleColours;

//==============================================================================
ParamSlider::ParamSlider (Param& p) : param (p)
{
    auto* pp = &param;
    setNormalisableRange ({ (double) p.min, (double) p.max,
                            [pp] (double, double, double n) { return (double) pp->fromNormalised ((float) n); },
                            [pp] (double, double, double v) { return (double) pp->toNormalised ((float) v); } });
    setValue (p.get(), juce::dontSendNotification);
    setDoubleClickReturnValue (true, p.def);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setTitle (p.name);
    setWantsKeyboardFocus (true);
    param.addListener (this);
}

ParamSlider::~ParamSlider()
{
    param.removeListener (this);
}

void ParamSlider::paramChanged (Param&)
{
    setValue (param.get(), juce::dontSendNotification);
}

void ParamSlider::valueChanged()
{
    param.set ((float) getValue());
}

void ParamSlider::stepBy (double delta)
{
    const double n = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + delta);
    setValue (proportionOfLengthToValue (n), juce::sendNotificationSync);
}

bool ParamSlider::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)  { stepBy (0.02); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) { stepBy (-0.02); return true; }
    if (code == juce::KeyPress::pageUpKey)   { stepBy (0.1); return true; }
    if (code == juce::KeyPress::pageDownKey) { stepBy (-0.1); return true; }
    if (code == juce::KeyPress::homeKey || code == juce::KeyPress::deleteKey)
    {
        setValue (param.def, juce::sendNotificationSync);
        return true;
    }
    return juce::Slider::keyPressed (k);
}

//==============================================================================
Knob::Knob (Param& p, juce::String text, juce::Colour cap)
    : slider (p), legend (std::move (text))
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (160);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("cap", (juce::int64) cap.getARGB());
    addAndMakeVisible (slider);
    p.addListener (this);
    updateLegend();
}

Knob::~Knob()
{
    slider.param.removeListener (this);
}

void Knob::updateLegend()
{
    shown = formatter ? formatter (slider.param.get()) : legend;
    slider.setTooltip (slider.param.name + ": " + (formatter ? shown : juce::String (slider.param.get(), 1)));
    repaint();
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().removeFromTop (getHeight() - 13).withSizeKeepingCentre (34, 34));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (legend.isEmpty() && shown.isEmpty() ? juce::Colours::transparentBlack : ConsoleColours::legend);
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
    g.drawText (shown, getLocalBounds().removeFromBottom (12), juce::Justification::centred, false);

    if (endLeft.isNotEmpty() || endRight.isNotEmpty())
    {
        const auto dial = slider.getBounds();
        g.setFont (ConsoleLookAndFeel::uiFont (8.0f));
        g.setColour (ConsoleColours::legend.withAlpha (0.85f));
        g.drawText (endLeft, juce::Rectangle<int> (dial.getX() - 9, dial.getBottom() - 9, 18, 9), juce::Justification::centred, false);
        g.drawText (endRight, juce::Rectangle<int> (dial.getRight() - 9, dial.getBottom() - 9, 18, 9), juce::Justification::centred, false);
    }
}

//==============================================================================
ParamButton::ParamButton (Param& p, const juce::String& text)
    : juce::TextButton (text), param (p)
{
    setToggleState (p.on(), juce::dontSendNotification);
    setTitle (p.name);
    p.addListener (this);
}

ParamButton::~ParamButton()
{
    param.removeListener (this);
}

void ParamButton::paramChanged (Param&)
{
    setToggleState (param.on(), juce::dontSendNotification);
}

void ParamButton::clicked()
{
    param.toggle();
}

//==============================================================================
Fader::Fader (Param& p) : slider (p)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setSliderSnapsToMousePosition (false);   // grab the cap; clicking the slot does not jump
    slider.textFromValueFunction = [] (double v)
    {
        return v <= 0.0 ? juce::String ("-inf dB") : juce::String (Dsp::faderDb ((float) v), 1) + " dB";
    };
    addAndMakeVisible (slider);
}

void Fader::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedLeft (20));
}

void Fader::paint (juce::Graphics& g)
{
    g.setFont (ConsoleLookAndFeel::uiFont (9.5f));
    g.setColour (ConsoleColours::legend.withAlpha (0.8f));

    for (int db : { 15, 10, 5, 0, -5, -10, -20, -30, -40, -60 })
    {
        const double pos = db <= -60 ? 0.0 : 80.0 + db / 0.75;
        const float y = (float) slider.getY() + (float) slider.getPositionOfValue (pos);
        const auto text = db == -60 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : (db > 0 ? "+" : "") + juce::String (db);
        g.drawText (text, juce::Rectangle<float> (0.0f, y - 5.0f, 18.0f, 10.0f), juce::Justification::centredRight, false);
    }
}

//==============================================================================
void LedMeter::setLevel (float linearPeak)
{
    const float db = linearPeak > 1.0e-5f ? 20.0f * std::log10 (linearPeak) : -100.0f;
    const float before = shownDb, holdBefore = holdDb;
    shownDb = db > shownDb ? db : juce::jmax (db, shownDb - 1.5f);   // falls about 45 dB/s at 30 fps

    if (db >= holdDb) { holdDb = db; holdFrames = 45; }
    else if (--holdFrames <= 0) holdDb = juce::jmax (db, holdDb - 1.5f);

    if (shownDb != before || holdDb != holdBefore)
        repaint();
}

void LedMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (r);

    // Green to -18, amber to -6, red above: the prototype's 62% / 84% split over -60..0 dB.
    constexpr int segments = 30;
    const float segH = r.getHeight() / (float) segments;
    const auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float lit = norm (shownDb) * segments;
    const int holdSeg = (int) (norm (holdDb) * segments) - 1;

    for (int s = 0; s < segments; ++s)
    {
        const float frac = (float) s / segments;
        auto colour = frac < 0.62f ? meterGreen : frac < 0.84f ? meterAmber : meterRed;
        const bool on = (float) s < lit || (s == holdSeg && holdDb > -60.0f);
        g.setColour (on ? colour.brighter (0.25f) : colour.withMultipliedBrightness (0.22f));
        g.fillRect (r.getX(), r.getBottom() - (float) (s + 1) * segH + 1.0f, r.getWidth(), juce::jmax (1.0f, segH - 1.0f));
    }
}
