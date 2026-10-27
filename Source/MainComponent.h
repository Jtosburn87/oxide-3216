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
#include "BusCompWindow.h"
#include "MidiPage.h"
#include "TapeMachineWindow.h"
#include "SessionController.h"
#include "ImportAudioDialog.h"
#include "SectionWindow.h"
#include "ChannelStripWindow.h"
#include "WindowButtons.h"
#include "MidiControlUI.h"
#include "CreditsWindow.h"
#include "Remote.h"

/** Top bar (name plate, zoom, status) above the zoomable desk. The app buttons live on the desk. */
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      private juce::Timer,
                      private juce::ChangeListener,
                      private juce::FocusChangeListener
{
public:
    MainComponent (AudioEngine&, PluginManager&, SessionController&, juce::PropertiesFile& settings, std::function<void()> saveSettings);

    bool keyPressed (const juce::KeyPress&) override;
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Set by the main window: full screen on / off (F11, Options menu), and whether it is on. */
    std::function<void()> onToggleFullScreen;
    std::function<bool()> isFullScreenMode;

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
        tapeMachineCmd, rackCmd, patchBayCmd, talkbackCmd, disarmAllCmd, fullScreenCmd,
        jogScrubCmd, storeCmd, setInCmd, setOutCmd, loopCmd, goInCmd, goOutCmd, punchCmd,   // the Jog / shuttle box
        jogBackCmd, jogForwardCmd, shuttleBackCmd, shuttleForwardCmd,
        midiPageCmd, exportTapeCmd, importTempoMapCmd, clearTempoMapCmd, shuttleWindowCmd,
        armTrack1 = 100,  // armTrack1 + t arms / disarms tape track t + 1
        memory1 = 200     // memory1 + m: locate memory m + 1 (or stores there after Store)
    };
    juce::ApplicationCommandManager commands;
    juce::Component::SafePointer<juce::DialogWindow> keysWindow;
    void openKeyMappings();

    /** A key with a command on it does that command and nothing else, whatever has the keyboard
        (a knob nudging, a button, the console scrolling). It rides on the focused component, because
        key listeners hear a key before the component itself does. Text fields, plug-in editors and
        dialogs (the shortcut editor, menus) keep their own keys. */
    struct AssignedKeysFirst : juce::KeyListener
    {
        explicit AssignedKeysFirst (MainComponent& m) : owner (m) {}
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        MainComponent& owner;
    };
    AssignedKeysFirst assignedKeysFirst { *this };
    juce::Component::SafePointer<juce::Component> assignedKeysHolder;
    void globalFocusChanged (juce::Component*) override;
    AudioEngine& engine;
    std::function<void()> saveSettings;
    juce::PropertiesFile& settings;
    PluginManager& pluginManager;
    SessionController& sessions;
    PluginWindows pluginWindows;
    PluginContext pluginContext;

    juce::TextButton audioButton { "Audio device" }, patchButton { "Patch bay" }, rackButton { "Rack" }, sessionButton { "Session" }, sectionsButton { "Options" };
    ViewSwitch viewSwitch;   // Record | Mix | Full
    void showView (int view);   // switches the desk, lights its button and remembers it
    juce::TextButton tapeMachineButton { "Tape Machine" };
    std::unique_ptr<TapeMachineWindow> tapeWindow;   // made on first use, then only hidden
    // MIDI control: the Jog / shuttle window's controls and the keyboard shortcuts (both saved in the settings).
    ShuttleMidi shuttleMidi;
    MidiShortcuts midiShortcuts;
    std::unique_ptr<ShuttleWindow> shuttleWindow;   // right-click the Jog / shuttle box; made on first use, then only hidden
    void openShuttleWindow();
    /** Rew / FF from the keyboard: 40x while a key on that command stays down (like holding the transport key). */
    void holdWindWhileKeyDown (juce::CommandID);
    juce::CommandID heldWindCommand = 0;
    juce::TimedCallback windKeyWatch { [this] { checkWindKey(); } };
    void checkWindKey();
    void setUpMidiControl();
    juce::TextButton midiButton { "MIDI" };      // the MIDI page in place of the console (lit while it shows)
    std::unique_ptr<MidiPage> midiPage;
    juce::TextButton creditsButton { "Credits" };   // who made it, and how to get in touch
    juce::TextButton remoteButton { "Remote" };     // the Wi-Fi remote window (QR code for a phone or iPad)
    void showMidiPage (bool);
    /** The console's cassette moves (Options setting) unless the Tape machine window is showing it. */
    void applyCassetteAnimation();
    juce::Slider zoomSlider;
    juce::Label zoomLabel, statusLabel;
    std::unique_ptr<juce::Drawable> logo;   // Resources/oxide-3216-logo.svg, in the top bar

    Desk desk;
    juce::Component deskHolder;   // sized to the zoomed desk, so the viewport scrolls correctly
    juce::Viewport viewport;
    std::unique_ptr<TouchPanZoom> touch;   // pinch to zoom (touch never scrolls)
    PatchBayPanel patchBay;
    std::unique_ptr<Remote> remote;   // the Wi-Fi remote (plays the desk's Jog / shuttle box, so it comes after the desk)
    WindowButtons windowButtons { [this] { return isFullScreenMode != nullptr && isFullScreenMode(); },
                                  [this] { if (onToggleFullScreen) onToggleFullScreen(); } };   // _ / square / X, top bar right

    // Sections of the console that are out in windows of their own.
    std::array<std::unique_ptr<SectionWindow>, Desk::numSections> sectionWindows;

    // Channel windows (double-click a knob on a strip), keyed by bank * 100 + strip index. A window's key
    // changes when its arrows step to another channel, so callbacks find it by pointer.
    std::map<int, std::unique_ptr<ChannelStripWindow>> stripWindows;
    void openStripWindow (Desk::Section bank, int strip);
    void closeStripWindow (int key);
    void stepStripWindow (ChannelStripWindow*, int delta);
    int stripWindowKey (const ChannelStripWindow*) const;   // -1 if gone
    juce::String stripWindowTitle (Desk::Section bank, int strip) const;
    void showSectionsMenu();
    void takeOut (Desk::Section);
    void putBack (Desk::Section);
    void restoreSections();
    void saveSections();

    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    juce::Component::SafePointer<juce::DialogWindow> pluginListWindow;
    void openPluginList();
    std::unique_ptr<RackWindow> rackWindow;   // made on first use, then only hidden
    std::unique_ptr<BusCompWindow> busCompWindow;   // the SBC button; made on first use, then only hidden
    void showSessionMenu();
    void parentHierarchyChanged() override;
    void forEachChain (const std::function<void (InsertChain&)>&);

    juce::String deviceText;
    std::uint64_t lastCallbackCount = 0;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openAudioSettings();
    void refreshDeviceText();
    /** The desk's scale across and down: the same from the Zoom slider, stretched apart by fitToWindow. */
    double scaleX = 1.0, scaleY = 1.0;
    void applyZoom();
    /** Both strip banks are off the console (in windows, or out of the current view). */
    bool centreSectionAlone() const;
    /** Stretch the console to fill the window (window resized, view changed, section docked or undocked). */
    void fitToWindow();
};
