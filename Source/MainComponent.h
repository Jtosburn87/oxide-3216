#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "ConsoleLookAndFeel.h"
#include "Desk.h"
#include "PatchBay.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "InsertsUI.h"
#include "RackWindow.h"
#include "TapeMachineWindow.h"
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
        newSession, openSession, saveSession, saveSessionAs, exportMix, importAudio,
        recordViewCmd, mixViewCmd, fullViewCmd, nextViewCmd,
        tapeMachineCmd, rackCmd, patchBayCmd, talkbackCmd, disarmAllCmd,
        armTrack1 = 100   // armTrack1 + t arms / disarms tape track t + 1
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

    juce::TextButton audioButton { "Audio device" }, patchButton { "Patch bay" }, pluginsButton { "Plugins" }, rackButton { "Rack" }, sessionButton { "Session" }, sectionsButton { "Options" };
    ViewSwitch viewSwitch;   // Record | Mix | Full
    void showView (int view);   // switches the desk, lights its button and remembers it
    juce::TextButton tapeMachineButton { "Tape Machine" };
    std::unique_ptr<TapeMachineWindow> tapeWindow;   // made on first use, then only hidden
    /** The console's cassette moves (Options setting) unless the Tape machine window is showing it. */
    void applyCassetteAnimation();
    juce::Slider zoomSlider;
    juce::Label zoomLabel, statusLabel;

    Desk desk;
    juce::Component deskHolder;   // sized to the zoomed desk, so the viewport scrolls correctly
    juce::Viewport viewport;
    std::unique_ptr<TouchPanZoom> touch;   // one-finger pan, pinch to zoom
    PatchBayPanel patchBay;

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
    std::unique_ptr<RackWindow> rackWindow;   // made on first use, then only hidden
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
    /** Zoom so the whole console fits the window (window resized, view changed, section docked or undocked). */
    void fitToWindow();
};
