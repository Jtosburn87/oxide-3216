#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BusCompressor.h"
#include "Widgets.h"

/** Black VU-style gain reduction meter, 0..20 dB, with a moving needle. */
class GrNeedleMeter : public juce::Component
{
public:
    void setReduction (float db);   // called ~30 times a second; the needle has its own ballistics
    void paint (juce::Graphics&) override;

private:
    float needle = 0.0f;
};

/** Large blue-capped faceplate knob with printed dots and legends around it.
    Continuous (threshold, make up) or a 6-position switch (attack, release, ratio, HPF).
    Clicking a printed legend on a switch jumps to that position. */
class BusKnob : public juce::Component
{
public:
    struct Legend { float value; juce::String text; };

    /** Continuous knob. */
    BusKnob (Param&, juce::String title, juce::String unit, int dots, std::vector<Legend> legends);
    /** Six-position switch; legends are the positions. */
    BusKnob (Param&, juce::String title, juce::String unit, const char* const* positionLabels);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    ParamSlider slider;
    juce::String title, unit;
    std::vector<Legend> legends;
    int dots = 0;
    bool stepped = false;

    juce::Point<float> dialCentre() const;
    float angleFor (float value) const;   // radians from 12 o'clock
};

/** The stereo bus compressor faceplate (after ref-buscomp.webp, without any maker's name). */
class BusCompPanel : public juce::Component
{
public:
    explicit BusCompPanel (BusCompressor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void update();   // ~30 times a second

    static constexpr int width = 300, height = 476;

private:
    BusCompressor& comp;
    GrNeedleMeter meter;
    BusKnob threshold, makeup, attack, release, ratio, hpf;
    ParamButton inButton;
    juce::Rectangle<float> border;
};
