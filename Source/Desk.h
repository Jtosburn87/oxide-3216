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
    constexpr int knobH = 970, faderH = 270;   // no meter bridge: the meters sit beside the faders
    constexpr int totalH = knobH + faderH;

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

    /** Where the pan knob sits (the link button of a pair goes between two of them). */
    juce::Rectangle<int> getPanBounds() const { return pan.getBounds(); }

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

    // Return strips: the gate slot can be switched to a de-esser.
    juce::TextButton gateModeButton;
    std::unique_ptr<Knob> deFreq, deReduction;
    std::unique_ptr<ParamButton> deWide, deGentle, deBypass;
    bool shownDeEss = false;
    std::array<bool, Config::numAux / 2> shownAuxLink {};   // linked aux pairs: the even send knob is the pair's pan
    void showAuxLink (int pair, bool linked);
    void updateVisibility();
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

/** Moves a cassette and a tape counter on by one frame (30 a second) from the tape's state. Used by the
    console's tape machine and by the Tape machine window. */
void updateTapeDisplay (TapeMachine&, Cassette&, TapeClock&);

/** Stacks its children top to bottom, each the full width (the app buttons in the centre section). */
class ButtonColumn : public juce::Component
{
public:
    static constexpr int buttonH = 30, gap = 6;
    int heightFor (int numButtons) const { return numButtons * buttonH + juce::jmax (0, numButtons - 1) * gap; }

    void resized() override
    {
        int y = 0;
        for (auto* c : getChildren())
        {
            c->setBounds (0, y, getWidth(), buttonH);
            y += buttonH + gap;
        }
    }
    void childrenChanged() override { resized(); if (onChildrenChanged) onChildrenChanged(); }
    std::function<void()> onChildrenChanged;   // the column's height depends on how many buttons it holds
};

/** Record, Mix and Full buttons (the current one lit): which banks the console shows (see Desk::View).
    The buttons sit in the top bar, joined into one control. */
class ViewSwitch
{
public:
    ViewSwitch();
    void setView (int view);   // lights the button, no callback
    std::function<void (int view)> onChange;

    std::array<juce::TextButton, 3> buttons;
};

/** Centre section: tape machine, transport and group EQs on the left; the app buttons and the monitor
    level between the tape machine and the bus compressor; main fader at the far right of the fader area. */
class CentreSection : public juce::Component
{
public:
    CentreSection (AudioEngine&, PluginContext*);

    void paint (juce::Graphics&) override;
    void resized() override;
    void update();   // called by the desk timer

    /** MainComponent puts its buttons (Session, Audio device, ...) in here. */
    ButtonColumn appButtons;

    void setCassetteAnimated (bool a) { cassette.setAnimated (a); }
    void setCassetteFrozen (bool f) { cassette.setFrozen (f); }

private:
    AudioEngine& engine;
    TapeMachine& tape;

    Cassette cassette;
    TapeClock clock;
    juce::OwnedArray<RadioParamButton> speedButtons;
    TransportKey power { "Power", TransportKey::power },
                 rew { "Rew", TransportKey::rewind, juce::Colour (0xffffb22e) },
                 ff { "F Fwd", TransportKey::fastForward, juce::Colour (0xffffb22e) },
                 stop { "Stop", TransportKey::stop, juce::Colours::transparentBlack },
                 play { "Play", TransportKey::play },
                 rec { "Record", TransportKey::record, juce::Colour (0xffff3b2a) },
                 erase { "Erase", TransportKey::erase, juce::Colours::transparentBlack };
    Knob recordLevel, pitchKnob;
    float shownRecordedIps = -1.0f;   // in the speed box's title
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
    juce::OwnedArray<ParamButton> busMainButtons;   // above each bus fader: Sub 1-4, then A1-A8
    int limitHold = 0;

    Knob monitorLevel;
    juce::Rectangle<int> deckBox, armBox, levelBox, groupEqBox, meterBox, appBox, monitorBox;

    /** Centre of meter slot i in the meter panel: subs, then auxes, then main (with room for the LIM lamp). */
    float meterSlotX (int slot) const { return (float) meterBox.getX() + 10.0f + ((float) slot + 0.5f) * (float) (meterBox.getWidth() - 50) / 13.0f; }

    void openErase();
};

/** A bank of 16 channel strips (the inputs or the tape returns). */
class StripBank : public juce::Component
{
public:
    StripBank (const juce::OwnedArray<ChannelStrip>&, const std::vector<Param*>& links);

    void resized() override;
    void update();

    /** Minimised sections (StripComponent::Collapsible bits). The bank's height follows. */
    int getCollapsed() const noexcept { return collapsed; }
    void setCollapsed (int mask);
    std::function<void()> onCollapseChanged;   // the user minimised or restored a section
    void setChainUnlocked (bool);

private:
    juce::OwnedArray<StripComponent> strips;
    juce::OwnedArray<ParamButton> linkButtons;   // one per pair of strips, between their pan knobs
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

    /** Which banks the console shows next to the centre section: Record = inputs, Mix = returns,
        Full = both. Sections out in windows of their own are not affected. */
    enum View { recordView, mixView, fullView };
    void setView (View);
    View getView() const { return view; }

    /** Docked, and part of the current view. */
    bool isOnDesk (Section s) const
    {
        return docked[(size_t) s] && (view == fullView || s == centreSection || (s == inputSection) == (view == recordView));
    }

    /** false: the section leaves the console (the caller shows it in a window); true: it comes back. */
    void setDocked (Section, bool);

    /** Minimised sections of a strip bank (input or return section). */
    int getCollapsed (Section) const;
    void setCollapsed (Section, int mask);
    std::function<void()> onCollapseChanged;

    /** Where the app's buttons go (in the centre section, so they travel with it). */
    ButtonColumn& getAppButtons() { return centre.appButtons; }

    /** Off: the cassette's reels stand still (Options > Animate the cassette), to save CPU. */
    void setCassetteAnimated (bool a) { centre.setCassetteAnimated (a); }
    /** Frozen: the console's cassette holds still entirely (while the Tape machine window is up). */
    void setCassetteFrozen (bool f) { centre.setCassetteFrozen (f); }

    /** Lets every channel's EQ, gate and compressor be reordered (Options > Unlock chain order). */
    void setChainUnlocked (bool);

    /** The desk's size changed. sectionMoved: a section left or came back (the view refits);
        otherwise a bank minimised or restored a section (the zoom stays as it is). */
    std::function<void (bool sectionMoved)> onSizeChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Draws the black end cheeks at both sides of an area. */
    static void paintCheeks (juce::Graphics&, juce::Rectangle<int> area);

private:
    AudioEngine& engine;
    StripBank inputs, returns;
    CentreSection centre;
    std::array<bool, numSections> docked { true, true, true };
    View view = recordView;

    void relayout (bool sectionMoved = true);
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
