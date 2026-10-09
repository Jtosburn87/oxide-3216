#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/** The tape machine drawn as a real compact cassette: smoked shell, paper label with a window, white
    hubs inside brown tape packs, and the tape path round the guide rollers and across the head openings.
    The label carries an editable hand-written project name.

    The packs follow tape physics: a pack's area is proportional to the tape on it, and with the tape
    moving at a steady speed a small pack turns faster than a big one. Fixed size 272 x 172. */
class Cassette : public juce::Component
{
public:
    static constexpr int width = 272, height = 172;

    Cassette();

    /** The project name shown on the label (the default session and export file name). */
    juce::Value& projectName() { return name; }

    /** Called ~30 times a second. hubTurnsPerSecond: how fast an empty hub would turn at the current tape
        motion (negative = rewinding, 0 = stopped); progress 0..1 = how far through the recording. */
    void update (double seconds, float hubTurnsPerSecond, float progress, const juce::String& speedText);

    /** Off: the reels stand still and the packs only move in coarse steps (saves CPU). */
    void setAnimated (bool);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    juce::Value name;
    juce::TextEditor label;
    std::array<float, 2> reelAngle { 0.0f, 0.7f }, blur { 0.0f, 0.0f };
    float packProgress = 0.0f;
    bool animated = true;
    juce::String speed { "15 ips" };

    float packRadius (int reel) const;
    void drawReel (juce::Graphics&, int reel) const;
};
