#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"
#include "BusCompPanel.h"

/** The Talkback Settings window (Setup, next to Talk on the console): where the talkback goes (Aux 5-8),
    dimming the monitors while talking, and the TB limiter - after the SSL 4000 E's listen mic compressor -
    with its Input, Blend and Makeup knobs and a gain reduction meter. */
class TalkbackSettings : public juce::Component,
                         private juce::Timer
{
public:
    /** Opens the window (or brings it to the front), centred on the screen at full size. */
    static void show (AudioEngine&);

    explicit TalkbackSettings (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    juce::OwnedArray<ParamButton> auxButtons;
    ParamButton dimButton, limiterButton;
    Knob inputKnob, blendKnob, makeupKnob;
    GrNeedleMeter grMeter;
    juce::Label note;
    juce::Rectangle<int> routeBox, limiterBox;

    static juce::Component::SafePointer<juce::DialogWindow> window;

    void timerCallback() override;
};
