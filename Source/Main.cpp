#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "MainComponent.h"
#include "AudioSettingsComponent.h"
#include "PatchBay.h"

class ConsoleTapeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return isSnapshot(); }

    /** Developer option: ConsoleTape.exe --snapshot out.png [zoom] renders the desk to an image and exits,
        without opening the audio device. Used to check the look without touching a running copy. */
    static bool isSnapshot() { return getCommandLineParameterArray().contains ("--snapshot"); }

    void initialise (const juce::String&) override
    {
        if (isSnapshot())
        {
            writeSnapshot();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "ConsoleTape";
        options.folderName = "ConsoleTape";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        auto saved = properties.getUserSettings()->getXmlValue ("audio");
        engine = std::make_unique<AudioEngine>();
        engine->initialise (saved.get());
        engine->projectName = properties.getUserSettings()->getValue ("projectName", "Untitled song");

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, [this] { saveSettings(); });
    }

    void shutdown() override
    {
        saveSettings();
        mainWindow = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

    void writeSnapshot()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto args = getCommandLineParameterArray();
        const int i = args.indexOf ("--snapshot");
        const juce::File out (args[i + 1]);
        const float zoom = args.size() > i + 2 ? args[i + 2].getFloatValue() : 1.0f;

        AudioEngine e;
        {
            // "--snapshot out.png 1.0 settings" renders the audio device window instead of the desk.
            std::unique_ptr<juce::Component> view;
            if (args.contains ("patchbay"))
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
                auto settings = std::make_unique<AudioSettingsComponent> (e, nullptr);
                if (args.contains ("outputs"))
                    settings->showOutputsTab();
                view = std::move (settings);
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
        MainWindow (const juce::String& name, AudioEngine& engine, std::function<void()> save)
            : DocumentWindow (name, ConsoleColours::room, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, std::move (save)), true);
            setResizable (true, true);
            setResizeLimits (800, 480, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    void saveSettings()
    {
        if (engine == nullptr)
            return;
        if (auto xml = engine->createStateXml())
            properties.getUserSettings()->setValue ("audio", xml.get());
        properties.getUserSettings()->setValue ("projectName", engine->projectName.toString());
        properties.saveIfNeeded();
    }

    ConsoleLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ConsoleTapeApplication)
