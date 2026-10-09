#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Config.h"

/** Four VST3 insert slots on a stereo bus.

    Plugins are created and prepared on the message thread and swapped in under a spin lock.
    The audio thread only ever *tries* the lock: if the message thread is mid-change it passes the
    audio through untouched for that one block instead of waiting.

    Mono / stereo / multichannel: each plugin is offered stereo in/out, then mono in -> stereo out,
    then mono in/out. A mono input gets (L+R)/2; a mono output feeds both sides; extra channels of a
    multichannel plugin are fed silence and their outputs ignored. */
class InsertChain : private juce::AudioProcessorListener
{
public:
    static constexpr int numSlots = 4;

    explicit InsertChain (juce::String name);
    ~InsertChain();

    const juce::String name;

    // Message thread.
    void prepare (double sampleRate, int maxBlockSize);
    void setSlot (int slot, std::unique_ptr<juce::AudioProcessor>, const juce::PluginDescription&);
    void clearSlot (int slot);
    void setBypassed (int slot, bool);
    void moveSlot (int from, int to);
    void clearAll();

    /** Stereo only (a linked aux pair): every plugin must take stereo in and give stereo out. One that cannot
        stays loaded but is left out of the signal (isStereoRefused) until the chain is no longer stereo only. */
    void setStereoOnly (bool);
    bool isStereoOnly() const noexcept { return stereoOnly; }
    bool isStereoRefused (int slot) const;

    juce::AudioProcessor* getProcessor (int slot) const;
    const juce::PluginDescription& getDescription (int slot) const;
    bool isBypassed (int slot) const;
    int numLoaded() const;

    /** Slots with plugin description, bypass and state (base64). */
    std::unique_ptr<juce::XmlElement> toXml() const;

    /** Called just before a plugin is deleted (so its editor window can close first). */
    std::function<void (juce::AudioProcessor*)> beforeDelete;
    /** Called after any change, for the UI. */
    std::function<void()> onChange;

    /** Goes up when a hosted plugin reports a parameter or state change (unsaved-changes detection). */
    std::uint32_t getPluginChangeCount() const noexcept { return pluginChanges.load(); }

    // Audio thread.
    /** Processes left/right in place. Returns true if any plugin ran. */
    bool process (float* left, float* right, int numSamples) noexcept;

    /** Total latency of the active plugins (as of the last processed block). */
    int getLatency() const noexcept { return latency.load (std::memory_order_relaxed); }
    bool isActive() const noexcept   { return active.load (std::memory_order_relaxed); }

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioProcessor> processor;
        juce::PluginDescription description;
        std::atomic<bool> bypassed { false };
        int inputs = 2, outputs = 2;   // main bus channels
        std::atomic<bool> refused { false };   // stereo only, and the plugin has no stereo in/out layout
    };

    std::array<Slot, numSlots> slots;
    juce::SpinLock lock;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    double sampleRate = 48000.0;
    int blockSize = Config::maxBlock;
    std::atomic<int> latency { 0 };
    std::atomic<bool> active { false };
    std::atomic<std::uint32_t> pluginChanges { 0 };
    bool stereoOnly = false;

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { pluginChanges.fetch_add (1); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged) pluginChanges.fetch_add (1); }

    /** Picks the plugin's channel layout; returns true if it ended up stereo in and stereo out. */
    static bool negotiateLayout (juce::AudioProcessor&, bool stereoOnly);
    void sizeBuffer();
    void destroy (std::unique_ptr<juce::AudioProcessor>);
    void changed();

    JUCE_DECLARE_NON_COPYABLE (InsertChain)
};
