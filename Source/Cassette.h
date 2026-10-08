#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The tape machine drawn as a beat-up old cassette (after the prototype).
    The label is an editable hand-written project name; the hubs turn at a rate set by the tape
    speed and the tape packs move from left to right as the tape plays. Fixed size 272 x 172. */
class Cassette : public juce::Component
{
public:
    static constexpr int width = 272, height = 172;

    Cassette();

    /** The project name shown on the label (the default session and export file name). */
    juce::Value& projectName() { return name; }

    /** Called ~30 times a second. progress 0..1 = how far through the recording. */
    void update (double seconds, bool running, float secondsPerTurn, float progress, const juce::String& speedText);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    juce::Value name;
    juce::TextEditor label;
    float hubAngle = 0.0f, packProgress = 0.0f;
    juce::String speed { "15 ips" };

    void drawHub (juce::Graphics&, juce::Point<float> centre) const;
};
