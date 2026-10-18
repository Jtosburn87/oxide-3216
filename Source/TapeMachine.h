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
        { "7.5", 1.80f, 90.0f, 4.0f, -3.0f, 12000.0f, -70.0f, 2.4f, 0.060f, 0.030f },
        { "15",  1.30f, 60.0f, 2.5f, -1.0f, 18000.0f, -77.0f, 1.2f, 0.030f, 0.015f },
        { "30",  0.95f, 38.0f, 1.5f,  0.0f, 21000.0f, -84.0f, 0.6f, 0.015f, 0.008f },
    };

    explicit TapeMachine (ParamSet&);
    ~TapeMachine() override;

    Param* recordLevel;                       // dB into tape, -12..+18
    Param* speed;                             // 0, 1, 2 = 7.5, 15, 30 ips
    Param* pitch;                             // vari-speed, percent: -12..+12 (0 = the speed switch exactly)
    std::array<Param*, numTracks> arm {};     // armed tracks monitor their input and record
    std::array<Param*, numTracks> armPlay {}; // with arm: the track plays back the tape until Record is on
    Param* wowFlutter;                        // W&F on/off
    Param* wowAmount;                         // wow, percent of the speed's own (0..500, 100 = as the machine is)
    Param* flutterAmount;                     // flutter, the same
    static constexpr float maxWowFlutterPercent = 500.0f;

    /** The record arm button's three states, in the order a click steps through them. */
    enum class ArmState { off, playback, armed };   // grey; yellow: tape plays until Record; red: input only
    ArmState getArmState (int track) const noexcept;
    void setArmState (int track, ArmState);
    void stepArmState (int track);   // off -> playback -> armed -> off
    /** The track is armed and takes its input now (red, or yellow with Record on): its tape out carries the input
        and its return strip hears nothing. Otherwise it plays the tape back. */
    bool takesInput (int track) const noexcept;

    const Speed& currentSpeed() const noexcept { return speeds[juce::jlimit (0, 2, juce::roundToInt (speed->get()))]; }
    static float ipsOf (int speedIndex) noexcept { return speedIndex <= 0 ? 7.5f : speedIndex == 1 ? 15.0f : 30.0f; }

    /** Speed and pitch, as on a real machine: the tape remembers the speed it was recorded at (set by the
        first recording or import onto a blank tape, cleared by erasing it). Playing at another speed, or with
        the pitch knob off centre, plays it faster or slower: rate = speed x (1 + pitch) / recorded speed.
        Recording at a rate other than 1 lays the take onto the tape at the tape's own density, so an overdub
        made at 30 ips on a tape recorded at 15 is in tune with it at 30 and an octave down at 15.
        Positions (the counter, In/Out, length) count the tape itself, in samples at the recorded speed. */
    float getRecordedIps() const noexcept { return recordedIps.load(); }   // 0 = blank tape
    void setRecordedIps (float ips) noexcept { recordedIps.store (ips); }
    double getPlaybackRate() const noexcept;                               // what the knobs ask for now
    static constexpr float maxPitchPercent = 12.0f;

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
    /** Autolocate: wind to a position the way Rew / F Fwd do (speeding up, then braking to arrive exactly there).
        Used by RTZ, Go In / Go Out and the locate memories. Message thread. */
    void windTo (juce::int64 samplePosition);
    int  getWindDirection() const noexcept { return windRequest.load(); }   // -1 rewind, +1 fast forward, 0 none
    bool isWinding() const noexcept { return windRequest.load() != 0 || windSpeed.load() != 0.0f; }
    float getWindSpeed() const noexcept { return windSpeed.load(); }        // signed, times play speed
    static constexpr double windMaxSpeed = 20.0, windRampSeconds = 3.0, windBrakeSeconds = 0.6;
    /** Holding Rew or FF down: the tape winds up to windHeldSpeed (in windRampSeconds) while held, and settles back to
        windMaxSpeed when let go (it keeps winding). Stop, Play and a locate let go too. */
    static constexpr double windHeldSpeed = 40.0;
    void setWindHeld (bool held) noexcept { windHeld.store (held); }

    /** The shuttle ring: wind at `speed` times play speed (signed; 0 stops). The tape gets there within a
        fraction of a second, and stops at the start or the end of the recording. Takes over from Play, Rew and
        F Fwd. Message thread. */
    void setShuttle (float speed);
    float getShuttle() const noexcept { return shuttleOn.load() ? shuttleTarget.load() : 0.0f; }
    static constexpr float shuttleMaxSpeed = 32.0f;
    /** The jog wheel: move the tape by `samples` (signed). The tape follows smoothly. Message thread. */
    void jog (double samples);
    /** Scrub: while the shuttle or the jog wheel moves the tape you hear it, at that speed and direction. */
    std::atomic<bool> scrubOn { false };

    /** Locate memories (saved with the session): tape positions, -1 = empty. Message thread. */
    static constexpr int numMemories = 99;
    void setMemory (int m, juce::int64 p) { if (juce::isPositiveAndBelow (m, numMemories)) memories[(size_t) m] = p; }
    juce::int64 getMemory (int m) const { return juce::isPositiveAndBelow (m, numMemories) ? memories[(size_t) m] : -1; }

    void setRecordEnabled (bool);
    void eraseAll();

    /** Erases part of one track, [from, to) in tape samples, with a 5 ms fade at each edge so the cut
        never clicks. Message thread. */
    void eraseSection (int track, juce::int64 from, juce::int64 to);
    /** Erases a whole track. When no track holds audio any more the tape is blank again (length 0, and it
        takes the speed of its next recording). Message thread. */
    void eraseTrack (int track);

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

    // Change tracking for the session's track files (TapeFiles), in chunks of chunkSize samples. Any thread.
    /** The end of everything written on a track since it was last erased (or replaced) as a whole. */
    juce::int64 getWrittenEnd (int track) const noexcept { return writtenEnd[(size_t) track].load (std::memory_order_acquire); }
    /** True once for each chunk changed since the last call. */
    bool takeChunkChanged (int track, int chunk) noexcept;
    void markChunkChanged (int track, int chunk) noexcept;
    /** True once after a track was erased (or replaced) as a whole: its file should go. */
    bool takeTrackCleared (int track) noexcept { return trackCleared[(size_t) track].exchange (false); }
    /** The tape now matches the files (just loaded, or a new session): forget pending changes. Message thread. */
    void markFilesInStep() noexcept;
    /** Hold this while reading the tape from a worker thread: erased chunks are only freed under it. */
    juce::CriticalSection& getStorageLock() noexcept { return storageLock; }

    bool isPlaying() const noexcept         { return playing.load(); }
    bool isRecordEnabled() const noexcept   { return recordEnabled.load(); }
    bool isRecording (int track) const noexcept;
    juce::int64 getPosition() const noexcept { return position.load(); }
    /** A locate asked for and not yet done (the next block starts there), or -1. */
    juce::int64 getPendingLocate() const noexcept { return locateRequest.load(); }
    juce::int64 getLength() const noexcept   { return length.load(); }
    /** Play goes on past the end of the recording up to here (tape samples): the end of the MIDI page's last clip. */
    void setPlayOnUntil (juce::int64 samples) noexcept { playOnUntil.store (samples); }
    juce::int64 getCapacity() const noexcept { return (juce::int64) maxChunks * chunkSize; }   // samples per track
    double getSampleRate() const noexcept    { return sampleRate; }
    /** An audio device (or a test) has set the sample rate: until then it is only the default. */
    bool isPrepared() const noexcept         { return prepared.load(); }
    int getLatency() const noexcept          { return latency.load(); }
    /** How far behind the input recording lands (the latency, oversampling and the wow / flutter delay). */
    int getRecordDelay() const noexcept      { return recordDelay(); }
    void setLatency (int samples) noexcept   { latency.store (juce::jmax (0, samples)); }
    juce::int64 getDroppedSamples() const noexcept { return dropped.load(); }

    /** Export: every track plays back (armed ones too) and nothing records. */
    std::atomic<bool> exporting { false };

    /** Does this track hold any recorded audio? */
    bool trackHasAudio (int track) const noexcept;

    /** Writes audio straight onto a track (session loading). Message thread, transport stopped. */
    void writeTrack (int track, juce::int64 start, const float* data, int numSamples);

    /** Peak of a track's tape out since the last call (the meter bridge). Any thread. */
    float takeOutputPeak (int track) noexcept { return outPeak[(size_t) track].exchange (0.0f); }

    /** Tape character on/off. Always on in the app; the tests switch it off to check storage bit-for-bit. */
    std::atomic<bool> characterOn { true };

    /** Message thread: allocate chunks ahead of armed tracks, free erased chunks.
        Runs on a timer; tests call it directly. */
    void service();

    /** Read recorded audio (message thread, e.g. for saving). */
    void readTrack (int track, juce::int64 start, float* dest, int numSamples) const noexcept;

private:
    double sampleRate = 48000.0;
    std::atomic<bool> prepared { false };
    std::atomic<int> latency { 0 };
    int oversamplerLatency = 0;
    const size_t maxChunks;

    std::array<std::vector<std::atomic<float*>>, numTracks> chunks;

    std::atomic<bool> playing { false }, recordEnabled { false };
    std::atomic<juce::int64> position { 0 }, length { 0 }, locateRequest { -1 }, dropped { 0 }, playOnUntil { 0 };
    std::atomic<std::uint64_t> blocksProcessed { 0 };
    juce::int64 pos = 0;   // audio thread's own copy of the position
    std::atomic<int> windRequest { 0 };
    std::atomic<bool> windHeld { false };
    std::atomic<float> windSpeed { 0.0f };
    std::atomic<bool> windReset { false };
    std::atomic<juce::int64> windTarget { -1 };   // windTo: where the wind ends (-1: Rew / F Fwd, to either end)
    double windVel = 0.0, windPos = 0.0;   // audio thread: signed speed (times play speed), fractional position
    void wind (int numSamples) noexcept;

    // Shuttle and jog (see setShuttle / jog), and scrubbing the tape they move.
    std::atomic<float> shuttleTarget { 0.0f };
    std::atomic<bool> shuttleOn { false }, jogOn { false };
    std::atomic<double> jogTarget { 0.0 };
    bool moving = false;                   // audio thread: windPos is live (it follows pos otherwise)
    float scrubGain = 0.0f;
    std::array<std::array<float, 2>, numTracks> scrubLowPass {};
    void scrub (float* const* tapeOut, int numSamples, double from, double to, bool audible) noexcept;
    float sampleAt (int track, juce::int64 p) const noexcept;
    std::array<juce::int64, numMemories> memories;
    std::atomic<juce::int64> inPoint { 0 }, outPoint { 0 };
    std::array<std::atomic<std::uint32_t>, numTracks> trackRevision {};
    std::array<std::unique_ptr<std::atomic<bool>[]>, numTracks> chunkChanged;   // maxChunks each
    std::array<std::atomic<bool>, numTracks> trackCleared {};
    std::array<std::atomic<juce::int64>, numTracks> writtenEnd {};
    juce::CriticalSection storageLock;
    void clearTrackFile (int track) noexcept;
    std::array<std::atomic<float>, numTracks> outPeak {};

    // Vari-speed (rate != 1): the tape is read at fractional positions and recorded through a resampler.
    static constexpr double maxRate = 4.6;   // 30 ips + 12 % on a tape recorded at 7.5
    static constexpr int maxFetch = (int) (maxRate * Config::maxBlock) + 8;
    std::atomic<float> recordedIps { 0.0f };
    double rateNow = 1.0, vsPhase = 0.0;
    bool vsPrimed = false;
    float aaPlayCut = 0.0f, aaRecCut = 0.0f;
    struct Resampler
    {
        std::vector<float> stream;      // 4 tape samples of history, then this block's fetched samples
        std::array<float, 4> inHistory {};
        Dsp::Biquad play1, play2, rec1, rec2;   // anti-alias low-passes (4th order each)
    };
    std::array<Resampler, numTracks> vs;
    std::vector<int> fetchCount;               // per output sample
    std::vector<float> outFrac, fetchTime;     // per output sample / per fetched tape sample
    std::vector<juce::int64> fetchPos;         // tape position of each fetched sample
    std::vector<float> inScratch, recScratch;
    void processVarispeed (float* const* tapeOut, int n, bool rec, bool character, const Speed&, int delay,
                           double r0, double r1) noexcept;
    void primeVarispeed() noexcept;
    void designAntiAlias (double r0, double r1) noexcept;
    void wobblePlayback (int track, float* play, int offset, int count) noexcept;
    void recordRun (int track, juce::int64 w0, const float* src, int count, bool punch,
                    juce::int64 punchIn, juce::int64 punchOut, juce::int64& recordedEnd) noexcept;

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
    double wowScale = 1.0, flutterScale = 1.0;   // the Wow / Flutter knobs, smoothed (audio thread)
    std::array<float, Config::maxBlock> wowDelay {};
    std::array<float, Config::maxBlock> playScratch {};
    std::vector<float> fadeScratch;   // maxFetch
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
