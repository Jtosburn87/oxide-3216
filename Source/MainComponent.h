#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "ConsoleLookAndFeel.h"

/** Vertical LED peak meter, -60 to 0 dBFS, with a short peak hold. */
class LedMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    float shownDb = -100.0f, holdDb = -100.0f;
    int holdFrames = 0;
};

class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent (AudioEngine&, std::function<void()> saveSettings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    std::function<void()> saveSettings;

    juce::TextButton audioButton { "Audio device" }, toneButton { "Test tone" };
    juce::Label statusLabel;

    std::array<LedMeter, AudioEngine::numConsoleInputs> inputMeters;
    std::array<juce::Label, AudioEngine::numConsoleInputs> inputNames, inputSources;
    std::array<LedMeter, 2> mainMeters;
    juce::Slider mainFader;
    juce::Label mainLabel;

    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

    juce::String deviceText;
    std::uint64_t lastCallbackCount = 0;
    int timerTicks = 0;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openAudioSettings();
    void refreshLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
