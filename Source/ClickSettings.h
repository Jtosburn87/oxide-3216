#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"

/** The click's own strip (Setup, next to Click in the Tempo box), laid out like a channel strip without the gate
    and compressor: level and pan, a four-band EQ, sends to Aux 1-8 (the headphone mixes; the click joins after the
    aux returns, so it never reaches the main mix through an effect) and the Main send on / off. */
class ClickSettings : public juce::Component
{
public:
    /** Opens the window (or brings it to the front), centred on the screen. */
    static void show (AudioEngine&);

    explicit ClickSettings (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    ParamButton clickOn, mainSend, eqIn;
    Knob level, pan;
    juce::OwnedArray<Knob> eqGain, eqFreq, auxSends;
    juce::Rectangle<int> levelBox, eqBox, auxBox, mainBox;

    static juce::Component::SafePointer<juce::DialogWindow> window;
};
