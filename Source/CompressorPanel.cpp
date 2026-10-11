#include "CompressorPanel.h"
#include "StripStyle.h"

using namespace StripStyle;

namespace
{
    juce::String formatRatio (float r)
    {
        if (r >= 40.0f)
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e:1"));   // infinity
        return juce::String (r, r < 10.0f ? 1 : 0) + ":1";
    }

    juce::String formatSeconds (float s)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 1) + " s";
    }
}

CompressorPanel::CompressorPanel (ChannelCompressor& c)
    : comp (c),
      vcaThreshold (*c.vca.threshold, "Thresh", compCap),
      vcaRatio (*c.vca.ratio, "Ratio", compCap),
      vcaOutput (*c.vca.output, "Output", compCap),
      vcaOverEasy (*c.vca.overEasy, "OverEasy"),
      fetInput (*c.fet.input, "Input", compCap),
      fetOutput (*c.fet.output, "Output", compCap),
      fetAttack (*c.fet.attack, "Attack", compCap),
      fetRelease (*c.fet.release, "Release", compCap),
      fetAll (*c.fet.all, "All"),
      optoPeak (*c.opto.peakReduction, "Peak red", compCap),
      optoGain (*c.opto.gain, "Gain", compCap),
      optoComp (*c.opto.limit, 0, "Comp"),
      optoLimit (*c.opto.limit, 1, "Limit"),
      sslThreshold (*c.ssl.threshold, "Thresh", compCap),
      sslRatio (*c.ssl.ratio, "Ratio", compCap),
      sslRelease (*c.ssl.release, "Release", compCap),
      sslFast (*c.ssl.fast, "Fast atk")
{
    // 60
    vcaThreshold.setEndLabels ("-50", "0");
    vcaRatio.setEndLabels ("1", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")));
    vcaRatio.setFormatter (formatRatio);
    vcaOutput.setEndLabels ("-20", "+20");
    vcaOverEasy.setTooltip ("OverEasy: a soft knee, so compression comes in gradually around the threshold");

    // 76
    fetInput.setEndLabels ("0", "10");
    fetOutput.setEndLabels ("-", "+");
    fetAttack.setEndLabels ("Slow", "Fast");
    fetRelease.setEndLabels ("Slow", "Fast");
    for (int i = 0; i < 4; ++i)
    {
        auto* b = fetRatios.add (new RadioParamButton (*c.fet.ratio, i, juce::String (juce::roundToInt (FetCompressor::ratios[i]))));
        b->setTooltip ("Ratio " + b->getButtonText() + ":1");
    }
    fetAll.setTooltip ("All buttons in: about 60:1 with extra drive and distortion");
    redButton (fetAll);

    // LA
    optoPeak.setEndLabels ("0", "100");
    optoGain.setEndLabels ("0", "30");
    optoComp.setTooltip ("Compress: about 3:1, gentle");
    optoLimit.setTooltip ("Limit: about 10:1, firmer");

    // G
    sslThreshold.setEndLabels ("-40", "0");
    sslRatio.setEndLabels ("1", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")));
    sslRatio.setFormatter (formatRatio);
    sslRelease.setEndLabels ("0.1", "4s");
    sslRelease.setFormatter (formatSeconds);
    sslFast.setTooltip ("Fast attack: 0.3 ms instead of 3 ms");

    for (int t = 0; t < ChannelCompressor::numTypes; ++t)
        for (auto* control : controlsFor (t))
            addChildComponent (control);

    grMeter.setTooltip ("Compressor gain reduction");
    addAndMakeVisible (grMeter);
    update();
}

std::vector<juce::Component*> CompressorPanel::controlsFor (int type)
{
    switch (type)
    {
        case ChannelCompressor::vca60:
            return { &vcaThreshold, &vcaRatio, &vcaOutput, &vcaOverEasy };
        case ChannelCompressor::fet76:
        {
            std::vector<juce::Component*> list { &fetInput, &fetOutput, &fetAttack, &fetRelease, &fetAll };
            for (auto* b : fetRatios) list.push_back (b);
            return list;
        }
        case ChannelCompressor::optoLA:
            return { &optoPeak, &optoGain, &optoComp, &optoLimit };
        default:
            return { &sslThreshold, &sslRatio, &sslRelease, &sslFast };
    }
}

std::vector<Knob*> CompressorPanel::knobs()
{
    return { &vcaThreshold, &vcaRatio, &vcaOutput, &fetInput, &fetOutput, &fetAttack, &fetRelease,
             &optoPeak, &optoGain, &sslThreshold, &sslRatio, &sslRelease };
}

void CompressorPanel::setKnobDoubleClick (std::function<void()> f)
{
    for (auto* k : knobs())
        k->setOnDoubleClick (f);
}

void CompressorPanel::update()
{
    const int t = comp.getType();
    if (t == shownType)
        return;
    shownType = t;
    for (int i = 0; i < ChannelCompressor::numTypes; ++i)
        for (auto* control : controlsFor (i))
            control->setVisible (i == t);
    if (onTypeChanged)
        onTypeChanged();
}

void CompressorPanel::resized()
{
    const int w = getWidth();
    const int left = 2, right = w - 2 - Knob::preferredWidth;
    const auto knob = [] (int x, int y) { return juce::Rectangle<int> (x, y, Knob::preferredWidth, Knob::preferredHeight); };
    const auto sideButton = juce::Rectangle<int> (right, 78, Knob::preferredWidth, 18);   // beside the second row's left knob

    // 60: Threshold, Ratio / Output, OverEasy
    vcaThreshold.setBounds (knob (left, 10));
    vcaRatio.setBounds (knob (right, 10));
    vcaOutput.setBounds (knob (left, 62));
    vcaOverEasy.setBounds (sideButton);

    // 76: Input, Output / Attack, Release / ratio buttons and All
    fetInput.setBounds (knob (left, 10));
    fetOutput.setBounds (knob (right, 10));
    fetAttack.setBounds (knob (left, 62));
    fetRelease.setBounds (knob (right, 62));
    auto row = juce::Rectangle<int> (4, 116, w - 8, 17);
    const int bw = row.getWidth() / 5;
    for (auto* r : fetRatios)
        r->setBounds (row.removeFromLeft (bw).reduced (1, 0));
    fetAll.setBounds (row.reduced (1, 0));

    // LA: Peak reduction, Gain / Comp | Limit
    optoPeak.setBounds (knob (left, 10));
    optoGain.setBounds (knob (right, 10));
    auto switchRow = juce::Rectangle<int> (6, 78, w - 12, 18);
    optoComp.setBounds (switchRow.removeFromLeft (switchRow.getWidth() / 2).withTrimmedRight (1));
    optoLimit.setBounds (switchRow.withTrimmedLeft (1));

    // G: Threshold, Ratio / Release, Fast attack
    sslThreshold.setBounds (knob (left, 10));
    sslRatio.setBounds (knob (right, 10));
    sslRelease.setBounds (knob (left, 62));
    sslFast.setBounds (sideButton);

    grMeter.setBounds (4, 141, w - 8, 8);
}
