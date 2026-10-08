#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"

/** Audio device window: driver, device, sample rate and buffer size on the left;
    on the right, tabs mapping interface inputs to the 16 input channels and interface outputs to
    main, aux and sub; below them, the reported latency and the recording offset. */
class AudioSettingsComponent : public juce::Component,
                               private juce::ChangeListener
{
public:
    AudioSettingsComponent (AudioEngine&, std::function<void()> onChanged);
    ~AudioSettingsComponent() override;

    void showOutputsTab() { tabs.setCurrentTabIndex (1); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** A grid of "label: channel chooser" rows. */
    class MappingPage : public juce::Component
    {
    public:
        struct Row
        {
            juce::String label;
            std::function<int()> get;
            std::function<void (int)> set;
        };

        MappingPage (juce::String heading, std::vector<Row> rows, int columns);
        void refresh (const juce::StringArray& channelNames, const juce::BigInteger& active);
        void resized() override;

        std::function<void()> onChanged;
        juce::Component* extra = nullptr;   // optional control under the grid

    private:
        juce::Label heading;
        std::vector<Row> rows;
        const int columns;
        juce::OwnedArray<juce::Label> labels;
        juce::OwnedArray<juce::ComboBox> boxes;
    };

    AudioEngine& engine;
    std::function<void()> onChanged;

    juce::AudioDeviceSelectorComponent selector;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<MappingPage> inputsPage, outputsPage;
    juce::TextButton autoMapButton { "Map 1 to 1" };
    juce::Label latencyLabel, offsetTitle, offsetHelp;
    juce::Slider offsetSlider;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void changed();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsComponent)
};
