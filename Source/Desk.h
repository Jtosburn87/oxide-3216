#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"
#include "Cassette.h"

/** Fixed console geometry, in desk units (pixels at 100% zoom). */
namespace DeskLayout
{
    constexpr int stripW = 110, cheekW = 30, centreW = 640;
    constexpr int bridgeH = 84, knobH = 930, faderH = 270;
    constexpr int totalH = bridgeH + knobH + faderH;

    // Centre fader area grid: Sub 1-4, Aux 1-8, then Main (1.8 columns wide) behind a divider.
    constexpr float gridX = 8.0f, gridW = 624.0f, colW = gridW / 13.8f;
    inline float subColumnX (int k)  { return gridX + (float) k * colW; }
    inline float auxColumnX (int k)  { return gridX + (float) (Config::numSubs + k) * colW; }
    inline float mainColumnX()       { return gridX + 12.0f * colW; }
}

/** One channel strip column: bridge meter, knob area, fader area, scribble strip. */
class StripComponent : public juce::Component
{
public:
    explicit StripComponent (ChannelStrip&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void update();   // meters and pop flash, from the desk timer

private:
    ChannelStrip& strip;

    LedMeter meter;
    Knob trim, hpf, pan;
    juce::OwnedArray<Knob> eqKnobs;            // gain, freq per band
    Knob gateThr, gateRange;
    ParamButton gateFast;
    Knob compInput, compOutput, compAttack, compRelease;
    juce::OwnedArray<RadioParamButton> ratioButtons;
    ParamButton compAll, compIn;
    GrMeter grMeter;
    juce::OwnedArray<Knob> auxKnobs;
    juce::OwnedArray<ParamButton> auxPre;
    ParamButton mainButton;
    juce::OwnedArray<ParamButton> subButtons;
    ParamButton mute, solo;
    Fader fader;
    juce::Label scribble;

    std::uint32_t lastPops = 0;

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

/** Centre section: tape machine, transport and group EQs on the left; bus meters in the bridge;
    main fader at the far right of the fader area. */
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

    Cassette cassette;
    TapeClock clock;
    juce::OwnedArray<RadioParamButton> speedButtons;
    juce::TextButton power { "Power" }, rew { "Rew" }, stop { "Stop" }, play { "Play" }, rec { "Rec" }, erase { "Erase" };
    Knob recordLevel;
    juce::OwnedArray<ParamButton> armButtons;
    juce::OwnedArray<Knob> groupEqKnobs;   // [sub][band][gain, freq]

    std::array<std::array<LedMeter, 2>, Config::numSubs> subMeters;
    std::array<LedMeter, Config::numAux> auxMeters;
    std::array<LedMeter, 2> mainMeters;
    Fader mainFader;
    juce::Label mainScribble;

    juce::Rectangle<int> deckBox, armBox, levelBox, groupEqBox;

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
