#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TapeMachine.h"
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

/** The Tape Machine window's ARA slots: one ARA plugin (Melodyne, Pitcher...) on each tape track.

    A slot is its own small ARA document: the track's whole recording is the audio source, one playback region lays
    it on the tape at the same place, so the plugin reads the tape in its own time and is free to edit it. On playback
    it replaces what comes off the tape, before the tape's own character (hiss, wow and flutter, the EQ): TapeMachine
    asks render() for each block. A track taking its input, a bypassed slot, and winding or scrubbing play the plain tape.

    The plugin's edits (its ARA archive) are saved beside the session as "ARA Track 01.bin"; recording over a track
    tells the plugin the audio has changed. Rendering stops only for the moment that takes.

    Message thread, except render(). In a build without the ARA SDK (isAvailable() false) no slot can be filled. */
class AraSlots : public TapeMachine::PlaybackSource,
                 private juce::Timer
{
public:
    explicit AraSlots (TapeMachine&);
    ~AraSlots() override;

    static constexpr int numSlots = TapeMachine::numTracks;
    static constexpr const char* filePrefix = "ARA Track ";

    static bool isAvailable() noexcept;

    /** Called with the device's rate; plugins are prepared again only if it has changed. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Makes a plugin: as PluginManager::create. */
    using CreatePlugin = std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, juce::String& error)>;

    /** Puts a plugin on a track (whatever was there goes). state / archive: what a session saved (may be empty).
        done gets an error, or an empty string; with a VST3 it is called before this returns. */
    void load (int track, const CreatePlugin&, const juce::PluginDescription&,
               const juce::MemoryBlock& state, const juce::MemoryBlock& archive,
               std::function<void (const juce::String& error)> done);

    void clear (int track);
    void clearAll();

    juce::AudioProcessor* getProcessor (int track) const;
    juce::PluginDescription getDescription (int track) const;
    bool isLoaded (int track) const { return getProcessor (track) != nullptr; }
    bool isBypassed (int track) const noexcept { return slots[(size_t) track].bypassed.load(); }
    void setBypassed (int track, bool);

    /** Offline export: tells the plugins to wait for their analysis instead of keeping up. */
    void setNonRealtime (bool);

    /** Called just before a plugin is deleted (so its editor window can close first). */
    std::function<void (juce::AudioProcessor*)> beforeDelete;

    /** Goes up when a slot changes or a plugin reports an edit (the UI, the session fingerprint). */
    std::uint32_t getRevision() const noexcept { return revision.load(); }

    /** The slots for session.json, writing the archives into the session folder. Only ones that changed, unless the
        folder is a new one. Leftover files of emptied slots are removed. */
    juce::var save (const juce::File& folder);
    /** Reads back what save() made. Returns the names of missing plugins. */
    juce::StringArray restore (const juce::var&, const juce::File& folder, const CreatePlugin&);

    /** Hands the plugin anything it has to pass on, and tells it about changes to the tape. Runs on a timer; tests call
        it directly. */
    void tick();

    /** Audio thread (TapeMachine::PlaybackSource). */
    bool render (int track, juce::int64 position, float* dest, int numSamples) noexcept override;

private:
    struct Host;
    struct Slot
    {
        juce::SpinLock lock;
        std::unique_ptr<Host> host;
        std::atomic<bool> bypassed { false }, dirty { false };
    };

    TapeMachine& tape;
    std::array<Slot, numSlots> slots;
    double rate = 0.0;
    int blockSize = 512;
    std::atomic<std::uint32_t> revision { 0 };
    juce::File savedTo;
    std::map<juce::String, std::weak_ptr<juce::AudioPluginInstance>> keepers;   // see Host::keeper
    /** Removed plugins whose documents are still to go: see Host::closeDocument. tick does that, and closingCount()
        says how many are on their way out. */
    struct Retiring { std::unique_ptr<Host> host; double since; };
    std::vector<Retiring> retiring;
    static constexpr double documentDelayMs = 300.0, quitWaitMs = 5000.0;
    void finishRetiring();
    std::shared_ptr<std::atomic<int>> closingCountPtr = std::make_shared<std::atomic<int>> (0);
    std::shared_ptr<std::atomic<int>> closingCount() const { return closingCountPtr; }
    std::shared_ptr<std::atomic<bool>> alive;

    void finishLoad (int track, std::unique_ptr<Host>, const juce::MemoryBlock& state, const juce::MemoryBlock& archive,
                     const std::function<void (const juce::String&)>& done);
    void updateModel (int track);
    void prepareAll();
    void changed (int track) noexcept;
    void timerCallback() override { tick(); }
    friend struct AraSlotsChange;

    JUCE_DECLARE_NON_COPYABLE (AraSlots)
};
