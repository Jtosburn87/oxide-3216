#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"

/** The stereo bus compressor in a window of its own (the SBC button above the master inserts).
    A normal window: closing it only hides it, and it opens where it was last left (settings key busCompWindow).
    The faceplate keeps its design size and is scaled to fit. */
class BusCompWindow : public juce::DocumentWindow
{
public:
    BusCompWindow (AudioEngine&, juce::PropertiesFile& settings, std::function<void()> saveSettings);

    void show();
    void closeButtonPressed() override;

private:
    class Content;
    juce::PropertiesFile& settings;
    std::function<void()> saveSettings;
    juce::ComponentBoundsConstrainer constrainer;
};
