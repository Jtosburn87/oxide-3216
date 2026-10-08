#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "Session.h"

class PluginManager;

/** The app's session handling: New / Open / Save / Save As / Export, unsaved-changes detection,
    asking before discarding changes, auto-save every two minutes and recovery after a crash. */
class SessionController : private juce::Timer
{
public:
    SessionController (AudioEngine&, PluginManager&, juce::PropertiesFile& settings);
    ~SessionController() override;

    void newSession();
    void openSession();
    void openFolder (const juce::File& folder);
    void save (std::function<void (bool)> done = {});
    void saveAs (std::function<void (bool)> done = {});
    void exportMix();

    bool isDirty() const;

    /** If there are unsaved changes, asks Save / Don't save / Cancel. proceed(true) means carry on. */
    void confirmDiscard (std::function<void (bool)> proceed);

    /** At start-up: offers to recover the auto-save if the last run did not close properly,
        otherwise reopens the last session. */
    void startUp();

    /** Clean exit: no recovery offer next time. */
    void shutDown();

    juce::String windowTitle() const;
    std::function<void()> onTitleChanged;

    static juce::File sessionsFolder();

private:
    AudioEngine& engine;
    PluginManager& plugins;
    juce::PropertiesFile& settings;

    juce::File folder;   // current session folder (empty = never saved)
    Session::TrackRevisions written {}, autosaveWritten {};
    std::uint64_t cleanPrint = 0, autosavedPrint = 0;
    juce::String lastTitle;
    int secondsSinceAutosave = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::File appData() const { return settings.getFile().getParentDirectory(); }
    juce::File autosaveFolder() const { return appData().getChildFile ("Autosave"); }
    juce::File lockFile() const { return appData().getChildFile ("running.lock"); }

    std::uint64_t fingerprint() const;
    void markClean();
    void saveTo (const juce::File&, std::function<void (bool)> done);
    void autosave();
    void timerCallback() override;
    static void showProblems (const juce::String& title, const Session::Result&);
};
