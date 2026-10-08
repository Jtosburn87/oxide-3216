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

    /** true: the bridge meter shows the input (after the trim); false: the output (post-fader). */
    void setMeterInput (bool);

    /** Sections that can be minimised, one bit each. The bank minimises a section on all its strips at once. */
    enum Collapsible { eqSection = 1, gateSection = 2, compSection = 4, auxSection = 8 };
    void setCollapsed (int mask);
    static int heightFor (int collapsedMask);
    std::function<void (int bit)> onToggleSection;

    /** Unlocked: EQ, gate and compressor show a button to move them earlier in this channel's chain. */
    void setChainUnlocked (bool);

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
    bool meterInput = false;
    int collapsed = 0;
    std::array<juce::TextButton, 4> foldButtons;
    std::array<juce::TextButton, 3> moveButtons;   // EQ, gate, compressor
    bool chainUnlocked = false;
    ChannelStrip::Chain shownChain {};
    void moveEarlier (int stage);
    std::vector<juce::Component*> sectionComponents (int bit);

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
    juce::TextButton power { "Power" }, rew { "Rew" }, ff { "FF" }, stop { "Stop" }, play { "Play" }, rec { "Rec" }, erase { "Erase" };
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

    /** Minimised sections (StripComponent::Collapsible bits). The bank's height follows. */
    int getCollapsed() const noexcept { return collapsed; }
    void setCollapsed (int mask);
    std::function<void()> onCollapseChanged;   // the user minimised or restored a section
    void setChainUnlocked (bool);

private:
    juce::OwnedArray<StripComponent> strips;
    juce::TextButton meterButton { "OUT" };   // flips the whole bank's meters between input and output
    bool meterInput = false;
    int collapsed = 0;
};

/** The console at a fixed size, made of three sections that can each be taken out into a window of
    their own. Whatever is docked is laid out between the two end cheeks. MainComponent scales it for zoom. */
class Desk : public juce::Component,
             private juce::Timer,
             private juce::ComponentListener
{
public:
    enum Section { inputSection, centreSection, returnSection, numSections };

    explicit Desk (AudioEngine&, PluginContext* plugins = nullptr);
    ~Desk() override;

    juce::Component& getSection (Section);
    static juce::String sectionName (Section);

    bool isDocked (Section s) const { return docked[(size_t) s]; }

    /** false: the section leaves the console (the caller shows it in a window); true: it comes back. */
    void setDocked (Section, bool);

    /** Minimised sections of a strip bank (input or return section). */
    int getCollapsed (Section) const;
    void setCollapsed (Section, int mask);
    std::function<void()> onCollapseChanged;

    /** Lets every channel's EQ, gate and compressor be reordered (Options > Unlock chain order). */
    void setChainUnlocked (bool);

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
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
};

/** One section with end cheeks either side, shown in its own window. */
class SectionFrame : public juce::Component,
                     private juce::ComponentListener
{
public:
    explicit SectionFrame (juce::Component& section);
    ~SectionFrame() override;

    /** The section changed size (sections minimised), so the frame did too. */
    std::function<void()> onSizeChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Component& section;
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
};
