#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "Session.h"
#include "TapeFiles.h"

class PluginManager;

/** The app's session handling. A session is always a folder on disk and saves itself, like real tape:
    the tape goes to the track files as it is recorded, imported or erased (TapeFiles), and the console
    (session.json) a moment after it stops changing. There is no undo and nothing to discard.

    New asks for a name and creates the folder. Save writes everything at once (it would be written
    within seconds anyway). Save As copies the session to a new folder and carries on in the copy,
    leaving the old one as it was. Export renders the mix. */
class SessionController : private juce::Timer
{
public:
    SessionController (AudioEngine&, PluginManager&, juce::PropertiesFile& settings);
    ~SessionController() override;

    void newSession();
    void openSession();
    void openFolder (const juce::File& folder);
    void save();
    void saveAs();
    void exportMix();

    /** At start-up: reopens the last session, or asks for a name for a new one. */
    void startUp();

    /** On quit: writes whatever is still pending. */
    void shutDown();

    juce::String windowTitle() const;
    std::function<void()> onTitleChanged;

    static juce::File sessionsFolder();

private:
    AudioEngine& engine;
    PluginManager& plugins;
    juce::PropertiesFile& settings;
    TapeFiles tapeFiles;

    juce::File folder;   // current session folder (empty only until the first session is named or opened)
    std::uint64_t savedPrint = 0, lastPrint = 0;
    int changedTicks = 0;
    bool settingsErrorShown = false;
    juce::String lastTitle;
    std::unique_ptr<juce::FileChooser> chooser;

    std::uint64_t fingerprint() const;
    bool saveSettingsNow (bool report);
    bool writeAll (bool report);
    void askForNew (bool firstRun);
    void startNew (const juce::File& target, const juce::String& name);
    void chooseSession (bool firstRun);
    void timerCallback() override;
    static juce::String uniqueName (const juce::String& base);
    static void showProblems (const juce::String& title, const Session::Result&, std::function<void()> then = {});
};
