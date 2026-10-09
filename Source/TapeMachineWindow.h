#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Desk.h"
#include "MeterBridge.h"

/** The tape machine as a front panel of its own, after the 1980s cassette multitrack in meterbridge.jpg:
    on the left the red Power key, the moving cassette in its well, the counter, the transport keys, the
    locators, tape speed, pitch and record level; on the right the double-stacked meter section.
    Everything works: it is the same machine as the one on the console. Fixed design size, scaled to fit. */
class TapeMachinePanel : public juce::Component,
                         private juce::Timer
{
public:
    explicit TapeMachinePanel (AudioEngine&);

    static constexpr int leftW = 340, designW = leftW + MeterBridge::designW, designH = MeterBridge::designH;

    void setCassetteAnimated (bool a) { cassette.setAnimated (a); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    TapeMachine& tape;

    Cassette cassette;
    TapeClock clock;
    TransportKey power { "Power", TransportKey::power },
                 rew { "Rew", TransportKey::rewind, juce::Colour (0xffffb22e) },
                 ff { "F Fwd", TransportKey::fastForward, juce::Colour (0xffffb22e) },
                 stop { "Stop", TransportKey::stop, juce::Colours::transparentBlack },
                 play { "Play", TransportKey::play },
                 rec { "Record", TransportKey::record, juce::Colour (0xffff3b2a) },
                 erase { "Erase", TransportKey::erase, juce::Colours::transparentBlack };
    juce::TextButton setIn { "Set In" }, setOut { "Set Out" }, loopButton { "Loop" }, punchButton { "Punch" };
    ParamButton wowButton;
    juce::OwnedArray<RadioParamButton> speedButtons;
    Knob pitchKnob, levelKnob;
    MeterBridge meters;

    juce::Rectangle<int> wellBox, transportBox, controlsBox, ventBox;

    void timerCallback() override;
};

/** The Tape machine window (top bar button). A normal window; closing it only hides it.
    While it is visible the console's own cassette stands still (MainComponent, via onShownChanged). */
class TapeMachineWindow : public juce::DocumentWindow
{
public:
    TapeMachineWindow (AudioEngine&, juce::PropertiesFile& settings, std::function<void()> saveSettings);

    void show();
    void closeButtonPressed() override;
    void visibilityChanged() override;
    void setCassetteAnimated (bool);

    std::function<void (bool shown)> onShownChanged;

private:
    class Content;
    Content* content = nullptr;   // owned by the window
    juce::PropertiesFile& settings;
    std::function<void()> saveSettings;
    juce::ComponentBoundsConstrainer constrainer;
};
