#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include "Config.h"
#include "Tempo.h"
#include "PatchMask.h"
#include "MidiOut.h"

class TapeMachine;

/** MIDI on the MIDI page: notes, clips and the tracks that hold them. Positions are ticks of a quarter note
    (Tempo::ticksPerQuarter) from the start of the tape, laid on it by the tempo (map), so the MIDI follows the tape exactly. */
namespace Midi
{
    struct Note
    {
        juce::int64 start = 0, length = Tempo::ticksPerQuarter / 4;   // ticks, start from the clip's start
        int pitch = 60, velocity = 100;
        int channel = 1;   // 1-16, as played or as in the MIDI file (the track's channel setting can override it)
        bool operator== (const Note& o) const { return start == o.start && length == o.length && pitch == o.pitch && velocity == o.velocity && channel == o.channel; }
    };

    /** A controller change (CC 0-127) or a program change (number programChange) in a clip. */
    struct Control
    {
        juce::int64 tick = 0;   // from the clip's start
        int number = 1, value = 0;
        int channel = 1;   // 1-16
        bool operator== (const Control& o) const { return tick == o.tick && number == o.number && value == o.value && channel == o.channel; }
    };
    constexpr int programChange = 128;   // Control::number of a program change (value = program 0-127)

    struct Clip
    {
        juce::String name;
        juce::int64 start = 0, length = 0;   // ticks from the start of the tape
        std::vector<Note> notes;
        std::vector<Control> controls;        // CC and program changes, sorted by tick
        juce::int64 end() const noexcept { return start + length; }
    };

    /** Where a track's instrument goes: nowhere, into input strips or into tape tracks (one, or two in stereo). */
    struct Route
    {
        enum Kind { none = 0, input = 1, tape = 2 };
        Kind kind = none;
        int first = 0;   // 0-based strip or tape track (a stereo instrument takes first and first + 1)
        bool operator== (const Route& o) const { return kind == o.kind && (kind == none || first == o.first); }
    };

    /** A note on or off, a CC or a program change from a MIDI keyboard while recording, at the tick it arrived
        (audio thread -> message thread). */
    struct RecordedEvent
    {
        enum Kind : std::uint8_t { note, controller, program };
        int track = 0;
        juce::int64 tick = 0;
        Kind kind = note;
        std::uint8_t a = 0, b = 0;   // note: pitch, velocity (0 = off); controller: number, value; program: program
        std::uint8_t channel = 1;    // 1-16, as it came from the keyboard
    };
}

/** The MIDI tracks (up to Config::numMidiTracks), each with one instrument plugin, its clips and a MIDI input.
    A track keeps its slot (0-based) for as long as it exists: slot k's instrument is patch bay source
    AudioEngine::Patch::instrumentOut + 2k (left) and + 2k + 1 (right; a mono instrument gives the same on both).

    Threads: tracks, clips and plugins are changed on the message thread. The audio thread plays the clips while the
    tape plays (process), feeds the instruments the live MIDI input of the armed track (or, with none armed, the
    selected one), and while the tape records collects what is played on armed tracks; the message thread turns that
    into a new clip when recording stops. Changes swap in under a spin lock the audio thread only ever tries. */
class MidiEngine : private juce::MidiInputCallback,
                   private juce::AudioProcessorListener,
                   private juce::Timer
{
public:
    static constexpr int maxTracks = Config::numMidiTracks;

    MidiEngine (Tempo&, TapeMachine&);
    ~MidiEngine() override;

    // Tracks (message thread) ------------------------------------------------
    /** Adds a track in the first free slot; returns the slot or -1 when every slot is used. */
    int addTrack (const juce::String& name = {});
    /** Takes the instrument and the clips with it. Unroute it first (AudioEngine::routeInstrument). */
    void removeTrack (int slot);
    void removeAllTracks();
    bool isUsed (int slot) const noexcept;
    /** The slots in use, in order. */
    juce::Array<int> usedSlots() const;

    juce::String getName (int slot) const;
    void setName (int slot, const juce::String&);
    juce::Colour getColour (int slot) const;
    void setColour (int slot, juce::Colour);

    void setArmed (int slot, bool);
    bool isArmed (int slot) const noexcept;
    void setMuted (int slot, bool);
    bool isMuted (int slot) const noexcept;
    void setSoloed (int slot, bool);
    bool isSoloed (int slot) const noexcept;
    /** The track the MIDI page has selected: it hears the keyboard while no track is armed. */
    void setSelected (int slot);
    int getSelected() const noexcept { return selected.load(); }

    /** The MIDI input a track listens to: a device identifier, allInputs or empty (none). */
    static constexpr const char* allInputs = "*";
    void setInput (int slot, const juce::String& deviceIdentifier);
    juce::String getInput (int slot) const;
    /** The MIDI inputs on this computer (opened as they appear). */
    static juce::Array<juce::MidiDeviceInfo> availableInputs() { return juce::MidiInput::getAvailableDevices(); }

    /** The channel a track plays on, to its instrument and its MIDI out: 1-16 (everything on that channel), or
        asRecorded (each note and controller on the channel it was played or imported on). New tracks: 1. */
    static constexpr int asRecorded = 0;
    void setChannel (int slot, int channel);
    int getChannel (int slot) const noexcept;

    /** A hardware MIDI output the track plays too (its clips, chased controllers and what comes in live): a device
        identifier, or empty for none. Notes go out when the audio they line up with is heard (the output latency and
        the console's plugin delay), plus the track's offset (ms; later is positive, earlier only as far as that latency). */
    void setOutput (int slot, const juce::String& deviceIdentifier);
    juce::String getOutput (int slot) const;
    void setOutputOffset (int slot, double ms);
    double getOutputOffset (int slot) const noexcept;
    static constexpr double maxOffsetMs = 100.0;

    /** MIDI clock (24 a quarter note, following the tempo map and vari-speed) with song position, start, continue
        and stop, to each output switched on here, while the tape plays. Offset as for the tracks. */
    void setClockOutput (const juce::String& deviceIdentifier, bool on);
    bool sendsClockTo (const juce::String& deviceIdentifier) const { return clockOutputs.contains (deviceIdentifier); }
    const juce::StringArray& getClockOutputs() const noexcept { return clockOutputs; }
    void setClockOffset (double ms);
    double getClockOffset() const noexcept { return clockOffset.load(); }

    MidiOuts& outs() noexcept { return midiOuts; }

    /** Scene control: a track can send its clips' controllers and program changes there (as it plays, not the
        chase when the tape starts part-way). */
    void setToScenes (int slot, bool);
    bool sendsToScenes (int slot) const noexcept;
    /** Controllers and program changes for scene control, on the message thread (about 100 times a second):
        internal = from a MIDI page track, else from the hardware input deviceIdentifier (every input's are passed). */
    std::function<void (const juce::MidiMessage&, bool internal, const juce::String& deviceIdentifier)> onSceneMidi;
    /** Every note on, controller and program change from the MIDI inputs, first (message thread): for MIDI shortcuts
        and the Jog / shuttle window. Return true if it was taken for a Learn (scene control then never sees it). */
    std::function<bool (const juce::MidiMessage&, const juce::String& deviceIdentifier)> onHardwareMidi;
    /** For the tests: hands what is waiting to onHardwareMidi / onSceneMidi now. */
    void deliverSceneMidi();

    /** A standard MIDI file (dragged from a plugin's groove browser, or from Explorer) as a clip starting at 0: its
        notes (every track merged, each note on its own channel) in our ticks, the length rounded up to whole bars of the tempo (map),
        named after the file. Empty clip and an error if it can't be read. */
    static Midi::Clip readMidiFile (const juce::File&, const Tempo&, juce::String& error);
    /** A MIDI file's tempo map (its tempo and time signature changes, in our ticks). Empty and an error if it has none. */
    static std::vector<Tempo::Change> readTempoMap (const juce::File&, juce::String& error);
    /** A clip length from `start` to the end of the bar holding contentEnd - 1 (the last tick with something in it). */
    static juce::int64 barsLength (const Tempo&, juce::int64 start, juce::int64 contentEnd);
    static bool isMidiFile (const juce::String& path) { return path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi") || path.endsWithIgnoreCase (".smf"); }

    // Instruments (message thread) --------------------------------------------
    /** Takes a created plugin (PluginManager::create), prefers stereo out, prepares it and swaps it in. */
    void setInstrument (int slot, std::unique_ptr<juce::AudioPluginInstance>, const juce::PluginDescription&);
    void clearInstrument (int slot);
    juce::AudioPluginInstance* getInstrument (int slot) const;
    const juce::PluginDescription& getDescription (int slot) const;
    bool isStereo (int slot) const noexcept;   // the instrument has two outputs (a track with none counts as stereo)
    /** Called just before an instrument is deleted (so its editor window can close first). */
    std::function<void (juce::AudioProcessor*)> beforeDelete;

    // Clips (message thread) -----------------------------------------------------
    const std::vector<Midi::Clip>& getClips (int slot) const;
    void setClips (int slot, std::vector<Midi::Clip>);
    /** The end of the last clip on any track, in ticks (0 without clips). */
    juce::int64 songEnd() const;

    /** Something changed (tracks, clips, a recording landed): for the MIDI page. Message thread. */
    std::function<void()> onChange;
    std::uint32_t getRevision() const noexcept { return revision; }
    /** Goes up when an instrument reports a parameter or state change (so the session saves itself). */
    std::uint32_t getPluginChangeCount() const noexcept { return pluginChanges.load(); }

    // Routing bookkeeping (AudioEngine::routeInstrument does the patching) ---------
    /** What a destination had before an instrument was routed into it, put back when it is unrouted.
        link: the strip's stereo link before (-1 = not recorded, 0 = none, 1 = its neighbour (older sessions), else partner + 2). */
    struct Restore { int destination = 0; PatchMask mask; int interfaceInput = -1; int link = -1; };
    std::vector<Restore>& restoreList (int slot);
    const std::vector<Restore>& restoreList (int slot) const;

    // Sessions -------------------------------------------------------------------------
    /** Tracks, clips, inputs and instruments (description + state); routing lives in the patch. */
    juce::var toVar() const;
    /** Recreates the tracks; createPlugin makes instruments (empty: none). Returns plugins that could not be loaded. */
    juce::StringArray fromVar (const juce::var&, const std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, juce::String& error)>& createPlugin);

    // Audio thread -----------------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);
    /** Renders every instrument for one block. The tape is at tapeFrom (tape samples) and moves `rate` tape samples
        per output sample while rolling (playing); recording: the tape is recording, so armed tracks capture what is played.
        outTimeMs: when the block's first sample is heard (Time::getMillisecondCounterHiRes), for the MIDI outputs
        and the clock; negative (an offline render): nothing goes out. */
    void process (int numSamples, juce::int64 tapeFrom, double rate, bool rolling, bool recording, double outTimeMs = -1.0) noexcept;
    /** An instrument output for the patch bay: source 0 = slot 0 left, 1 = slot 0 right, 2 = slot 1 left... */
    const float* output (int source) const noexcept { return outputs[(size_t) juce::jlimit (0, 2 * maxTracks - 1, source)].data(); }

    /** Opens the computer's MIDI inputs and the outputs in use (and any plugged in later). Off until the app calls it,
        so the tests leave them alone. */
    void enableDeviceInputs() { openDevices = true; openNewInputs(); midiOuts.enable(); }

    /** For the tests. Feeds a message to a slot as if it came from its input. */
    void injectLive (int slot, const juce::MidiMessage&);
    /** For the tests: runs what the message-thread timer does (recordings into clips, the song end for the tape). */
    void tick() { timerCallback(); }

private:
    /** The clips compiled for playback. Kinds sort in this order on the same tick: program, controller, off, on. */
    struct Event
    {
        enum Kind : std::uint8_t { program = 0, controller = 1, noteOff = 2, noteOn = 3 };
        juce::int64 tick;
        Kind kind;
        std::uint8_t a, b;   // note: pitch, velocity; controller: number, value; program: program
        std::uint8_t ch;     // 0-15, as recorded (the track's channel setting is applied as it plays)
    };

    struct Slot
    {
        // Message thread.
        juce::String name, input;
        juce::Colour colour;
        std::vector<Midi::Clip> clips;
        juce::PluginDescription description;
        std::vector<Restore> restore;
        // Recording in progress (message thread): notes held and finished.
        std::array<juce::int64, 128> recordHeld {};   // tick of the note on, -1 = up
        std::array<int, 128> recordVelocity {}, recordChannel {};
        juce::String output;                          // MIDI out device identifier (empty: none)
        std::vector<Midi::Note> recorded;             // absolute ticks
        std::vector<Midi::Control> recordedControls;  // absolute ticks
        bool recordingTake = false;

        // Shared, swapped under the lock.
        juce::SpinLock lock;
        std::unique_ptr<juce::AudioPluginInstance> processor;
        std::vector<Event> events;   // the clips as note ons / offs, sorted by tick
        juce::AudioBuffer<float> buffer;

        std::atomic<bool> used { false }, armed { false }, muted { false }, soloed { false }, stereo { true };
        std::atomic<bool> listens { false };   // takes the live input (armed, or selected with none armed)
        std::atomic<int> channel { 1 };        // 1-16 or asRecorded
        std::atomic<bool> toScenes { false };  // clip controllers / program changes go to scene control too
        std::atomic<int> outPort { -1 };       // MidiOuts port, -1 = none
        std::atomic<double> outOffset { 0.0 }; // ms
        juce::MidiMessageCollector live;

        // Audio thread.
        juce::MidiBuffer midi, remapped;
        std::array<std::uint16_t, 128> sounding {};   // per pitch, the channels (bit 0 = 1) the clips have it on
        juce::int64 expectedFrom = -1;        // where the tape should be next block if it keeps rolling
        int sentPort = -1, sentChannel = 1;   // where the notes sounding went (a change ends them there)
    };

    Tempo& tempo;
    TapeMachine& tape;
    std::array<std::unique_ptr<Slot>, maxTracks> slots;
    std::atomic<int> selected { -1 };
    double sampleRate = 48000.0;
    int blockSize = Config::maxBlock;
    std::uint32_t revision = 0;
    std::atomic<std::uint32_t> pluginChanges { 0 };
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { pluginChanges.fetch_add (1); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged)
            pluginChanges.fetch_add (1);
    }
    std::vector<std::array<float, Config::maxBlock>> outputs = std::vector<std::array<float, Config::maxBlock>> (2 * maxTracks);

    // Live MIDI in.
    juce::OwnedArray<juce::MidiInput> openInputs;
    juce::StringArray openIds;
    mutable juce::CriticalSection inputLock;   // each track's input name: read on the MIDI thread
    bool openDevices = false;
    void openNewInputs();
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    // Recording: audio thread -> message thread.
    juce::AbstractFifo recordFifo { 4096 };
    std::vector<Midi::RecordedEvent> recordBuffer = std::vector<Midi::RecordedEvent> (4096);
    std::atomic<bool> recordingNow { false };
    void pushRecorded (const Midi::RecordedEvent&) noexcept;
    void finishTake (Slot&, juce::int64 endTick);

    // MIDI out and clock.
    MidiOuts midiOuts;
    juce::StringArray clockOutputs;
    std::atomic<std::uint32_t> clockPorts { 0 };   // bit per MidiOuts port
    std::atomic<double> clockOffset { 0.0 };
    // Audio thread: the clock run in progress.
    juce::int64 clockExpected = -1;   // where the tape should be next block if it keeps rolling
    juce::int64 clockNext = 0;        // the next clock to send, in clocks from the start (40 ticks each)
    std::uint32_t clockRunning = 0;   // ports that were sent start / continue
    // Scene control: from the audio thread (tracks) and the MIDI input thread (hardware) to the message thread.
    struct SceneBytes { std::uint8_t data[3]; };
    juce::AbstractFifo sceneFifo { 256 };
    std::vector<SceneBytes> sceneQueue = std::vector<SceneBytes> (256);
    juce::CriticalSection sceneInLock;
    std::vector<std::pair<juce::MidiMessage, juce::String>> sceneFromDevices;
    juce::TimedCallback sceneDelivery { [this] { deliverSceneMidi(); } };
    void pushScene (SceneBytes) noexcept;

    void endSounding (Slot&, juce::MidiBuffer&, int port, double timeMs) noexcept;
    /** Runs the MIDI out sending timer only while a track has a MIDI out or the clock goes somewhere. */
    void updateOutputsActive();
    void clock (int numSamples, juce::int64 from, juce::int64 to, double t0, double t1, bool rolling, double outTimeMs) noexcept;

    int initSlot (int slot, const juce::String& name);
    void compile (Slot&);   // clips -> events, swapped in
    void updateListening();
    void changed();
    double ticksAt (juce::int64 tapeSamples) const noexcept;
    juce::int64 samplesAt (juce::int64 ticks) const noexcept;
    void timerCallback() override;
    int inputCheckCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE (MidiEngine)
};
