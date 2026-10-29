#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"
#include "Cassette.h"
#include "TempoPanel.h"
#include "InsertsUI.h"
#include "CompressorPanel.h"
#include "ShuttlePanel.h"
#include "SceneWindow.h"

/** Fixed console geometry, in desk units (pixels at 100% zoom). */
namespace DeskLayout
{
    constexpr int stripW = 110, cheekW = 30, centreW = 780;
    constexpr int knobH = 1056, faderH = 270;   // no meter bridge: the meters sit beside the faders
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
    /** bank: the strips of its bank (input or return), for selecting several scribble strips and for the faders
        grouped by colour. */
    StripComponent (ChannelStrip&, const juce::OwnedArray<ChannelStrip>* bank = nullptr);
    ~StripComponent() override;
    void mouseDown (const juce::MouseEvent&) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    /** Meters and pop flash, from the desk timer. With a source (the same strip in another bank),
        shows the meter readings that one took instead of taking its own. */
    void update (const StripComponent* source = nullptr);

    /** A knob was double-clicked: open this channel in a big window. */
    std::function<void()> onOpenStrip;

    /** Meter readings, taken once a frame by the console's bank (update). */
    struct Readings { float out = 0.0f, in = 0.0f, gr = 0.0f, gate = 0.0f, deEss = 0.0f; };
    const Readings& getReadings() const noexcept { return readings; }
    /** The loudest readings since the last call (the Wi-Fi remote reads less often than the console draws). */
    Readings takeRemoteReadings() noexcept { auto r = remoteHeld; remoteHeld = {}; return r; }
    bool isMeterInput() const noexcept { return meterInput; }

    /** The Wi-Fi remote moving the fader or switching mute / solo / main / a sub: as by hand on the console, so the
        colour group (and for the switches the selected strips) follow. */
    void remoteFader (float value) { moveFader (value); }
    void remoteSwitch (Param*, bool on);

    /** Sends on fader: the fader moves this aux send (0-7) instead of the channel's level; -1 = its own level. */
    void setSendsOnFader (int aux) { fader.setSend (aux >= 0 ? strip.controls.auxLevel[aux] : nullptr); }

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
    Knob gain, trim, hpf, pan;
    ParamButton phase;
    ChainLinkButton gainLink;
    juce::OwnedArray<Knob> eqKnobs;            // gain, freq per band
    Knob gateThr, gateRange;
    ParamButton gateFast;
    ParamButton eqIn, gateIn;   // EQ in (on its title line), gate in (beside Fast atk)
    GrMeter gateMeter { 60.0f };   // how far the gate has closed
    CompressorPanel compPanel;   // the controls of the compressor type chosen
    ParamButton compIn;
    juce::OwnedArray<Knob> auxKnobs;
    juce::OwnedArray<ParamButton> auxPre;
    ParamButton mainButton;
    juce::OwnedArray<ParamButton> subButtons;
    ParamButton mute, solo;
    Fader fader;
    juce::Label scribble;

    // Scribble strip colour and selection: click a strip to select it, Shift+click the ones between, Ctrl+click one
    // more; the colour box (left of the name) colours every selected strip. Same colour = faders move together.
    struct ColourBox : juce::Component, juce::SettableTooltipClient
    {
        juce::Colour colour;
        juce::Colour text;      // the number, readable on the colour
        juce::String number;    // the channel number (fixed)
        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    } colourBox;
    const juce::OwnedArray<ChannelStrip>* bank;
    int shownColour = -1;
    bool shownSelected = false;
    int bankIndex() const;
    void chooseColour();
    void moveFader (float value);
    /** Mute, main, solo and sub assigns: the selected strips (if this is one) and the colour group switch too. */
    std::vector<ChannelStrip*> followers() const;
    void switchFollowers (Param* ChannelStrip::Controls::* which, bool on);
    void switchSubFollowers (int sub, bool on);

    std::uint32_t lastPops = 0;
    Readings readings;   // the last ones taken (or copied from the source)
    Readings remoteHeld;   // the loudest since the remote last read them
    bool meterInput = false;
    int collapsed = 0;
    std::array<juce::TextButton, 4> foldButtons;
    std::array<juce::TextButton, 3> moveButtons;   // EQ, gate, compressor

    // Return strips: the gate slot can be switched to a de-esser.
    juce::TextButton gateModeButton;
    std::unique_ptr<Knob> deFreq, deReduction;
    std::unique_ptr<ParamButton> deWide, deGentle, deBypass;
    GrMeter deMeter;   // de-esser reduction
    bool shownDeEss = false;
    std::array<bool, Config::numAux / 2> shownAuxLink {};   // linked aux pairs: the even send knob is the pair's pan
    void showAuxLink (int pair, bool linked);
    void updateVisibility();
    bool chainUnlocked = false;
    ChannelStrip::Chain shownChain {};
    void moveEarlier (int stage);
    std::vector<juce::Component*> sectionComponents (int bit);

    struct Section { juce::String title; juce::Rectangle<int> box; int titleIndent = 0; };
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

/** The talkback key: hold it to talk, double-click it to latch talkback on (a click stops it). Lit while talking. */
class TalkButton : public juce::TextButton
{
public:
    explicit TalkButton (std::atomic<bool>& talking);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void clicked() override;   // keyboard: Space / Enter latches or stops
    void update()
    {
        latched = latched && talking.load();
        setToggleState (talking.load(), juce::dontSendNotification);
    }

private:
    std::atomic<bool>& talking;
    bool latched = false;
};

/** Moves a cassette and a tape counter on by one frame (30 a second) from the tape's state. Used by the
    console's tape machine and by the Tape machine window. */
void updateTapeDisplay (TapeMachine&, Cassette&, TapeClock&);

/** Stacks its children top to bottom, each the full width (the app buttons in the centre section). */
class ButtonColumn : public juce::Component
{
public:
    static constexpr int buttonH = 26, gap = 4;   // compact: the Scene box sits under the column
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
    ShuttlePanel& getShuttle() { return shuttle; }
    /** The main L / R peaks since the Wi-Fi remote last read them (linear). */
    float takeRemoteMain (int side) noexcept { const float p = remoteMain[(size_t) side]; remoteMain[(size_t) side] = 0.0f; return p; }

    /** The SBC button over the master inserts was clicked. */
    std::function<void()> onOpenBusComp;
    /** The Sends on Fader button (under the master inserts) was clicked. */
    std::function<void()> onSendsOnFader;
    void showSendsOnFader (const juce::String& text, bool on) { inserts.showSendsOnFader (text, on); }

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
    juce::OwnedArray<ArmButton> armButtons;
    juce::TextButton setIn { "Set In" }, setOut { "Set Out" }, loopButton { "Loop" }, punchButton { "Punch" };
    ParamButton wowButton;
    juce::Rectangle<int> locateBox;
    juce::int64 shownIn = -1, shownOut = -1;
    juce::OwnedArray<Knob> groupEqKnobs;   // [sub][band] gain (fixed bands)

    std::array<std::array<LedMeter, 2>, Config::numSubs> subMeters;
    std::array<LedMeter, Config::numAux> auxMeters;
    std::array<LedMeter, 2> mainMeters;
    std::array<float, 2> remoteMain {};
    Fader mainFader;
    juce::Label mainScribble;
    TempoPanel tempoPanel;         // where the bus compressor was (that is in its own window now: SBC)
    juce::Rectangle<int> tempoBox;
    static constexpr int tempoBoxW = 300, tempoBoxH = 476;   // the size the bus compressor had, so nothing else moves
    InsertsSection inserts;
    ShuttlePanel shuttle;          // jog / shuttle, In / Out keys and locate memories, right of the group EQ
    juce::Rectangle<int> shuttleBox;
    juce::Rectangle<int> insertsBox;
    juce::OwnedArray<Fader> busFaders;          // Sub 1-4, then A1-A8
    juce::OwnedArray<juce::Label> busScribbles;
    juce::OwnedArray<ParamButton> busMainButtons;   // above each bus fader: Sub 1-4, then A1-A8
    int limitHold = 0;

    Knob monitorLevel, tbLevel;
    TalkButton talkButton;
    juce::TextButton tbSetup { "Setup" };
    XlrSocket tbMic;               // talkback mic in: right-click picks the interface input patched to TB In
    SceneReadout sceneReadout;     // the scene number; right-click: Scene Memory
    juce::TextButton scenePrev, sceneGo { "GO" }, sceneNext;
    juce::Rectangle<int> sceneBox;
    juce::String shownTbMic { "?" };   // anything update() can't produce, so the first update sets the tooltip
    static constexpr int tbMicH = 44;
    PluginContext* context;        // its `changed` saves the routing
    juce::Rectangle<int> deckBox, armBox, levelBox, groupEqBox, meterBox, appBox, monitorBox;

    /** Centre of meter slot i in the meter panel: subs, then auxes, then main (with room for the LIM lamp). */
    float meterSlotX (int slot) const { return (float) meterBox.getX() + 10.0f + ((float) slot + 0.5f) * (float) (meterBox.getWidth() - 50) / 13.0f; }

    void openErase();
    void showTbMicMenu();
    juce::String tbMicInputName() const;   // the interface input on TB In, or empty
};

/** A bank of 16 channel strips (the inputs or the tape returns). */
class StripBank : public juce::Component
{
public:
    StripBank (const juce::OwnedArray<ChannelStrip>&, BankLinks& links);
    ~StripBank() override;

    /** A second bank of the same strips, for a window of its own: the same controls, starting with this
        bank's minimised sections and meter switch, and showing the meter readings this bank takes. */
    std::unique_ptr<StripBank> makeCopy();
    bool hasCopies() const noexcept { return ! copies.isEmpty(); }

    /** A knob on strip i (0-based) was double-clicked (on this bank or a copy). */
    std::function<void (int strip)> onOpenStrip;
    const StripComponent& getStrip (int i) const { return *strips[i]; }
    StripComponent& getStrip (int i) { return *strips[i]; }
    /** Channel windows showing this bank's meter readings: while any is open, the bank keeps updating. */
    int readers = 0;

    void resized() override;
    void update();   // also updates the copies

    /** Minimised sections (StripComponent::Collapsible bits). The bank's height follows. */
    int getCollapsed() const noexcept { return collapsed; }
    void setCollapsed (int mask);
    std::function<void()> onCollapseChanged;   // the user minimised or restored a section
    void setChainUnlocked (bool);

    /** Sends on fader for the whole bank (and its copies): aux 0-7, -1 = off. */
    void setSendsOnFader (int aux);
    int getSendsOnFader() const noexcept { return sendsOnFader; }

private:
    juce::OwnedArray<StripComponent> strips;
    juce::OwnedArray<juce::TextButton> linkButtons;   // one per strip, beside its pan knob
    std::vector<int> shownLinkState;                  // per strip: partner, -1 unlinked, -2 waiting for a partner
    void clickLink (int strip);
    void updateLinkButtons();
    juce::TextButton meterButton { "Meters In/Out" };   // lit = input; flips the whole bank's meters between input and output
    bool meterInput = false;
    int collapsed = 0;
    bool chainUnlocked = false;
    int sendsOnFader = -1;
    void setMeterInput (bool);

    const juce::OwnedArray<ChannelStrip>& channelStrips;
    BankLinks& links;
    juce::Component::SafePointer<StripBank> original;   // a copy: the bank it copies
    juce::Array<StripBank*> copies;
    void updateFrom (const StripBank& source);
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

    /** false: the section leaves the console (the caller shows it in a window); true: it comes back.
        Only the centre section moves; a strip bank stays on the console and its window gets a copy. */
    void setDocked (Section, bool);

    /** A copy of the input or return bank, for a window of its own (see StripBank::makeCopy). */
    std::unique_ptr<StripBank> makeBankCopy (Section s) { return (s == inputSection ? inputs : returns).makeCopy(); }

    /** The console's input or return bank (channel windows read its meters). */
    StripBank& getBank (Section s) { return s == inputSection ? inputs : returns; }
    /** A knob on a channel strip was double-clicked: bank (input or return section) and strip index. */
    std::function<void (Section, int strip)> onOpenStrip;

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
    /** The Jog / shuttle box (its keys have keyboard shortcuts). */
    ShuttlePanel& getShuttle() { return centre.getShuttle(); }
    float takeRemoteMain (int side) noexcept { return centre.takeRemoteMain (side); }
    /** The SBC button (over the master inserts) opens the stereo bus compressor window. */
    void setOnOpenBusComp (std::function<void()> f) { centre.onOpenBusComp = std::move (f); }

    /** Lets every channel's EQ, gate and compressor be reordered (Options > Unlock chain order). */
    void setChainUnlocked (bool);

    /** Sends on Fader: every channel fader (both banks and their windows) moves its send to this aux (0-7) instead of
        the channel's level; -1 = off. A linked aux pair is one send (its odd aux), whichever of the two is picked. */
    void setSendsOnFader (int aux);
    int getSendsOnFader() const noexcept { return sendsOnFader; }

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
    int sendsOnFader = -1, appliedSendsOnFader = -1;   // picked / on the faders now (a linked pair's odd aux)
    juce::String shownSendsOnFader;

    void chooseSendsOnFader();    // the button's menu: Off, Aux 1-8 (linked pairs as one)
    void applySendsOnFader();     // also follows aux links made or broken while it is on

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
