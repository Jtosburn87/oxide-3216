#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "ConsoleLookAndFeel.h"
#include "Desk.h"
#include "PatchBay.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "InsertsUI.h"

/** Top bar (name plate, audio device, zoom, Fit, status) above the zoomable desk. */
class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent (AudioEngine&, PluginManager&, std::function<void()> saveSettings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    std::function<void()> saveSettings;
    PluginManager& pluginManager;
    PluginWindows pluginWindows;
    PluginContext pluginContext;

    juce::TextButton audioButton { "Audio device" }, toneButton { "Test tone" }, fitButton { "Fit" }, patchButton { "Patch bay" }, pluginsButton { "Plugins" };
    juce::Slider zoomSlider;
    juce::Label zoomLabel, statusLabel;

    Desk desk;
    juce::Component deskHolder;   // sized to the zoomed desk, so the viewport scrolls correctly
    juce::Viewport viewport;
    PatchBayPanel patchBay;
    bool fittedOnce = false;

    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    juce::Component::SafePointer<juce::DialogWindow> pluginListWindow;
    void openPluginList();
    void forEachChain (const std::function<void (InsertChain&)>&);

    juce::String deviceText;
    std::uint64_t lastCallbackCount = 0;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openAudioSettings();
    void refreshDeviceText();
    void applyZoom();
    void fitToWindow();
};
