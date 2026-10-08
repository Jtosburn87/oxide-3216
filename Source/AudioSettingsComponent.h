#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"

/** Audio device window: driver, device, sample rate and buffer size on the left;
    which interface inputs feed the 16 input channels and which outputs carry main L/R on the right. */
class AudioSettingsComponent : public juce::Component,
                               private juce::ChangeListener
{
public:
    AudioSettingsComponent (AudioEngine&, std::function<void()> onChanged);
    ~AudioSettingsComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    std::function<void()> onChanged;

    juce::AudioDeviceSelectorComponent selector;
    juce::Label inputsTitle, outputsTitle, latencyLabel;
    juce::OwnedArray<juce::Label> inputLabels;
    juce::OwnedArray<juce::ComboBox> inputBoxes;
    juce::Label mainLabels[2];
    juce::ComboBox mainBoxes[2];
    juce::TextButton autoMapButton { "Map 1 to 1" };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshChannelLists();
    void autoMap();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsComponent)
};
