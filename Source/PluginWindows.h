#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** The plugins' own editor windows: one per plugin, closed before the plugin is deleted.
    Each is a normal top-level window, so Windows' display scaling applies to the plugin as usual
    (JUCE passes the monitor's scale factor to VST3 editors and updates it when the window moves). */
class PluginWindows
{
public:
    ~PluginWindows() { closeAll(); }

    void open (juce::AudioProcessor&, const juce::String& title);
    void close (juce::AudioProcessor*);
    void closeAll() { windows.clear(); }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        Window (PluginWindows&, juce::AudioProcessor&, const juce::String& title);
        ~Window() override;
        void closeButtonPressed() override { owner.close (&processor); }

        PluginWindows& owner;
        juce::AudioProcessor& processor;
    };

    juce::OwnedArray<Window> windows;
};
