#pragma once

#include "Params.h"
#include "Dsp.h"
#include "Config.h"
#include <juce_events/juce_events.h>
#include <vector>

/** Multitrack tape recorder.

    Storage: each track is a table of fixed-size chunks. Chunks are allocated on the message
    thread ahead of the record position (service()), so the audio thread never allocates.
    Erased chunks are freed only after the audio thread can no longer be using them.

    Latency: a sample captured at the interface input in the same callback that plays tape
    position P was performed while the musician heard P - roundTrip. It is written there,
    so overdubs line up with what was heard. */
class TapeMachine : private juce::Timer
{
public:
    static constexpr int chunkBits = 16;
    static constexpr int chunkSize = 1 << chunkBits;   // 65536 samples
    static constexpr int numTracks = Config::numTracks;

    explicit TapeMachine (ParamSet&);
    ~TapeMachine() override;

    Param* recordLevel;                       // dB into tape, -12..+18
    std::array<Param*, numTracks> arm {};     // armed tracks monitor their input and record

    // Called before audio starts (not from the audio callback).
    void prepare (double sampleRate, int roundTripLatencySamples);

    /** Audio thread. tapeIn/tapeOut: one mono buffer per track. */
    void process (const float* const* tapeIn, float* const* tapeOut, int numSamples) noexcept;

    // Transport, message thread.
    void play();
    void stop();
    void locate (juce::int64 samplePosition);
    void setRecordEnabled (bool);
    void eraseAll();

    bool isPlaying() const noexcept         { return playing.load(); }
    bool isRecordEnabled() const noexcept   { return recordEnabled.load(); }
    bool isRecording (int track) const noexcept;
    juce::int64 getPosition() const noexcept { return position.load(); }
    juce::int64 getLength() const noexcept   { return length.load(); }
    double getSampleRate() const noexcept    { return sampleRate; }
    int getLatency() const noexcept          { return latency.load(); }
    void setLatency (int samples) noexcept   { latency.store (juce::jmax (0, samples)); }
    juce::int64 getDroppedSamples() const noexcept { return dropped.load(); }

    /** Message thread: allocate chunks ahead of armed tracks, free erased chunks.
        Runs on a timer; tests call it directly. */
    void service();

    /** Read recorded audio (message thread, e.g. for saving). */
    void readTrack (int track, juce::int64 start, float* dest, int numSamples) const noexcept;

private:
    double sampleRate = 48000.0;
    std::atomic<int> latency { 0 };
    const size_t maxChunks;

    std::array<std::vector<std::atomic<float*>>, numTracks> chunks;

    std::atomic<bool> playing { false }, recordEnabled { false };
    std::atomic<juce::int64> position { 0 }, length { 0 }, locateRequest { -1 }, dropped { 0 };
    std::atomic<std::uint64_t> blocksProcessed { 0 };
    juce::int64 pos = 0;   // audio thread's own copy of the position

    Dsp::Smoothed levelGain;
    std::array<float, Config::maxBlock> scratch {};

    struct Retired { float* chunk; std::uint64_t stamp; };
    std::vector<Retired> retired;
    std::uint64_t lastSeenBlocks = 0;
    int idleTicks = 0;

    void timerCallback() override { service(); }
    void ensureChunks (int track, juce::int64 from, juce::int64 to);
    void write (int track, juce::int64 start, const float* src, int n) noexcept;

    JUCE_DECLARE_NON_COPYABLE (TapeMachine)
};
