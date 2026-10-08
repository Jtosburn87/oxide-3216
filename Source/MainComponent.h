#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "ConsoleLookAndFeel.h"
#include "Desk.h"

/** Top bar (name plate, audio device, zoom, Fit, status) above the zoomable desk. */
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

    juce::TextButton audioButton { "Audio device" }, toneButton { "Test tone" }, fitButton { "Fit" };
    juce::Slider zoomSlider;
    juce::Label zoomLabel, statusLabel;

    Desk desk;
    juce::Component deskHolder;   // sized to the zoomed desk, so the viewport scrolls correctly
    juce::Viewport viewport;
    bool fittedOnce = false;

    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

    juce::String deviceText;
    std::uint64_t lastCallbackCount = 0;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openAudioSettings();
    void refreshDeviceText();
    void applyZoom();
    void fitToWindow();
};
