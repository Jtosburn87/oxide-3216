#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"
#include "Cassette.h"
#include "BusCompPanel.h"
#include "InsertsUI.h"

/** Fixed console geometry, in desk units (pixels at 100% zoom). */
namespace DeskLayout
{
    constexpr int stripW = 110, cheekW = 30, centreW = 780;
    constexpr int bridgeH = 84, knobH = 970, faderH = 270;
    constexpr int totalH = bridgeH + knobH + faderH;

    // Centre fader area grid: Sub 1-4, Aux 1-8, then Main (1.8 columns wide) behind a divider.
    constexpr float gridX = 8.0f, gridW = (float) centreW - 16.0f, colW = gridW / 13.8f;
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
    CentreSection (AudioEngine&, PluginContext*);

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
    juce::TextButton setIn { "Set In" }, setOut { "Set Out" }, loopButton { "Loop" }, punchButton { "Punch" };
    ParamButton wowButton;
    juce::Rectangle<int> locateBox;
    juce::int64 shownIn = -1, shownOut = -1;
    juce::OwnedArray<Knob> groupEqKnobs;   // [sub][band][gain, freq]

    std::array<std::array<LedMeter, 2>, Config::numSubs> subMeters;
    std::array<LedMeter, Config::numAux> auxMeters;
    std::array<LedMeter, 2> mainMeters;
    Fader mainFader;
    juce::Label mainScribble;
    BusCompPanel busPanel;
    InsertsSection inserts;
    juce::Rectangle<int> insertsBox;
    juce::OwnedArray<Fader> busFaders;          // Sub 1-4, then A1-A8
    juce::OwnedArray<juce::Label> busScribbles;
    int limitHold = 0;

    juce::Rectangle<int> deckBox, armBox, levelBox, groupEqBox;

    void styleTransport (juce::TextButton&, juce::Colour);
    void confirmErase();
};

/** A bank of 16 channel strips (the inputs or the tape returns). */
class StripBank : public juce::Component
{
public:
    explicit StripBank (const juce::OwnedArray<ChannelStrip>&);

    void resized() override;
    void update();

private:
    juce::OwnedArray<StripComponent> strips;
};

/** The console at a fixed size, made of three sections that can each be taken out into a window of
    their own. Whatever is docked is laid out between the two end cheeks. MainComponent scales it for zoom. */
class Desk : public juce::Component,
             private juce::Timer
{
public:
    enum Section { inputSection, centreSection, returnSection, numSections };

    explicit Desk (AudioEngine&, PluginContext* plugins = nullptr);

    juce::Component& getSection (Section);
    static juce::String sectionName (Section);

    bool isDocked (Section s) const { return docked[(size_t) s]; }

    /** false: the section leaves the console (the caller shows it in a window); true: it comes back. */
    void setDocked (Section, bool);

    /** The desk's size changed because a section left or came back. */
    std::function<void()> onSizeChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Draws the black end cheeks at both sides of an area. */
    static void paintCheeks (juce::Graphics&, juce::Rectangle<int> area);

private:
    AudioEngine& engine;
    StripBank inputs, returns;
    CentreSection centre;
    std::array<bool, numSections> docked { true, true, true };

    void relayout();
    void timerCallback() override;
};

/** One section with end cheeks either side, shown in its own window. */
class SectionFrame : public juce::Component
{
public:
    explicit SectionFrame (juce::Component& section);
    ~SectionFrame() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Component& section;
};
