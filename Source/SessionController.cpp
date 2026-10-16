#include "SessionController.h"
#include "PluginManager.h"

namespace
{
    constexpr int settleSeconds = 5;   // console moves are written at most this long after they start

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
        ExportJob (AudioEngine& e, juce::File f, juce::int64 startSample, juce::int64 endSample, double tail)
            : juce::ThreadWithProgressWindow ("Exporting " + f.getFileName(), true, true), engine (e), file (std::move (f)),
              start (startSample), end (endSample), tailSeconds (tail)
        {
            setStatusMessage ("Rendering the main mix to a 24-bit WAV...");
        }

        void run() override
        {
            result = Session::exportMix (engine, file, start, end, tailSeconds, [this] (double p)
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
        juce::int64 start, end;
        double tailSeconds;
        Session::Result result;
    };

    juce::String formatTime (juce::int64 samples, double sampleRate)
    {
        const double s = (double) samples / juce::jmax (1.0, sampleRate);
        const int minutes = (int) (s / 60.0);
        return juce::String (minutes) + ":" + juce::String (s - minutes * 60.0, 1).paddedLeft ('0', 4);
    }
}

//==============================================================================
SessionController::SessionController (AudioEngine& e, PluginManager& p, juce::PropertiesFile& s)
    : engine (e), plugins (p), settings (s), tapeFiles (e.tape)
{
    savedPrint = lastPrint = fingerprint();
    startTimer (1000);
}

SessionController::~SessionController()
{
    stopTimer();
}

juce::File SessionController::sessionsFolder()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto f = docs.getChildFile ("Oxide-3216 Sessions");
    // Sessions saved while the app was called ConsoleTape stay where they are.
    if (const auto old = docs.getChildFile ("ConsoleTape Sessions"); ! f.isDirectory() && old.isDirectory())
        return old;
    f.createDirectory();
    return f;
}

juce::String SessionController::uniqueName (const juce::String& base)
{
    auto name = juce::File::createLegalFileName (base);
    for (int n = 2; sessionsFolder().getChildFile (name).exists(); ++n)
        name = juce::File::createLegalFileName (base) + " " + juce::String (n);
    return name;
}

/** Everything session.json holds (the audio itself is TapeFiles' business). */
std::uint64_t SessionController::fingerprint() const
{
    std::uint64_t h = engine.params.changeCount();
    auto& tape = engine.tape;
    for (int t = 0; t < Config::numTracks; ++t)
        h = mix (h, (std::uint64_t) (tape.trackHasAudio (t) ? 1 : 0));
    h = mix (h, (std::uint64_t) tape.getLength());
    h = mix (h, (std::uint64_t) tape.getInPoint());
    h = mix (h, (std::uint64_t) tape.getOutPoint());
    h = mix (h, (std::uint64_t) (tape.loopOn.load() ? 1 : 0) + (tape.punchOn.load() ? 2 : 0));
    for (int m = 0; m < TapeMachine::numMemories; ++m)
        h = mix (h, (std::uint64_t) tape.getMemory (m));
    h = mix (h, (std::uint64_t) engine.scenes.getRevision());
    for (int d = 0; d < AudioEngine::Patch::numDestinations; ++d)
        h = mix (h, engine.getPatch (d).hash());
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
    h = mix (h, (std::uint64_t) engine.midi.getRevision());
    h = mix (h, (std::uint64_t) engine.midi.getPluginChangeCount());
    return h;
}

juce::String SessionController::windowTitle() const
{
    return "Oxide-3216 " JUCE_APPLICATION_VERSION_STRING "  -  " + engine.projectName.toString()
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

    if (const auto error = tapeFiles.takeError(); error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Writing the tape", error);

    if (folder == juce::File())
        return;

    // The console is written a second after it stops changing, and every few seconds during a long fader ride or take.
    const auto print = fingerprint();
    if (print == savedPrint)
        changedTicks = 0;
    else if (print == lastPrint || ++changedTicks >= settleSeconds)
        saveSettingsNow (false);
    lastPrint = print;
}

bool SessionController::saveSettingsNow (bool report)
{
    if (folder == juce::File())
        return true;
    const auto r = Session::saveSettings (engine, folder);
    if (r.ok)
    {
        savedPrint = fingerprint();
        changedTicks = 0;
        settingsErrorShown = false;
    }
    else if (report || ! settingsErrorShown)   // a lasting problem (disk full) is reported once, not every second
    {
        settingsErrorShown = true;
        showProblems ("Saving", r);
    }
    return r.ok;
}

bool SessionController::writeAll (bool report)
{
    if (folder == juce::File())
        return true;
    if (const auto error = tapeFiles.flush(); error.isNotEmpty())
    {
        if (report)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Writing the tape", error);
        return false;
    }
    return saveSettingsNow (report);
}

void SessionController::showProblems (const juce::String& title, const Session::Result& r, std::function<void()> then)
{
    const auto text = ! r.ok ? r.error : r.warnings.joinIntoString ("\n\n");
    if (text.isEmpty())
    {
        if (then) then();
        return;
    }
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle (title)
                       .withMessage (text)
                       .withButton ("OK");
    juce::AlertWindow::showAsync (options, [then] (int) { if (then) then(); });
}

//==============================================================================
void SessionController::newSession()
{
    askForNew (false);
}

void SessionController::askForNew (bool firstRun)
{
    auto* w = new juce::AlertWindow (firstRun ? "Welcome to Oxide-3216" : "New session",
                                     "A session is a folder in\n" + sessionsFolder().getFullPathName()
                                       + "\n\nEverything you record and every move on the console is saved in it as you go.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", uniqueName ("Untitled song"), "Name");
    w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (firstRun)
        w->addButton ("Open a session...", 2);
    else
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, firstRun] (int button)
    {
        if (button == 2)
            return chooseSession (true);
        if (button != 1)
            return;

        const auto name = w->getTextEditorContents ("name").trim();
        const auto target = sessionsFolder().getChildFile (juce::File::createLegalFileName (name));
        if (name.isEmpty() || target.exists())
        {
            auto options = juce::MessageBoxOptions()
                               .withIconType (juce::MessageBoxIconType::WarningIcon)
                               .withTitle ("New session")
                               .withMessage (name.isEmpty() ? juce::String ("The session needs a name.")
                                                            : "There is already a session called \"" + target.getFileName() + "\". Choose another name.")
                               .withButton ("OK");
            juce::AlertWindow::showAsync (options, [this, firstRun] (int) { askForNew (firstRun); });
            return;
        }
        startNew (target, name);
    }), true);
}

void SessionController::startNew (const juce::File& target, const juce::String& name)
{
    engine.tape.stop();
    writeAll (true);   // the old session keeps everything up to now
    tapeFiles.setFolder ({});

    Session::reset (engine);
    engine.resetPatchToDefault();   // a new session starts from the default patch, interface wiring included
    engine.projectName = name;
    engine.tape.markFilesInStep();

    if (! target.createDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "New session",
                                                "Could not create the folder " + target.getFullPathName());
        folder = juce::File();
        return askForNew (true);
    }
    folder = target;
    tapeFiles.setFolder (folder);
    settings.setValue ("lastSession", folder.getFullPathName());
    saveSettingsNow (true);
}

void SessionController::openSession()
{
    chooseSession (false);
}

void SessionController::chooseSession (bool firstRun)
{
    chooser = std::make_unique<juce::FileChooser> ("Open a session (choose its session.json)", sessionsFolder(), "session.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, firstRun] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file != juce::File())
            openFolder (file.isDirectory() ? file : file.getParentDirectory());
        else if (firstRun)
            askForNew (true);   // there has to be a session to record into
    });
}

void SessionController::openFolder (const juce::File& f)
{
    engine.tape.stop();
    writeAll (true);   // the current session keeps everything up to now
    tapeFiles.setFolder ({});

    const auto r = Session::load (engine, &plugins, f);   // fails before touching the console if f is no session
    engine.tape.markFilesInStep();   // the tape now matches the files it came from
    if (r.ok)
    {
        folder = f;
        settings.setValue ("lastSession", f.getFullPathName());
    }
    tapeFiles.setFolder (folder);
    savedPrint = lastPrint = fingerprint();

    showProblems ("Opening " + f.getFileName(), r, [this]
    {
        if (folder == juce::File())
            askForNew (true);
    });
}

void SessionController::save()
{
    writeAll (true);
}

void SessionController::saveAs()
{
    if (folder == juce::File())
        return askForNew (true);

    const auto suggested = sessionsFolder().getChildFile (uniqueName (folder.getFileName() + " copy"));
    chooser = std::make_unique<juce::FileChooser> ("Save a copy of the session as (a folder with this name is created; you carry on in the copy)",
                                                   suggested, "*");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto target = fc.getResult();
        if (target == juce::File())
            return;
        if (target.getFileName() == Session::fileName)
            target = target.getParentDirectory();
        if (target.existsAsFile())
            target = target.withFileExtension ("");
        if (target == folder)
            return save();

        const bool occupied = target.existsAsFile()
                           || (target.isDirectory() && target.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0);
        if (occupied)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save as",
                                                    target.getFullPathName() + " already exists. Choose a new name for the copy.");
            return;
        }

        engine.tape.stop();
        if (! writeAll (true))
            return;

        // The copy: session.json and the track files. Bounced mixes stay with the original.
        juce::StringArray names { Session::fileName };
        for (int t = 0; t < Config::numTracks; ++t)
            names.add (Session::trackFileName (t));
        bool ok = target.createDirectory();
        for (auto& n : names)
            if (const auto src = folder.getChildFile (n); ok && src.existsAsFile())
                ok = src.copyFileTo (target.getChildFile (n));
        if (! ok)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save as",
                                                    "Could not copy the session to " + target.getFullPathName()
                                                      + ". You are still working in " + folder.getFullPathName() + ".");
            return;
        }

        folder = target;
        tapeFiles.setFolder (folder);
        settings.setValue ("lastSession", folder.getFullPathName());
    });
}

void SessionController::exportMix()
{
    if (engine.tape.getLength() <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export", "The tape is empty: record something first.");
        return;
    }

    // Exports go in "Bounced Audio" inside the session folder (or inside the sessions folder before the first save).
    const auto bounced = Session::bounceFolder (folder != juce::File() ? folder : sessionsFolder());
    auto& tape = engine.tape;
    const double sr = tape.getSampleRate();
    const bool hasRegion = tape.hasRegion();

    auto* w = new juce::AlertWindow ("Export mix", "24-bit WAV of the main mix, saved in\n" + bounced.getFullPathName(),
                                     juce::MessageBoxIconType::NoIcon);
    w->addComboBox ("range", { "Whole session",
                               hasRegion ? "In to Out (" + formatTime (tape.getInPoint(), sr) + " - " + formatTime (tape.getOutPoint(), sr) + ")"
                                         : juce::String ("In to Out (set In and Out first)") }, "Export");
    auto* range = w->getComboBoxComponent ("range");
    range->setItemEnabled (2, hasRegion);
    range->setSelectedItemIndex (hasRegion && settings.getValue ("exportRange") == "inOut" ? 1 : 0);
    w->addTextEditor ("name", juce::File::createLegalFileName (engine.projectName.toString()), "File name");
    w->addButton ("Export", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, bounced] (int button)
    {
        if (button != 1)
            return;
        const bool inOut = w->getComboBoxComponent ("range")->getSelectedItemIndex() == 1 && engine.tape.hasRegion();
        settings.setValue ("exportRange", inOut ? "inOut" : "whole");

        auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty())
            name = "Mix";
        auto file = bounced.getChildFile (name);
        if (! file.hasFileExtension ("wav"))
            file = bounced.getChildFile (name + ".wav");

        auto& t = engine.tape;
        const auto start = inOut ? t.getInPoint() : (juce::int64) 0;
        const auto end = inOut ? t.getOutPoint() : t.getLength();
        const double tail = inOut ? 0.0 : 3.0;   // a region stops at Out; the whole session lets reverbs ring out

        auto run = [this, file, start, end, tail]
        {
            engine.tape.stop();
            engine.getDeviceManager().removeAudioCallback (&engine);   // the export renders the engine itself
            (new ExportJob (engine, file, start, end, tail))->launchThread();
        };

        if (! file.existsAsFile())
            return run();

        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Export mix")
                           .withMessage (file.getFileName() + " is already in Bounced Audio. Replace it?")
                           .withButton ("Replace")
                           .withButton ("Cancel");
        juce::AlertWindow::showAsync (options, [run] (int result) { if (result == 1) run(); });
    }), true);
}

//==============================================================================
void SessionController::startUp()
{
    const juce::File last (settings.getValue ("lastSession"));
    if (last.getChildFile (Session::fileName).existsAsFile())
        openFolder (last);
    else
        askForNew (true);
}

void SessionController::shutDown()
{
    stopTimer();
    engine.tape.stop();
    writeAll (false);
}
