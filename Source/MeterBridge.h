#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TapeMachine.h"

/** The tape machine's meter section, after a 1980s cassette multitrack: for each track a number tab,
    a 15-LED peak bar from -20 to +6, a record LED and a record function switch (arms the track).
    Two rows of eight: tracks 1-8 above 9-16. Drawn at a fixed design size and scaled to the window.

    Meter 0 = -14 dBFS (+4 dBu with 0 dBFS = +18 dBu, as the preamps are calibrated), so +6 = -8 dBFS. */
class MeterBridge : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    explicit MeterBridge (TapeMachine&);

    static constexpr int designW = 572, designH = 690;
    static constexpr float referenceDbfs = -14.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    TapeMachine& tape;
    std::array<float, TapeMachine::numTracks> shownDb {};
    std::array<bool, TapeMachine::numTracks> shownArmed {}, shownLed {};
    int frame = 0;
    bool wasHidden = true;

    juce::Rectangle<float> columnBounds (int track) const;   // design coordinates
    juce::Rectangle<float> switchBounds (int track) const;
    int switchAt (juce::Point<float> local) const;            // -1 = none
    /** Design coordinates to the component: scaled to fit, centred. */
    juce::AffineTransform toScreen() const noexcept
    {
        const float s = juce::jmin ((float) getWidth() / (float) designW, (float) getHeight() / (float) designH);
        return juce::AffineTransform::scale (s).translated (((float) getWidth() - s * designW) * 0.5f, ((float) getHeight() - s * designH) * 0.5f);
    }

    void timerCallback() override;
    void paintColumn (juce::Graphics&, int track) const;
};

/** The Meter bridge window (top bar button). A normal window; closing it only hides it. */
class MeterBridgeWindow : public juce::DocumentWindow
{
public:
    MeterBridgeWindow (TapeMachine&, juce::PropertiesFile& settings, std::function<void()> saveSettings);

    void show();
    void closeButtonPressed() override;

private:
    juce::PropertiesFile& settings;
    std::function<void()> saveSettings;
    juce::ComponentBoundsConstrainer constrainer;
};
