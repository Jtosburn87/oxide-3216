#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "MainComponent.h"
#include "AudioSettingsComponent.h"
#include "PatchBay.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "InsertsUI.h"
#include "SessionController.h"
#include "Cassette.h"

class ConsoleTapeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return isSnapshot() || isScanner() || isFolderScan(); }

    /** Developer option: ConsoleTape.exe --scan-folder <folder> <report.txt> scans like the Plugins window does
        (one child process per plugin) into a throw-away list, and writes what was found and what failed. */
    static bool isFolderScan() { return getCommandLineParameterArray().contains ("--scan-folder"); }

    /** Developer option: ConsoleTape.exe --snapshot out.png [zoom] renders the desk to an image and exits,
        without opening the audio device. Used to check the look without touching a running copy. */
    static bool isSnapshot() { return getCommandLineParameterArray().contains ("--snapshot"); }

    /** Plugin scanner child process (see PluginManager). */
    static bool isScanner() { return getCommandLineParameterArray().contains ("--scan-plugin"); }

    void initialise (const juce::String&) override
    {
        if (isSnapshot())
        {
            writeSnapshot();
            return;
        }
        if (isFolderScan())
        {
            scanFolderReport();
            quit();
            return;
        }
        if (isScanner())
        {
            setApplicationReturnValue (PluginManager::runScanner (getCommandLineParameterArray()));
            quit();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "ConsoleTape";
        options.folderName = "ConsoleTape";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);
        plugins = std::make_unique<PluginManager> (*properties.getUserSettings());

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        auto saved = properties.getUserSettings()->getXmlValue ("audio");
        engine = std::make_unique<AudioEngine>();
        engine->initialise (saved.get());
        sessions = std::make_unique<SessionController> (*engine, *plugins, *properties.getUserSettings());

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *plugins, *sessions, *properties.getUserSettings(), [this] { saveSettings(); });
        sessions->startUp();   // recover after a crash, or reopen the last session
    }

    void shutdown() override
    {
        saveSettings();
        if (sessions != nullptr)
            sessions->shutDown();
        mainWindow = nullptr;   // closes plugin windows first
        sessions = nullptr;
        engine = nullptr;       // then the plugins themselves
        plugins = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        // Ask before closing with unsaved changes.
        if (sessions == nullptr)
            quit();
        else
            sessions->confirmDiscard ([] (bool ok) { if (ok) JUCEApplication::quit(); });
    }
    void anotherInstanceStarted (const juce::String&) override {}

    void scanFolderReport()
    {
        const auto args = getCommandLineParameterArray();
        const int i = args.indexOf ("--scan-folder");
        const juce::File folder (args[i + 1]), report (args[i + 2]);

        juce::TemporaryFile tempSettings ("settings");
        juce::PropertiesFile::Options o;
        juce::PropertiesFile props (tempSettings.getFile(), o);
        PluginManager pm (props);
        juce::PluginDirectoryScanner scanner (pm.known, *pm.formats.getFormat (0), juce::FileSearchPath (folder.getFullPathName()),
                                             true, tempSettings.getFile().getSiblingFile ("scan-crash.txt"));
        const auto start = juce::Time::getMillisecondCounter();
        juce::String name;
        while (scanner.scanNextFile (true, name)) {}

        juce::String text;
        text << "Scanned in " << juce::String ((juce::Time::getMillisecondCounter() - start) / 1000.0, 1) << " s\n";
        text << "Found " << pm.known.getNumTypes() << " plugins, " << pm.effects().size() << " effects\n";
        text << "Failed " << pm.known.getBlacklistedFiles().size() << ":\n" << pm.known.getBlacklistedFiles().joinIntoString ("\n") << "\n";
        report.replaceWithText (text);
    }

    void writeSnapshot()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto args = getCommandLineParameterArray();
        const int i = args.indexOf ("--snapshot");
        const juce::File out (args[i + 1]);
        const float zoom = args.size() > i + 2 ? args[i + 2].getFloatValue() : 1.0f;

        if (args.contains ("icon"))
        {
            // The app icon: the cassette, centred on a transparent square.
            Cassette cassette;
            cassette.projectName() = "Console Tape";
            cassette.update (0.0, false, 1.0f, 0.3f, "15 ips");
            const int size = 512;
            const float scale = (float) size / (float) Cassette::width;
            const auto shot = cassette.createComponentSnapshot (cassette.getLocalBounds(), true, scale);
            juce::Image icon (juce::Image::ARGB, size, size, true);
            {
                juce::Graphics g (icon);
                g.drawImageAt (shot, 0, (size - shot.getHeight()) / 2);
            }
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (icon, stream);
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            quit();
            return;
        }

        AudioEngine e;
        {
            // "--snapshot out.png 1.0 settings" renders the audio device window instead of the desk.
            std::unique_ptr<juce::Component> view;
            std::unique_ptr<juce::TemporaryFile> tempSettings;
            std::unique_ptr<juce::PropertiesFile> tempProps;
            std::unique_ptr<PluginManager> pm;
            std::unique_ptr<PluginWindows> windows;
            std::unique_ptr<PluginContext> context;

            if (args.contains ("inserts"))
            {
                // Aux 1 return chain with two real plugins (if installed), as the FX pop-up shows it.
                tempSettings = std::make_unique<juce::TemporaryFile> ("settings");
                tempProps = std::make_unique<juce::PropertiesFile> (tempSettings->getFile(), juce::PropertiesFile::Options());
                pm = std::make_unique<PluginManager> (*tempProps);
                windows = std::make_unique<PluginWindows>();
                context.reset (new PluginContext { *pm, *windows, [] { return 48000.0; }, nullptr });
                int slot = 0;
                for (auto* name : { "CSR Plate v6.vst3", "Black 76 v6.vst3" })
                {
                    juce::OwnedArray<juce::PluginDescription> found;
                    pm->formats.getFormat (0)->findAllTypesForFile (found, "C:\\Program Files\\Common Files\\VST3\\" + juce::String (name));
                    juce::String error;
                    if (! found.isEmpty())
                        e.auxInserts[0]->setSlot (slot, pm->create (*found[0], 48000.0, Config::maxBlock, error), *found[0]);
                    slot += 2;
                }
                e.auxInserts[0]->setBypassed (2, true);
                view = std::make_unique<InsertChainEditor> (*e.auxInserts[0], *context, e.auxReturn[0]);
            }
            else if (args.contains ("patchbay"))
            {
                using P = AudioEngine::Patch;
                e.setPatch (P::returnIn + 5, std::uint64_t (1) << (P::auxOut + 0));   // reverb return
                e.setPatch (P::tapeIn + 2, std::uint64_t (1) << (P::channelOut + 7));
                e.setPatch (P::channelIn + 15, (std::uint64_t (1) << (P::subOut + 1)) | (std::uint64_t (1) << P::mainL));
                e.setPatch (P::returnIn + 12, std::uint64_t (1) << (P::tapeOut + 3));
                auto panel = std::make_unique<PatchBayPanel> (e, nullptr);
                panel->setSize (1500, 520);
                view = std::move (panel);
            }
            else if (args.contains ("settings"))
            {
                view = std::make_unique<AudioSettingsComponent> (e, nullptr);
            }
            else
                view = std::make_unique<Desk> (e);
            const auto image = view->createComponentSnapshot (view->getLocalBounds(), true, zoom > 0.0f ? zoom : 1.0f);
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AudioEngine& engine, PluginManager& pm, SessionController& sc,
                    juce::PropertiesFile& settings, std::function<void()> save)
            : DocumentWindow (name, ConsoleColours::room, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, pm, sc, settings, std::move (save)), true);
            setResizable (true, true);
            setResizeLimits (800, 480, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    void forEachChain (const std::function<void (InsertChain&)>& fn)
    {
        for (auto* c : engine->subInserts) fn (*c);
        for (auto* c : engine->auxInserts) fn (*c);
        fn (engine->masterInserts);
    }

    void saveSettings()
    {
        if (engine == nullptr)
            return;
        if (auto xml = engine->createStateXml())
            properties.getUserSettings()->setValue ("audio", xml.get());
        properties.saveIfNeeded();
    }

    ConsoleLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<PluginManager> plugins;
    std::unique_ptr<SessionController> sessions;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ConsoleTapeApplication)
