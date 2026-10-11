#include "Widgets.h"
#include "ConsoleLookAndFeel.h"
#include "Dsp.h"

using namespace ConsoleColours;

//==============================================================================
ParamSlider::ParamSlider (Param& p) : param (p)
{
    setRange (false);
    setValue (p.get(), juce::dontSendNotification);
    setDoubleClickReturnValue (true, p.def, juce::ModifierKeys::ctrlModifier);   // double-click or Ctrl+click resets
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setTitle (p.name);
    setWantsKeyboardFocus (true);
    param.addListener (this);
}

ParamSlider::~ParamSlider()
{
    param.removeListener (this);
}

void ParamSlider::setRange (bool reversed)
{
    auto* pp = &param;
    const double flip = reversed ? 1.0 : 0.0;   // reversed: position n shows the value at 1 - n
    setNormalisableRange ({ (double) param.min, (double) param.max,
                            [pp, flip] (double, double, double n) { return (double) pp->fromNormalised ((float) std::abs (flip - n)); },
                            [pp, flip] (double, double, double v) { return std::abs (flip - (double) pp->toNormalised ((float) v)); } });
    if (param.scale == Param::Scale::stepped)
        setNormalisableRange ({ (double) param.min, (double) param.max, 1.0 });   // snap to switch positions
}

void ParamSlider::setReversed (bool reversed)
{
    setRange (reversed);
    setValue (param.get(), juce::dontSendNotification);
    repaint();
}

void ParamSlider::paramChanged (Param&)
{
    setValue (param.get(), juce::dontSendNotification);
}

void ParamSlider::valueChanged()
{
    param.set ((float) getValue());
}

void ParamSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (onDoubleClick != nullptr)
        onDoubleClick();
    else
        juce::Slider::mouseDoubleClick (e);
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
    slider.setBounds (getLocalBounds().removeFromTop (getHeight() - 13).withSizeKeepingCentre (dialSize, dialSize));
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
        // Kept inside the knob (words like "Slow" would be cut off at its edge), squeezed a little if need be.
        const auto box = [this, &dial] (int centreX)
        {
            return juce::Rectangle<int> (juce::jlimit (0, getWidth() - 18, centreX - 9), dial.getBottom() - 9, 18, 9);
        };
        g.drawFittedText (endLeft, box (dial.getX()), juce::Justification::centred, 1, 0.75f);
        g.drawFittedText (endRight, box (dial.getRight()), juce::Justification::centred, 1, 0.75f);
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

void ParamButton::flash()
{
    flashing = true;
    repaint();
    juce::Timer::callAfterDelay (110, [safe = juce::Component::SafePointer<ParamButton> (this)]
    {
        if (safe != nullptr)
        {
            safe->flashing = false;
            safe->repaint();
        }
    });
}

void ParamButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (! flashing)
    {
        juce::TextButton::paintButton (g, over, down);
        return;
    }

    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xffff5a46).withAlpha (0.45f));
    g.fillRoundedRectangle (r.expanded (0.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff7a62), r.getX(), r.getY(), juce::Colour (0xffd6392d), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r.reduced (1.0f), 2.0f);
    g.setColour (juce::Colours::white);
    g.setFont (getLookAndFeel().getTextButtonFont (*this, getHeight()));
    g.drawFittedText (getButtonText(), getLocalBounds(), juce::Justification::centred, 1);
}

//==============================================================================
Fader::Fader (Param& p, int labels) : slider (p), labelWidth (labels)
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
    slider.setBounds (getLocalBounds().withTrimmedLeft (labelWidth));
}

void Fader::paint (juce::Graphics& g)
{
    g.setFont (ConsoleLookAndFeel::uiFont (labelWidth < 20 ? 8.5f : 9.5f));
    g.setColour (ConsoleColours::legend.withAlpha (0.8f));

    for (int db : { 15, 10, 5, 0, -5, -10, -20, -30, -40, -60 })
    {
        const double pos = db <= -60 ? 0.0 : 80.0 + db / 0.75;
        const float y = (float) slider.getY() + (float) slider.getPositionOfValue (pos);
        const auto text = db == -60 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : (db > 0 ? "+" : "") + juce::String (db);
        g.drawText (text, juce::Rectangle<float> (0.0f, y - 5.0f, (float) labelWidth - 2.0f, 10.0f), juce::Justification::centredRight, false);
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

//==============================================================================
RadioParamButton::RadioParamButton (Param& p, int i, const juce::String& text)
    : juce::TextButton (text), param (p), index (i)
{
    setToggleState (juce::roundToInt (p.get()) == index, juce::dontSendNotification);
    p.addListener (this);
}

RadioParamButton::~RadioParamButton()
{
    param.removeListener (this);
}

void RadioParamButton::paramChanged (Param&)
{
    setToggleState (juce::roundToInt (param.get()) == index, juce::dontSendNotification);
}

void RadioParamButton::clicked()
{
    param.set ((float) index);
}

//==============================================================================
void GrMeter::setReduction (float db)
{
    const float before = shown;
    shown = db > shown ? db : juce::jmax (db, shown - fullScale / 20.0f);   // falls back the width of 1 dB in 20
    if (std::abs (shown - before) > 0.01f)
        repaint();
}

void GrMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (r);
    g.setColour (juce::Colours::black);
    g.drawRect (r, 1.0f);

    const float w = (r.getWidth() - 2.0f) * juce::jlimit (0.0f, 1.0f, shown / fullScale);
    if (w > 0.0f)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9a02d), r.getX(), 0.0f,
                                                 juce::Colour (0xffb8372c), r.getRight(), 0.0f, false));
        g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, w, r.getHeight() - 2.0f);
    }
}

//==============================================================================
TransportKey::TransportKey (const juce::String& label, Icon i, juce::Colour led)
    : juce::Button (label), icon (i), ledColour (led)
{
    setTitle (label);
}

void TransportKey::paintIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.42f;   // symbol size
    const auto centre = r.getCentre();
    const auto triangle = [&] (float x, float dir)   // pointing right (dir 1) or left (-1)
    {
        juce::Path p;
        p.addTriangle (x - dir * s * 0.45f, centre.y - s * 0.5f, x - dir * s * 0.45f, centre.y + s * 0.5f, x + dir * s * 0.45f, centre.y);
        g.fillPath (p);
    };

    g.setColour (c);
    switch (icon)
    {
        case rewind:      triangle (centre.x - s * 0.42f, -1.0f); triangle (centre.x + s * 0.42f, -1.0f); break;
        case fastForward: triangle (centre.x - s * 0.42f, 1.0f);  triangle (centre.x + s * 0.42f, 1.0f);  break;
        case play:        triangle (centre.x + s * 0.08f, 1.0f); break;
        case stop:        g.fillRect (juce::Rectangle<float> (s * 0.85f, s * 0.85f).withCentre (centre)); break;
        case record:
            g.setColour (juce::Colour (0xffe8402f));
            g.fillEllipse (juce::Rectangle<float> (s * 0.95f, s * 0.95f).withCentre (centre));
            break;
        case erase:
        {
            // A strip of tape with a slash through it.
            const auto strip = juce::Rectangle<float> (s * 1.7f, s * 0.7f).withCentre (centre);
            g.drawRect (strip, 1.3f);
            g.drawLine (strip.getX() + s * 0.25f, strip.getBottom() + s * 0.3f, strip.getRight() - s * 0.25f, strip.getY() - s * 0.3f, 1.6f);
            break;
        }
        case power: break;
    }
}

void TransportKey::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto silk = juce::Colour (0xffd9dcdd);
    auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();

    if (icon == power)
    {
        // Name to the left, a red square key to the right.
        const auto key = b.removeFromRight (juce::jmin (b.getWidth() * 0.5f, b.getHeight() * 1.5f)).reduced (1.0f).translated (0.0f, isButtonDown ? 1.0f : 0.0f);
        g.setColour (silk);
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
        g.drawText (getName().toUpperCase(), b.withTrimmedRight (6.0f), juce::Justification::centredRight, false);

        const auto red = on ? juce::Colour (0xffe8502f) : juce::Colour (0xff6e2318);
        if (on)
        {
            g.setColour (red.withAlpha (0.3f));
            g.fillRoundedRectangle (key.expanded (3.0f), 4.0f);
        }
        g.setColour (juce::Colour (0xff0b0c0d));
        g.fillRoundedRectangle (key.expanded (1.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (red.brighter (0.25f), 0, key.getY(), red.darker (0.25f), 0, key.getBottom(), false));
        g.fillRoundedRectangle (key, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.35f : 0.15f));
        g.fillRect (key.getX() + 2.0f, key.getY() + 1.0f, key.getWidth() - 4.0f, 1.0f);
        if (isMouseOver)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (key, 2.5f);
        }
        return;
    }

    // Name above the key, centred together with its LED (keys without a status have none).
    const auto labelArea = b.removeFromTop ((float) labelHeight);
    const auto name = getName().toUpperCase();
    const bool hasLed = ! ledColour.isTransparent();
    const float ledRoom = hasLed ? 8.0f : 0.0f;
    float fontSize = 9.0f, textW = 0.0f;
    for (;; fontSize -= 0.5f)   // long names (RECORD) get a touch smaller rather than pushing the LED off the key
    {
        g.setFont (ConsoleLookAndFeel::uiFont (fontSize, true));
        textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
        if (textW + ledRoom <= labelArea.getWidth() - 2.0f || fontSize <= 7.0f)
            break;
    }
    const float textX = labelArea.getCentreX() - (textW + ledRoom) * 0.5f;
    g.setColour (silk);
    g.drawText (name, juce::Rectangle<float> (textX, labelArea.getY(), textW + 1.0f, labelArea.getHeight()), juce::Justification::centredLeft, false);
    if (hasLed)
    {
        const juce::Point<float> led (textX + textW + 5.0f, labelArea.getCentreY());
        if (on)
        {
            g.setColour (ledColour.withAlpha (0.3f));
            g.fillEllipse (led.x - 4.5f, led.y - 4.5f, 9.0f, 9.0f);
        }
        g.setColour (on ? ledColour : ledColour.withMultipliedBrightness (0.22f));
        g.fillEllipse (led.x - 2.2f, led.y - 2.2f, 4.4f, 4.4f);
    }

    // The key: dark, bevelled, pressed in a pixel while held.
    auto key = b.withTrimmedTop (2.0f).reduced (1.0f);
    g.setColour (juce::Colour (0xff0b0c0d));
    g.fillRoundedRectangle (key.expanded (1.0f), 3.0f);
    if (isButtonDown)
        key.translate (0.0f, 1.0f);
    const auto face = isButtonDown ? juce::Colour (0xff2c3033) : juce::Colour (0xff3a3e41);
    g.setGradientFill (juce::ColourGradient (face.brighter (0.12f), 0, key.getY(), face.darker (0.35f), 0, key.getBottom(), false));
    g.fillRoundedRectangle (key, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (isButtonDown ? 0.08f : 0.2f));
    g.fillRect (key.getX() + 2.0f, key.getY() + 1.0f, key.getWidth() - 4.0f, 1.0f);
    if (isMouseOver && ! isButtonDown)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (key, 2.5f);
    }
    paintIcon (g, key, juce::Colour (0xffe9ebec).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
}
