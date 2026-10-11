#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Desk.h"

/** One channel strip laid out like a channel-strip plugin, in two rows: Input, Equaliser, Gate over
    Compressor, Aux sends, Output (the channel's actual chain order is printed at the top). Every control
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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChannelStrip& strip;
    StripBank& bank;
    const int index;

    juce::Label scribble;
    Knob trim, hpf, pan;
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
    std::unique_ptr<ParamButton> deWide, deGentle, deBypass;
    GrMeter deMeter;

    bool shownDeEss = false;
    std::array<bool, Config::numAux / 2> shownAuxLink {};
    ChannelStrip::Chain shownChain {};
    std::uint32_t lastPops = 0;

    struct Module { juce::String title; juce::Rectangle<int> box; };
    std::vector<Module> modules;

    juce::String chainText() const;
    void showAuxLink (int pair, bool linked);
    void updateVisibility();
    void timerCallback() override;
};

/** The channel window: double-click any knob on a channel strip. Scales the panel to fit. */
class ChannelStripWindow : public juce::DocumentWindow
{
public:
    ChannelStripWindow (const juce::String& title, ChannelStrip&, StripBank& consoleBank, int stripIndex,
                        std::function<void()> onClose, std::function<bool (const juce::KeyPress&)> keys);

    void closeButtonPressed() override { if (onClose) onClose(); }
    bool keyPressed (const juce::KeyPress& k) override { return keys != nullptr && keys (k); }

private:
    class Content;
    juce::ComponentBoundsConstrainer constrainer;
    std::function<void()> onClose;
    std::function<bool (const juce::KeyPress&)> keys;
};
