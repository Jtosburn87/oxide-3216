#include "BusCompPanel.h"
#include "ConsoleLookAndFeel.h"

namespace
{
    using juce::Colour;
    constexpr float pi = juce::MathConstants<float>::pi;
    const Colour ink { 0xfff1f3f4 };
    constexpr float knobRadius = 18.0f, dotRadius = 24.0f, legendRadius = 33.0f;

    float deg (float d) { return d * pi / 180.0f; }
}

//==============================================================================
void GrNeedleMeter::setReduction (float db)
{
    const float before = needle;
    needle += (juce::jlimit (0.0f, 20.0f, db) - needle) * 0.2f;   // simple needle ballistics at 30 fps
    if (std::abs (needle - before) > 0.005f)
        repaint();
}

void GrNeedleMeter::paint (juce::Graphics& g)
{
    // Designed at 230 x 140, scaled to fit.
    const float s = juce::jmin ((float) getWidth() / 230.0f, (float) getHeight() / 140.0f);
    g.addTransform (juce::AffineTransform::scale (s));
    const juce::Point<float> pivot (115.0f, 126.0f);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (1.0f, 3.0f, 230.0f, 140.0f, 3.0f);
    g.setColour (Colour (0xff0e0f10));
    g.fillRoundedRectangle (0.0f, 0.0f, 230.0f, 140.0f, 3.0f);
    const juce::Rectangle<float> face (8.0f, 6.0f, 214.0f, 92.0f);
    g.setColour (Colour (0xff040506));
    g.fillRoundedRectangle (face, 2.0f);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
    for (int db = 0; db <= 20; ++db)
    {
        const float a = deg (-50.0f + 5.0f * (float) db);
        const bool big = db % 4 == 0;
        g.setColour (ink);
        g.drawLine ({ pivot.getPointOnCircumference (big ? 80.0f : 85.0f, a), pivot.getPointOnCircumference (91.0f, a) }, big ? 1.6f : 0.8f);
        if (big)
        {
            const auto p = pivot.getPointOnCircumference (102.0f, a);
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (a, p.x, p.y));
            g.drawText (juce::String (db), juce::Rectangle<float> (24.0f, 14.0f).withCentre (p), juce::Justification::centred, false);
            g.restoreState();
        }
    }
    g.setFont (ConsoleLookAndFeel::uiFont (9.0f, true));
    g.drawText ("dB", juce::Rectangle<float> (95.0f, 70.0f, 40.0f, 10.0f), juce::Justification::centred, false);
    g.setFont (ConsoleLookAndFeel::uiFont (10.5f, true).withExtraKerningFactor (0.05f));
    g.drawText ("COMPRESSION", juce::Rectangle<float> (55.0f, 82.0f, 120.0f, 12.0f), juce::Justification::centred, false);

    // Needle.
    const float a = deg (-50.0f + 5.0f * needle);
    g.setColour (Colour (0xfff6f6f2));
    g.drawLine ({ pivot, pivot.getPointOnCircumference (96.0f, a) }, 1.8f);
    g.restoreState();

    // Pivot housing below the face.
    g.setColour (Colour (0xff050506));
    g.fillEllipse (pivot.x - 9.0f, 121.0f - 9.0f, 18.0f, 18.0f);
    g.setColour (Colour (0xff1d1e20));
    g.drawEllipse (pivot.x - 9.0f, 121.0f - 9.0f, 18.0f, 18.0f, 1.0f);
    g.setColour (Colour (0xff18191a));
    g.fillRect (113.5f, 113.0f, 3.0f, 16.0f);
}

//==============================================================================
BusKnob::BusKnob (Param& p, juce::String t, juce::String u, int numDots, std::vector<Legend> l)
    : slider (p), title (std::move (t)), unit (std::move (u)), legends (std::move (l)), dots (numDots)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (deg (-135.0f) + 2.0f * pi, deg (135.0f) + 2.0f * pi, true);
    slider.setMouseDragSensitivity (180);
    slider.getProperties().set ("busKnob", true);
    addAndMakeVisible (slider);
}

BusKnob::BusKnob (Param& p, juce::String t, juce::String u, const char* const* positionLabels)
    : slider (p), title (std::move (t)), unit (std::move (u)), stepped (true)
{
    for (int i = 0; i < 6; ++i)
        legends.push_back ({ (float) i, positionLabels[i] });

    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (deg (-100.0f) + 2.0f * pi, deg (100.0f) + 2.0f * pi, true);
    slider.setMouseDragSensitivity (5 * 22);
    slider.getProperties().set ("busKnob", true);
    addAndMakeVisible (slider);
}

juce::Point<float> BusKnob::dialCentre() const
{
    return { getWidth() * 0.5f, 14.0f + legendRadius + 4.0f };
}

float BusKnob::angleFor (float value) const
{
    const auto& p = slider.param;
    const float n = (value - p.min) / (p.max - p.min);
    return stepped ? deg (-100.0f + 200.0f * n) : deg (-135.0f + 270.0f * n);
}

void BusKnob::resized()
{
    const auto c = dialCentre();
    slider.setBounds (juce::Rectangle<int> ((int) (knobRadius * 2 + 6), (int) (knobRadius * 2 + 6)).withCentre (c.toInt()));
}

void BusKnob::mouseDown (const juce::MouseEvent& e)
{
    if (! stepped)
        return;
    const auto c = dialCentre();
    for (auto& l : legends)
        if (e.position.getDistanceFrom (c.getPointOnCircumference (legendRadius, angleFor (l.value))) < 11.0f)
            slider.param.set (l.value);
}

void BusKnob::paint (juce::Graphics& g)
{
    const auto c = dialCentre();
    g.setColour (ink);
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true).withExtraKerningFactor (0.03f));
    g.drawText (title, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 13.0f), juce::Justification::centred, false);

    // Printed dots.
    if (stepped)
    {
        for (auto& l : legends)
        {
            const auto p = c.getPointOnCircumference (dotRadius, angleFor (l.value));
            g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
        }
    }
    else
    {
        for (int i = 0; i < dots; ++i)
        {
            const auto p = c.getPointOnCircumference (dotRadius, deg (-135.0f + 270.0f * (float) i / (float) (dots - 1)));
            g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
        }
    }

    g.setFont (ConsoleLookAndFeel::uiFont (10.5f, true));
    for (auto& l : legends)
    {
        const auto p = c.getPointOnCircumference (legendRadius, angleFor (l.value));
        g.drawText (l.text, juce::Rectangle<float> (34.0f, 12.0f).withCentre (p), juce::Justification::centred, false);
    }

    g.setFont (ConsoleLookAndFeel::uiFont (10.5f, true));
    g.drawText (unit, juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ c.x, c.y + knobRadius + 9.0f }), juce::Justification::centred, false);
}

//==============================================================================
BusCompPanel::BusCompPanel (BusCompressor& c)
    : comp (c),
      threshold (*c.threshold, "THRESHOLD", "dB", 9, { { -20.0f, "-20" }, { 0.0f, "0" }, { 20.0f, "+20" } }),
      makeup (*c.makeup, "MAKE UP", "dB", 5, { { -5.0f, "-" }, { 0.0f, "0" }, { 15.0f, "+" } }),
      attack (*c.attack, "ATTACK", "ms", BusCompressor::attackLabels),
      release (*c.release, "RELEASE", "s", BusCompressor::releaseLabels),
      ratio (*c.ratio, "RATIO", "", BusCompressor::ratioLabels),
      hpf (*c.hpf, "HPF", "Hz", BusCompressor::hpfLabels),
      inButton (*c.in, "IN")
{
    addAndMakeVisible (meter);
    for (auto* k : { &threshold, &makeup, &attack, &release, &ratio, &hpf })
        addAndMakeVisible (k);
    inButton.getProperties().set ("faceplateIn", true);
    inButton.setColour (juce::TextButton::textColourOffId, Colour (0xff0b0b0b));
    inButton.setColour (juce::TextButton::textColourOnId, Colour (0xff0b0b0b));
    inButton.setTooltip ("Bus compressor in");
    addAndMakeVisible (inButton);
    setTitle ("Stereo bus compressor");
    setFocusContainerType (FocusContainerType::focusContainer);
}

void BusCompPanel::resized()
{
    border = { 16.0f, 26.0f, 250.0f, 420.0f };
    meter.setBounds (juce::Rectangle<int> (226, 138).withCentre ({ (int) border.getCentreX(), 38 + 69 }));

    const int top = 186, rowH = 86, colW = 116;
    const int x0 = (int) border.getCentreX() - colW;
    BusKnob* grid[3][2] = { { &threshold, &makeup }, { &attack, &release }, { &ratio, &hpf } };
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 2; ++col)
            grid[r][col]->setBounds (x0 + col * colW, top + r * rowH, colW, rowH);

    inButton.setBounds ((int) border.getRight() - 22, 202, 46, 26);
}

void BusCompPanel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // Brushed dark-grey plate.
    g.setGradientFill (juce::ColourGradient (Colour (0xff45484c), 0.0f, 0.0f, Colour (0xff393b3f), 0.0f, r.getHeight(), false));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.028f));
    for (float x = 0.0f; x < r.getWidth(); x += 3.0f)
        g.fillRect (x, 0.0f, 1.0f, r.getHeight());
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r.reduced (1.5f), 2.0f, 1.0f);

    // Mounting screws.
    for (auto p : { juce::Point<float> (16.0f, 12.0f), juce::Point<float> (r.getRight() - 16.0f, 12.0f),
                    juce::Point<float> (16.0f, r.getBottom() - 12.0f), juce::Point<float> (r.getRight() - 16.0f, r.getBottom() - 12.0f) })
    {
        juce::ColourGradient ring (Colour (0xff8d9296), p, Colour (0xff2a2c2e), p.translated (6.0f, 0.0f), true);
        g.setGradientFill (ring);
        g.fillEllipse (p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
        g.setColour (Colour (0xff050506));
        g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
    }

    // Thin pale-blue panel border, and the module name at the foot.
    g.setColour (Colour (0xff7ea4bd));
    g.drawRoundedRectangle (border, 9.0f, 1.0f);
    g.setColour (ink);
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true).withExtraKerningFactor (0.06f));
    g.drawText ("STEREO BUS COMPRESSOR", juce::Rectangle<float> (0.0f, border.getBottom() + 4.0f, r.getWidth(), 18.0f),
                juce::Justification::centred, false);
}

void BusCompPanel::update()
{
    meter.setReduction (comp.takeGainReduction());   // zero when the compressor is out
}
