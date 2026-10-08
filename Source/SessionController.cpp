#include "SessionController.h"
#include "PluginManager.h"

namespace
{
    constexpr int autosaveSeconds = 120;

    std::uint64_t mix (std::uint64_t h, std::uint64_t v) noexcept
    {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }

    std::uint64_t mix (std::uint64_t h, const juce::String& s) noexcept { return mix (h, (std::uint64_t) s.hashCode64()); }

    /** Renders the mix on a worker thread while the audio device is not calling the engine. */
    class ExportJob : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (AudioEngine& e, juce::File f)
            : juce::ThreadWithProgressWindow ("Exporting " + f.getFileName(), true, true), engine (e), file (std::move (f))
        {
            setStatusMessage ("Rendering the main mix to a 24-bit WAV...");
        }

        void run() override
        {
            result = Session::exportMix (engine, file, 3.0, [this] (double p)
            {
                setProgress (p);
                return ! threadShouldExit();
            });
        }

        void threadComplete (bool) override
        {
            engine.getDeviceManager().addAudioCallback (&engine);   // live audio again
            if (result.ok)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export finished",
                                                        "The mix was saved to\n" + file.getFullPathName());
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", result.error);
            delete this;
        }

    private:
        AudioEngine& engine;
        juce::File file;
        Session::Result result;
    };
}

//==============================================================================
SessionController::SessionController (AudioEngine& e, PluginManager& p, juce::PropertiesFile& s)
    : engine (e), plugins (p), settings (s)
{
    markClean();
    startTimer (1000);
}

SessionController::~SessionController()
{
    stopTimer();
}

juce::File SessionController::sessionsFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ConsoleTape Sessions");
    f.createDirectory();
    return f;
}

std::uint64_t SessionController::fingerprint() const
{
    std::uint64_t h = engine.params.changeCount();
    auto& tape = engine.tape;
    for (int t = 0; t < Config::numTracks; ++t)
        h = mix (h, tape.getTrackRevision (t));
    h = mix (h, (std::uint64_t) tape.getLength());
    h = mix (h, (std::uint64_t) tape.getInPoint());
    h = mix (h, (std::uint64_t) tape.getOutPoint());
    h = mix (h, (std::uint64_t) (tape.loopOn.load() ? 1 : 0) + (tape.punchOn.load() ? 2 : 0));
    for (int d = 0; d < AudioEngine::Patch::numDestinations; ++d)
        h = mix (h, engine.getPatch (d));
    for (auto* s : engine.inputStrips)  h = mix (h, s->name);
    for (auto* s : engine.returnStrips) h = mix (h, s->name);
    for (auto& n : engine.subNames) h = mix (h, n);
    for (auto& n : engine.auxNames) h = mix (h, n);
    h = mix (h, engine.projectName.toString());

    const auto chain = [&h] (const InsertChain& c)
    {
        for (int i = 0; i < InsertChain::numSlots; ++i)
        {
            h = mix (h, c.getDescription (i).createIdentifierString());
            h = mix (h, (std::uint64_t) (c.isBypassed (i) ? 1 : 0));
        }
        h = mix (h, c.getPluginChangeCount());
    };
    for (auto* c : engine.subInserts) chain (*c);
    for (auto* c : engine.auxInserts) chain (*c);
    chain (engine.masterInserts);
    return h;
}

bool SessionController::isDirty() const   { return fingerprint() != cleanPrint; }
void SessionController::markClean()       { cleanPrint = autosavedPrint = fingerprint(); }

juce::String SessionController::windowTitle() const
{
    return "ConsoleTape  -  " + engine.projectName.toString() + (isDirty() ? "  *" : "")
         + (folder != juce::File() ? "   (" + folder.getFullPathName() + ")" : juce::String());
}

void SessionController::timerCallback()
{
    const auto title = windowTitle();
    if (title != lastTitle)
    {
        lastTitle = title;
        if (onTitleChanged)
            onTitleChanged();
    }

    if (++secondsSinceAutosave >= autosaveSeconds)
    {
        secondsSinceAutosave = 0;
        bool recording = false;
        for (int t = 0; t < Config::numTracks; ++t)
            recording = recording || engine.tape.isRecording (t);
        if (! recording && fingerprint() != autosavedPrint)
            autosave();
    }
}

void SessionController::autosave()
{
    const auto dir = autosaveFolder();
    if (Session::save (engine, dir, &autosaveWritten).ok)
    {
        dir.getChildFile ("origin.txt").replaceWithText (folder.getFullPathName());
        autosavedPrint = fingerprint();
    }
}

void SessionController::showProblems (const juce::String& title, const Session::Result& r)
{
    if (! r.ok)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, r.error);
    else if (! r.warnings.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, r.warnings.joinIntoString ("\n\n"));
}

//==============================================================================
void SessionController::confirmDiscard (std::function<void (bool)> proceed)
{
    if (! isDirty())
    {
        proceed (true);
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Unsaved changes")
                       .withMessage ("Save the changes to \"" + engine.projectName.toString() + "\" first?")
                       .withButton ("Save")
                       .withButton ("Don't save")
                       .withButton ("Cancel");

    juce::AlertWindow::showAsync (options, [this, proceed] (int result)
    {
        if (result == 1)      save ([proceed] (bool ok) { proceed (ok); });   // Save
        else if (result == 2) proceed (true);                                  // Don't save
        else                  proceed (false);                                 // Cancel
    });
}

void SessionController::newSession()
{
    confirmDiscard ([this] (bool ok)
    {
        if (! ok)
            return;
        Session::reset (engine);
        folder = juce::File();
        written = {};
        markClean();
    });
}

void SessionController::openSession()
{
    confirmDiscard ([this] (bool ok)
    {
        if (! ok)
            return;
        chooser = std::make_unique<juce::FileChooser> ("Open a session (choose its session.json)", sessionsFolder(), "session.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                openFolder (file.isDirectory() ? file : file.getParentDirectory());
        });
    });
}

void SessionController::openFolder (const juce::File& f)
{
    engine.tape.stop();
    written = {};
    const auto r = Session::load (engine, &plugins, f, &written);
    if (r.ok)
    {
        folder = f;
        settings.setValue ("lastSession", f.getFullPathName());
        markClean();
    }
    showProblems ("Opening " + f.getFileName(), r);
}

void SessionController::save (std::function<void (bool)> done)
{
    if (folder == juce::File())
        saveAs (std::move (done));
    else
        saveTo (folder, std::move (done));
}

void SessionController::saveAs (std::function<void (bool)> done)
{
    // The project name on the cassette label is the default session name.
    const auto suggested = sessionsFolder().getChildFile (juce::File::createLegalFileName (engine.projectName.toString()));
    chooser = std::make_unique<juce::FileChooser> ("Save session as (a folder with this name is created)", suggested, "*");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, done] (const juce::FileChooser& fc)
    {
        auto target = fc.getResult();
        if (target == juce::File())
        {
            if (done) done (false);
            return;
        }
        if (target.getFileName() == Session::fileName)
            target = target.getParentDirectory();
        if (target.existsAsFile())
            target = target.withFileExtension ("");

        // A different folder: every track must be written there.
        if (target != folder)
            written = {};
        saveTo (target, done);
    });
}

void SessionController::saveTo (const juce::File& target, std::function<void (bool)> done)
{
    const auto r = Session::save (engine, target, &written);
    if (r.ok)
    {
        folder = target;
        settings.setValue ("lastSession", target.getFullPathName());
        markClean();
    }
    showProblems ("Saving", r);
    if (done)
        done (r.ok);
}

void SessionController::exportMix()
{
    if (engine.tape.getLength() <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export", "The tape is empty: record something first.");
        return;
    }

    const auto dir = folder != juce::File() ? folder : sessionsFolder();
    const auto suggested = dir.getChildFile (juce::File::createLegalFileName (engine.projectName.toString()) + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Export the main mix (24-bit WAV)", suggested, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (! file.hasFileExtension ("wav"))
            file = file.withFileExtension ("wav");

        engine.tape.stop();
        engine.getDeviceManager().removeAudioCallback (&engine);   // the export renders the engine itself
        (new ExportJob (engine, file))->launchThread();
    });
}

//==============================================================================
void SessionController::startUp()
{
    const bool crashed = lockFile().existsAsFile();
    lockFile().create();

    const auto autosaved = autosaveFolder().getChildFile (Session::fileName);
    if (crashed && autosaved.existsAsFile())
    {
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Recover?")
                           .withMessage ("ConsoleTape did not close properly last time.\n\nRecover the auto-saved session from "
                                         + autosaved.getLastModificationTime().toString (true, true, false) + "?")
                           .withButton ("Recover")
                           .withButton ("Start fresh");
        juce::AlertWindow::showAsync (options, [this] (int result)
        {
            if (result != 1)
                return;
            const juce::File origin (autosaveFolder().getChildFile ("origin.txt").loadFileAsString().trim());
            const auto r = Session::load (engine, &plugins, autosaveFolder());
            folder = origin.isDirectory() ? origin : juce::File();
            written = {};
            cleanPrint = 0;   // recovered work counts as unsaved
            showProblems ("Recovering", r);
        });
        return;
    }

    const juce::File last (settings.getValue ("lastSession"));
    if (last.getChildFile (Session::fileName).existsAsFile())
        openFolder (last);
}

void SessionController::shutDown()
{
    lockFile().deleteFile();
}
