#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Desk.h"

/** One channel strip laid out like a channel-strip plugin, in two rows: Input, Equaliser, Gate over
    Compressor, Aux sends, Output (the channel's chain order is at the top, as blocks that can be
    dragged into another order once unlocked). Every control
    is the console's own (they move together); the meters show the readings the console's bank takes.
    It also has controls the console strip doesn't: HF and LF bell/shelf, HMF/LMF Q, gate release and the
    gate's side-chain filter. Fixed design size. */
class ChannelStripPanel : public juce::Component,
                          private juce::Timer
{
public:
    ChannelStripPanel (ChannelStrip&, StripBank& consoleBank, int stripIndex);
    ~ChannelStripPanel() override;

    static constexpr int designW = 640, designH = 496;

    /** The arrow buttons: -1 previous channel, +1 next (the window decides what that means). */
    std::function<void (int delta)> onStep;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChannelStrip& strip;
    StripBank& bank;
    const int index;

    juce::Label scribble;
    juce::TextButton prevButton, nextButton;   // at the two ends of the header
    Knob gain, trim, hpf, pan;
    ParamButton phase;
    ChainLinkButton gainLink;
    LedMeter inMeter, outMeter;
    juce::OwnedArray<Knob> eqKnobs;   // gain, freq per band
    // Only in this window: HF and LF bell/shelf, HMF and LMF Q.
    ParamButton hfBell, lfBell;
    Knob hmfQ, lmfQ;
    Knob gateThr, gateRange;
    ParamButton gateFast;
    ParamButton eqIn, gateIn;
    GrMeter gateMeter { 60.0f };
    // Only in this window: gate release and the side-chain filter.
    Knob gateRelease;
    ParamButton scIn;
    juce::OwnedArray<RadioParamButton> scTypeButtons;   // HPF, LPF, BPF
    Knob scFreq, scQ;
    juce::Rectangle<int> scBox;
    CompressorPanel compPanel;
    ParamButton compIn;
    juce::OwnedArray<RadioParamButton> compTypeButtons;   // 60, 76, LA, G
    juce::OwnedArray<Knob> auxKnobs;
    juce::OwnedArray<ParamButton> auxPre;
    ParamButton mainButton, mute, solo;
    juce::OwnedArray<ParamButton> subButtons;
    Fader fader;

    // Return strips: the gate slot can be a de-esser.
    juce::TextButton gateModeButton;
    std::unique_ptr<Knob> deFreq, deReduction;
    std::unique_ptr<ParamButton> deWide, deGentle, deIn;   // deIn: the de-esser bypass shown as an In button
    GrMeter deMeter;

    bool shownDeEss = false;
    std::array<bool, Config::numAux / 2> shownAuxLink {};
    ChannelStrip::Chain shownChain {};
    std::uint32_t lastPops = 0;

    struct Module { juce::String title; juce::Rectangle<int> box; };
    std::vector<Module> modules;

    class ChainBlocks;
    std::unique_ptr<ChainBlocks> chainBlocks;   // the signal chain at the top right: blocks to drag, and Lock

    void showAuxLink (int pair, bool linked);
    void updateVisibility();
    void timerCallback() override;
};

/** The channel window: double-click any knob on a channel strip. Scales the panel to fit.
    Its arrow buttons ask for the previous / next channel of the same bank (onStep, called after the click
    has finished, so the owner may swap the panel with showStrip). */
class ChannelStripWindow : public juce::DocumentWindow
{
public:
    ChannelStripWindow (const juce::String& title, ChannelStrip&, StripBank& consoleBank, int stripIndex,
                        std::function<bool (const juce::KeyPress&)> keys);

    /** The close button, and the arrows (-1 previous, +1 next). */
    void setCallbacks (std::function<void()> close, std::function<void (int delta)> step)
    {
        onClose = std::move (close);
        onStep = std::move (step);
    }

    /** Shows another channel in this window. */
    void showStrip (const juce::String& title, ChannelStrip&, StripBank& consoleBank, int stripIndex);

    void closeButtonPressed() override { if (onClose) onClose(); }
    bool keyPressed (const juce::KeyPress& k) override { return keys != nullptr && keys (k); }

private:
    class Content;
    Content* content = nullptr;
    juce::ComponentBoundsConstrainer constrainer;
    std::function<void()> onClose;
    std::function<void (int)> onStep;
    std::function<bool (const juce::KeyPress&)> keys;
};
