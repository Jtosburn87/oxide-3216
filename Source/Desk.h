#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"

/** Fixed console geometry, in desk units (pixels at 100% zoom). */
namespace DeskLayout
{
    constexpr int stripW = 110, cheekW = 30, centreW = 640;
    constexpr int bridgeH = 84, knobH = 330, faderH = 270;
    constexpr int totalH = bridgeH + knobH + faderH;
}

/** One channel strip column: bridge meter, knob area, fader area, scribble strip. */
class StripComponent : public juce::Component
{
public:
    StripComponent (ChannelStrip&, bool isReturn);

    void paint (juce::Graphics&) override;
    void resized() override;
    void updateMeter() { meter.setLevel (strip.takePeak()); }

private:
    ChannelStrip& strip;
    const bool isReturn;

    LedMeter meter;
    Knob trim, hpf, pan;
    ParamButton mainButton, mute, solo;
    Fader fader;
    juce::Label scribble;

    struct Section { juce::String title; juce::Rectangle<int> box; };
    std::vector<Section> sections;
};

/** Seven-segment style tape counter. */
class TapeClock : public juce::Component
{
public:
    void setTime (double seconds, bool recording);
    void paint (juce::Graphics&) override;

private:
    juce::String text { "00:00.0" };
    bool rec = false;
};

/** Centre section: tape machine and transport on the left, main fader and meters. */
class CentreSection : public juce::Component
{
public:
    explicit CentreSection (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void update();   // called by the desk timer

private:
    AudioEngine& engine;
    TapeMachine& tape;

    TapeClock clock;
    juce::TextButton power { "Power" }, rew { "Rew" }, stop { "Stop" }, play { "Play" }, rec { "Rec" }, erase { "Erase" };
    Knob recordLevel;
    juce::OwnedArray<ParamButton> armButtons;

    std::array<LedMeter, 2> mainMeters;
    Fader mainFader;
    juce::Label mainScribble;

    juce::Rectangle<int> deckBox, armBox, levelBox;

    void styleTransport (juce::TextButton&, juce::Colour);
    void confirmErase();
};

/** The whole console at a fixed size. MainComponent scales it for zoom. */
class Desk : public juce::Component,
             private juce::Timer
{
public:
    explicit Desk (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    juce::OwnedArray<StripComponent> inputs, returns;
    CentreSection centre;

    void timerCallback() override;
};
