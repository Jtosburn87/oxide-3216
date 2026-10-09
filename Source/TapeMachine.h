#pragma once

#include "Params.h"
#include "Dsp.h"
#include "Config.h"
#include <juce_events/juce_events.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

/** Multitrack tape recorder with speed-dependent tape character.

    Per track:  tape in -> record level -> saturation (4x oversampled) -> [recorded]
                armed: monitor that signal; unarmed: play back (+ hiss while rolling)
                -> head bump -> 8 kHz shelf -> low-pass -> tape out

    Storage: each track is a table of fixed-size chunks. Chunks are allocated on the message
    thread ahead of the record position (service()), so the audio thread never allocates.
    Erased chunks are freed only after the audio thread can no longer be using them.

    Latency: a sample captured at the interface input in the same callback that plays tape
    position P was performed while the musician heard P - roundTrip. It is written there
    (plus the oversampler's own fixed delay), so overdubs line up with what was heard. */
class TapeMachine : private juce::Timer
{
public:
    static constexpr int chunkBits = 16;
    static constexpr int chunkSize = 1 << chunkBits;   // 65536 samples
    static constexpr int numTracks = Config::numTracks;

    /** Tape character per speed. Starting values from the prototype; tune by ear. */
    struct Speed
    {
        const char* name;
        float saturation;      // tanh drive (higher = more saturation)
        float bumpHz, bumpDb;  // head bump (bell, Q 1)
        float shelfDb;         // high shelf at 8 kHz
        float lowPassHz;
        float hissDb;          // dBFS
        float secondsPerTurn;  // how fast the cassette hubs spin
        float wowPercent, flutterPercent;   // peak speed deviation (wow ~0.5 Hz, flutter ~6-9 Hz)
    };

    static constexpr Speed speeds[3] = {
        { "7.5", 1.80f, 90.0f, 4.0f, -3.0f, 12000.0f, -50.0f, 2.4f, 0.060f, 0.030f },
        { "15",  1.30f, 60.0f, 2.5f, -1.0f, 18000.0f, -57.0f, 1.2f, 0.030f, 0.015f },
        { "30",  0.95f, 38.0f, 1.5f,  0.0f, 21000.0f, -64.0f, 0.6f, 0.015f, 0.008f },
    };

    explicit TapeMachine (ParamSet&);
    ~TapeMachine() override;

    Param* recordLevel;                       // dB into tape, -12..+18
    Param* speed;                             // 0, 1, 2 = 7.5, 15, 30 ips
    std::array<Param*, numTracks> arm {};     // armed tracks monitor their input and record
    Param* wowFlutter;                        // W&F on/off

    const Speed& currentSpeed() const noexcept { return speeds[juce::jlimit (0, 2, juce::roundToInt (speed->get()))]; }

    // Called before audio starts (not from the audio callback).
    void prepare (double sampleRate, int roundTripLatencySamples);

    /** Audio thread. tapeIn/tapeOut: one mono buffer per track. */
    void process (const float* const* tapeIn, float* const* tapeOut, int numSamples) noexcept;

    // Transport, message thread.
    void play();
    void stop();
    void locate (juce::int64 samplePosition);

    /** Rewind and fast forward wind like a real transport: they speed up over windRampSeconds to windMaxSpeed
        times play speed, and brake (windBrakeSeconds from full speed) to stop, or to arrive at the start or at
        the end of the recording. Stop brakes; Play and locate take over at once. Message thread. */
    void rewind();
    void fastForward();
    int  getWindDirection() const noexcept { return windRequest.load(); }   // -1 rewind, +1 fast forward, 0 none
    bool isWinding() const noexcept { return windRequest.load() != 0 || windSpeed.load() != 0.0f; }
    float getWindSpeed() const noexcept { return windSpeed.load(); }        // signed, times play speed
    static constexpr double windMaxSpeed = 20.0, windRampSeconds = 3.0, windBrakeSeconds = 0.6;
    void setRecordEnabled (bool);
    void eraseAll();

    // Locators: one In/Out region used by both loop and auto-punch (message thread).
    void setInPoint (juce::int64 p)  { inPoint.store (juce::jmax ((juce::int64) 0, p)); }
    void setOutPoint (juce::int64 p) { outPoint.store (juce::jmax ((juce::int64) 0, p)); }
    juce::int64 getInPoint() const noexcept  { return inPoint.load(); }
    juce::int64 getOutPoint() const noexcept { return outPoint.load(); }
    std::atomic<bool> loopOn { false }, punchOn { false };

    /** Crossfade at each end of a punch, so punching in and out is smooth. */
    static constexpr double punchFadeMs = 10.0;
    bool hasRegion() const noexcept { return outPoint.load() > inPoint.load(); }

    /** Replaces a track with new audio (session loading). Message thread, transport stopped. */
    void loadTrack (int track, const float* data, juce::int64 numSamples);
    void setLength (juce::int64 samples) { length.store (juce::jmax ((juce::int64) 0, samples)); }

    /** Goes up whenever a track is recorded on, so sessions only rewrite tracks that changed. */
    std::uint32_t getTrackRevision (int track) const noexcept { return trackRevision[(size_t) track].load(); }

    bool isPlaying() const noexcept         { return playing.load(); }
    bool isRecordEnabled() const noexcept   { return recordEnabled.load(); }
    bool isRecording (int track) const noexcept;
    juce::int64 getPosition() const noexcept { return position.load(); }
    juce::int64 getLength() const noexcept   { return length.load(); }
    juce::int64 getCapacity() const noexcept { return (juce::int64) maxChunks * chunkSize; }   // samples per track
    double getSampleRate() const noexcept    { return sampleRate; }
    int getLatency() const noexcept          { return latency.load(); }
    void setLatency (int samples) noexcept   { latency.store (juce::jmax (0, samples)); }
    juce::int64 getDroppedSamples() const noexcept { return dropped.load(); }

    /** Export: every track plays back (armed ones too) and nothing records. */
    std::atomic<bool> exporting { false };

    /** Does this track hold any recorded audio? */
    bool trackHasAudio (int track) const noexcept;

    /** Writes audio straight onto a track (session loading). Message thread, transport stopped. */
    void writeTrack (int track, juce::int64 start, const float* data, int numSamples);

    /** Tape character on/off. Always on in the app; the tests switch it off to check storage bit-for-bit. */
    std::atomic<bool> characterOn { true };

    /** Message thread: allocate chunks ahead of armed tracks, free erased chunks.
        Runs on a timer; tests call it directly. */
    void service();

    /** Read recorded audio (message thread, e.g. for saving). */
    void readTrack (int track, juce::int64 start, float* dest, int numSamples) const noexcept;

private:
    double sampleRate = 48000.0;
    std::atomic<int> latency { 0 };
    int oversamplerLatency = 0;
    const size_t maxChunks;

    std::array<std::vector<std::atomic<float*>>, numTracks> chunks;

    std::atomic<bool> playing { false }, recordEnabled { false };
    std::atomic<juce::int64> position { 0 }, length { 0 }, locateRequest { -1 }, dropped { 0 };
    std::atomic<std::uint64_t> blocksProcessed { 0 };
    juce::int64 pos = 0;   // audio thread's own copy of the position
    std::atomic<int> windRequest { 0 };
    std::atomic<float> windSpeed { 0.0f };
    std::atomic<bool> windReset { false };
    double windVel = 0.0, windPos = 0.0;   // audio thread: signed speed (times play speed), fractional position
    void wind (int numSamples) noexcept;
    std::atomic<juce::int64> inPoint { 0 }, outPoint { 0 };
    std::array<std::atomic<std::uint32_t>, numTracks> trackRevision {};

    // Tape character state.
    Dsp::Smoothed levelGain;
    std::array<float, Config::maxBlock> gainScratch {};
    std::array<std::array<float, Config::maxBlock>, numTracks> recordBuf {};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    struct TrackFilters { Dsp::Biquad bump, shelf, lowPass; };
    std::array<TrackFilters, numTracks> filters {};
    std::array<Dsp::Noise, numTracks> hiss {};

    // Wow and flutter: one modulated delay per track on the playback path, driven by one shared transport wobble.
    static constexpr int wowLineSize = 512;
    std::array<std::array<float, wowLineSize>, numTracks> wowLine {};
    int wowWrite = 0;
    double wowPhase = 0.0, flutterPhase1 = 0.0, flutterPhase2 = 0.0;
    std::array<float, Config::maxBlock> wowDelay {};
    std::array<float, Config::maxBlock> playScratch {};
    std::array<float, Config::maxBlock> fadeScratch {};
    float punchGain (juce::int64 position, juce::int64 punchIn, juce::int64 punchOut) const noexcept;
    int wowBase (const Speed&) const noexcept;
    void processSegment (float* const* tapeOut, int offset, int count,
                         bool isPlay, bool rec, bool character, const Speed& sp, int delay) noexcept;
    int designedSpeed = -1;
    double designedRate = 0.0;

    struct Retired { float* chunk; std::uint64_t stamp; };
    std::vector<Retired> retired;
    std::uint64_t lastSeenBlocks = 0;
    int idleTicks = 0;

    void timerCallback() override { service(); }
    void ensureChunks (int track, juce::int64 from, juce::int64 to);
    void write (int track, juce::int64 start, const float* src, int n) noexcept;
    void designFilters (int speedIndex) noexcept;
    void saturate (int numSamples, const Speed&) noexcept;
    int recordDelay() const noexcept;

    JUCE_DECLARE_NON_COPYABLE (TapeMachine)
};
