#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "ConsoleLookAndFeel.h"
#include "Desk.h"
#include "PatchBay.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "InsertsUI.h"
#include "SessionController.h"
#include "ImportAudioDialog.h"
#include "SectionWindow.h"

/** Top bar (name plate, zoom, status) above the zoomable desk. The app buttons live on the desk. */
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent (AudioEngine&, PluginManager&, SessionController&, juce::PropertiesFile& settings, std::function<void()> saveSettings);

    bool keyPressed (const juce::KeyPress&) override;
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Commands, each with keys the user can change (Options > Keyboard shortcuts).
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

private:
    enum CommandIDs
    {
        playStop = 1, play, stop, record, rewind, fastForward, returnToZero,
        newSession, openSession, saveSession, saveSessionAs, exportMix, importAudio
    };
    juce::ApplicationCommandManager commands;
    juce::Component::SafePointer<juce::DialogWindow> keysWindow;
    void openKeyMappings();
    AudioEngine& engine;
    std::function<void()> saveSettings;
    juce::PropertiesFile& settings;
    PluginManager& pluginManager;
    SessionController& sessions;
    PluginWindows pluginWindows;
    PluginContext pluginContext;

    juce::TextButton audioButton { "Audio device" }, vFitButton { "V Fit" }, hFitButton { "H Fit" }, patchButton { "Patch bay" }, pluginsButton { "Plugins" }, sessionButton { "Session" }, sectionsButton { "Options" };
    juce::Slider zoomSlider;
    juce::Label zoomLabel, statusLabel;

    Desk desk;
    juce::Component deskHolder;   // sized to the zoomed desk, so the viewport scrolls correctly
    juce::Viewport viewport;
    std::unique_ptr<TouchPanZoom> touch;   // one-finger pan, pinch to zoom
    PatchBayPanel patchBay;
    bool fittedOnce = false;

    // Sections of the console that are out in windows of their own.
    std::array<std::unique_ptr<SectionWindow>, Desk::numSections> sectionWindows;
    void showSectionsMenu();
    void takeOut (Desk::Section);
    void putBack (Desk::Section);
    void restoreSections();
    void saveSections();

    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    juce::Component::SafePointer<juce::DialogWindow> pluginListWindow;
    void openPluginList();
    void showSessionMenu();
    void parentHierarchyChanged() override;
    void forEachChain (const std::function<void (InsertChain&)>&);

    juce::String deviceText;
    std::uint64_t lastCallbackCount = 0;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openAudioSettings();
    void refreshDeviceText();
    void applyZoom();
    /** Zoom so the console fits the window: its height, its width, or both. */
    void fitToWindow (bool height = true, bool width = true);
};
