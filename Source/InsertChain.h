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
class InsertChain
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
    };

    std::array<Slot, numSlots> slots;
    juce::SpinLock lock;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    double sampleRate = 48000.0;
    int blockSize = Config::maxBlock;
    std::atomic<int> latency { 0 };
    std::atomic<bool> active { false };

    static void negotiateLayout (juce::AudioProcessor&);
    void sizeBuffer();
    void destroy (std::unique_ptr<juce::AudioProcessor>);
    void changed();

    JUCE_DECLARE_NON_COPYABLE (InsertChain)
};
