#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "InsertChain.h"

/** Knows the installed VST3 plugins (and Audio Units on a Mac) and creates them.

    Scanning runs out of process: for each plugin file the app starts another copy of itself with
    --scan-plugin. If that copy crashes or hangs (60 s), only it dies; the plugin goes on the failed
    list (KnownPluginList's blacklist), which is saved with the plugin list. */
class PluginManager : private juce::ChangeListener
{
public:
    explicit PluginManager (juce::PropertiesFile& settings);
    ~PluginManager() override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::PropertiesFile& settings;

    /** Effects only (no instruments), sorted for menus. */
    juce::Array<juce::PluginDescription> effects() const;
    /** Instruments only (for the MIDI page's tracks). */
    juce::Array<juce::PluginDescription> instruments() const;

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

    /** Restores a chain saved by InsertChain::toXml. Returns the names of plugins that could not be loaded. */
    juce::StringArray restore (InsertChain&, const juce::XmlElement&, double sampleRate, int blockSize);

    juce::File deadMansPedalFile() const;

    /** Child-process entry point: Oxide-3216.exe --scan-plugin <plugin file> <result.xml>. Returns the exit code. */
    static int runScanner (const juce::StringArray& args);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
