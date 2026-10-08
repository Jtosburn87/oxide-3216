#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "MainComponent.h"

class ConsoleTapeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
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
        properties.saveIfNeeded();
    }

    ConsoleLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ConsoleTapeApplication)
