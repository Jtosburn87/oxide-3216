#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include "Config.h"
#include "Params.h"
#include "Scenes.h"
#include "ChannelStrip.h"
#include "StripLink.h"
#include "TapeMachine.h"
#include "BusCompressor.h"
#include "InsertChain.h"
#include "Talkback.h"
#include "Tempo.h"
#include "MidiEngine.h"
#include "PatchMask.h"

/** Owns the audio device, the console's parameters and its DSP, and runs the real-time callback.

    Signal flow (stage 2):
        interface input (mapped) -> input strip -> direct out -> tape in (normalled)
        tape out -> return strip
        input strips + return strips (main assign, pan) -> main fader -> mapped outputs
        soloed strips -> solo bus -> monitor outputs (which otherwise copy main)

    Real-time rules: the audio callback never allocates, locks or blocks. The UI changes
    Params (atomics) and calls TapeMachine transport methods (atomics). */
class AudioEngine : public juce::AudioIODeviceCallback,
                    private Param::Listener,
                    private juce::ChangeListener
{
public:
    static constexpr int numConsoleInputs  = 16;   // interface inputs that can be mapped
    static constexpr int maxDeviceChannels = 128;

    AudioEngine();
    ~AudioEngine() override;

    /** Opens the audio device: the saved one if possible, otherwise the first ASIO driver,
        otherwise the system default. */
    void initialise (const juce::XmlElement* savedState);
    std::unique_ptr<juce::XmlElement> createStateXml() const;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // Power switch: closes the device (frees the driver for other apps) or reopens it.
    void setPower (bool on);
    bool isPowered() const { return deviceManager.getCurrentAudioDevice() != nullptr; }

    // Routing. Device channel numbers are 0-based; -1 means "not connected".
    void setInputSource (int consoleChannel, int deviceInput);
    int  getInputSource (int consoleChannel) const;
    void setMainOutput (int side, int deviceOutput);   // side 0 = left, 1 = right
    int  getMainOutput (int side) const;
    /** Monitor outs (optional, -1 = none): a copy of main, or the solo bus while anything is soloed. */
    void setMonitorOutput (int side, int deviceOutput);
    int  getMonitorOutput (int side) const;
    void setAuxOutput (int aux, int deviceOutput);                // optional, -1 = none
    int  getAuxOutput (int aux) const;
    void setSubOutput (int sub, int side, int deviceOutput);      // optional, -1 = none
    int  getSubOutput (int sub, int side) const;

    //==========================================================================
    // Patch bay. Sources (top row) and destinations (bottom row), as in the prototype.
    struct Patch
    {
        // Sources
        static constexpr int channelOut = 0, returnOut = 16, tapeOut = 32, auxOut = 48, subOut = 56, mainL = 60, mainR = 61, oscillator = 62,
                             tbLimiterOut = 63,
                             instrumentOut = 64;   // MIDI track slot k's instrument: instrumentOut + 2k (left), + 2k + 1 (right)
        static constexpr int numSources = instrumentOut + 2 * Config::numMidiTracks;
        static_assert (numSources <= PatchMask::numBits, "the cable mask has room for every source");
        // Destinations
        static constexpr int tapeIn = 0, returnIn = 16, channelIn = 32, talkbackIn = 48, tbLimiterIn = 49, tbSend = 50,
                             interfaceOut = 51;   // interface outputs 1..numInterfaceOuts: whatever is patched in is added to them
        static constexpr int numInterfaceOuts = 32;
        static constexpr int numDestinations = interfaceOut + numInterfaceOuts;

    };

    /** The interface input wired straight into a patch destination (-1 = none). It acts like a cable:
        into a tape in or a return strip in it breaks the normal. Channel strip ins are what setInputSource maps. */
    void setInterfaceInput (int destination, int deviceInput);
    int  getInterfaceInput (int destination) const;

    /** Which sources are plugged into a destination (bit n = source n). Message thread. */
    void setPatch (int destination, PatchMask sources);
    PatchMask getPatch (int destination) const;
    void clearPatch();

    /** Routes MIDI track `slot`'s instrument (the MIDI page's routing button): into input strips or tape ins,
        one for a mono instrument, an odd/even pair for a stereo one (which also links a pair of input strips).
        It is patching, so the patch bay shows the cables. Whatever those inputs had (cables, interface input,
        the strip link) is kept and comes back when the instrument is routed elsewhere or to none. Another track
        routed into the same inputs is unrouted first. Message thread. */
    void routeInstrument (int slot, Midi::Route);
    /** Where a track's instrument is routed, read from the patch (a cable pulled in the patch bay counts). */
    Midi::Route getInstrumentRoute (int slot) const;
    /** The MIDI track routed into a destination by its routing button, or -1. */
    int instrumentRoutedInto (int destination) const;
    /** Unroutes a MIDI track, pulls any cable from its instrument and removes it. */
    void removeMidiTrack (int slot);
    using Restore = MidiEngine::Restore;
    /** The default patch: no cables, no interface inputs wired into the bay, channel strip N from interface input N. */
    void resetPatchToDefault();

    /** Extra recording offset in samples, added to the latency the driver reports.
        Positive = the driver under-reports its latency. */
    void setRecordOffset (int samples);
    int  getRecordOffset() const { return recordOffset.load(); }

    // The console.
    ParamSet params;
    SceneMemory scenes { params };   // 99 scenes of the faders and knobs (saved with the session)
    juce::OwnedArray<ChannelStrip> inputStrips, returnStrips;
    /** Stereo links between neighbouring strips (1+2, 3+4, ...): everything follows, pan mirrors. See StripLink. */
    std::array<Param*, Config::numInputStrips / 2> inputLink {};
    std::array<Param*, Config::numReturnStrips / 2> returnLink {};
    TapeMachine tape { params };
    juce::OwnedArray<FourBandEq> groupEq;                  // one stereo group EQ per sub
    std::array<Param*, Config::numSubs> subFader {};       // 0..100, unity at 80 (faders arrive in stage 5)
    std::array<Param*, Config::numAux> auxMaster {};       // 0..100, unity at 80
    BusCompressor busComp { params };
    /** Tempo, time signature and click (on the main and monitor outs only: never recorded or exported). */
    Tempo tempo { params };
    /** The MIDI page's tracks and instruments, played in step with the tape. */
    MidiEngine midi { tempo, tape };

    // VST3 inserts: 4 slots on each sub (after the group EQ), each aux return, and the master bus.
    juce::OwnedArray<InsertChain> subInserts, auxInserts;
    InsertChain masterInserts { "Master" };
    std::array<Param*, Config::numAux> auxReturnPan {};   // -1..1: balance of the stereo return (it comes back at unity)
    /** Main assigns: a sub (after its fader) and an aux return (after its inserts) only reach the main mix while
        assigned. An unassigned bus still feeds its interface outputs and the patch bay. */
    std::array<Param*, Config::numSubs> subMain {};
    std::array<Param*, Config::numAux> auxMain {};

    /** Aux pairs A1+A2, A3+A4, ... linked into one stereo aux: odd = left, even = right. The masters and main
        assigns move together, the odd aux's inserts process both sides in stereo (stereo plugins only) and its
        return pan balances them; the even aux's own inserts and return sit idle. Every channel feeds the pair in
        stereo (see ChannelStrip::auxLink). */
    static constexpr int numAuxPairs = Config::numAux / 2;
    std::array<Param*, numAuxPairs> auxLink {};
    /** Links or unlinks a pair from the console: linking also starts every channel's pan knob for the pair
        (the even send knob) at that channel's main pan. Returns the plugins on the pair's inserts that cannot run in stereo (left out while linked). */
    juce::StringArray setAuxLink (int pair, bool on);

    // Scribble-strip names of the subs and the aux masters (message thread; saved with the session).
    std::array<juce::String, Config::numSubs> subNames;
    std::array<juce::String, Config::numAux> auxNames;
    static juce::String defaultSubName (int k) { return "Sub " + juce::String (k + 1); }
    static juce::String defaultAuxName (int k) { return "A" + juce::String (k + 1); }
    void resetBusNames();

    /** Extra delay added by plugins (latency compensation + master inserts), in samples. */
    int getPluginLatency() const noexcept { return pluginLatency.load (std::memory_order_relaxed); }
    /** Plugin delay compensation (on): the dry mix, the subs and the aux returns wait for the slowest bus plugin chain,
        and every output (aux and sub outs, the click, the solo bus on the monitors) is held back to stay in step with
        main. Off: nothing waits (lowest latency for tracking), so bus plugins with latency come out late. */
    Param* pdcOn = nullptr;

    /** Bounce Song (real time): the main mix after the limiter (what the main outs carry, without the click) goes to
        a writer from the audio thread while the song plays and you mix. skip: samples left out first (the plugin delay,
        so the file starts on the song); total: samples to write, after which isBounceDone(). The writer stays the
        caller's: stopBounce() before deleting it. Message thread. */
    void startBounce (juce::AudioFormatWriter::ThreadedWriter*, juce::int64 skip, juce::int64 total);
    void stopBounce();
    bool isBouncing() const noexcept { return bouncing.load(); }
    bool isBounceDone() const noexcept { return bounceDone.load(); }
    juce::int64 getBounced() const noexcept { return bounceWritten.load(); }
    Param* mainFader = nullptr;
    Param* monitorLevel = nullptr;   // dB, -60 (off) .. +10: level of the monitor outs (headphones)
    static float monitorGain (float db) noexcept { return db <= -59.5f ? 0.0f : Dsp::dbToGain (db); }

    static constexpr float limiterCeiling = 0.70795f;   // -3 dBFS

    // Metering: peak linear level since the last call.
    float takeInputPeak (int strip);   // post-fader, input bank
    float takeMainPeak (int side);
    float takeSubPeak (int sub, int side);
    float takeAuxPeak (int aux);
    bool takeLimiting() noexcept { return limiting.exchange (false); }   // limiter worked since the last call

    /** Number of audio callbacks so far: lets the UI tell whether audio is actually running. */
    std::uint64_t getCallbackCount() const noexcept { return callbackCount.load (std::memory_order_relaxed); }

    /** Project name, written on the cassette label (message thread only). */
    juce::Value projectName { juce::var ("Untitled song") };

    /** Test oscillator, an output on the patch bay: sine (frequency swept by a knob), white or pink noise. */
    Param* oscType = nullptr;    // Dsp::Oscillator::Type: 0 sine, 1 white noise, 2 pink noise
    Param* oscFreq = nullptr;    // Hz, 20..20000 (sine)
    Param* oscLevel = nullptr;   // dBFS, the sine's peak (the noises have the same RMS)
    std::atomic<bool> oscillatorMuted { false };   // export: the mix is rendered without it

    /** Console colour: every input and return preamp voiced like a Neve 1073 (see Preamp). On by default. */
    Param* preampColour = nullptr;

    /** Talkback, as on a real console. On the patch bay: TB In (the talkback mic, usually from an interface input)
        is normalled to TB Limiter In, and the limiter's output to TB Send; whatever reaches TB Send goes, while
        Talk is held or latched, to the chosen aux outputs (any of Aux 1-8) at the TB level. It joins after the aux
        returns, so it never reaches the main mix through an effect. With "through the limiter" off, or with
        something else patched into TB Limiter In, TB Send is normalled straight from TB In instead (the talkback
        goes dry). Patching TB Send breaks its normal. The limiter's in and out work anywhere else too. */
    std::atomic<bool> talkbackOn { false };
    static constexpr int firstTalkbackAux = 0, numTalkbackAux = 8;   // Aux 1-8
    Param* tbLevel = nullptr;                                         // dB, -60 (off) .. +10
    std::array<Param*, numTalkbackAux> tbToAux {};                    // Aux 1..8
    Param* tbDim = nullptr;                                           // dim the monitors 15 dB while talking (always on: no switch since 0.1.3)
    Param* tbThroughLimiter = nullptr;
    Param* tbLimInput = nullptr;                                      // dB, 0..+30
    Param* tbLimBlend = nullptr;                                      // %, 0..100 (100 = all compressed)
    Param* tbLimMakeup = nullptr;                                     // dB, -12..+24
    static constexpr float talkbackDimDb = -15.0f;
    float takeTalkbackReduction() noexcept { return tbLimiter.takeReduction(); }

    /** Export: runs the console with no live inputs and writes main L/R (post limiter) to the given buffers.
        Only call while the device is not calling the engine (see MainComponent::exportMix). */
    void renderOffline (float* left, float* right, int numSamples);

    /** Called by audioDeviceAboutToStart; also used by the tests to run without a device. */
    void prepare (double sampleRate, const juce::BigInteger& activeInputs, const juce::BigInteger& activeOutputs,
                  int reportedRoundTripLatency = 0);

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    juce::AudioDeviceManager deviceManager;
    juce::OwnedArray<StripLink> stripLinks;
    void paramChanged (Param&) override;   // aux links: switch the odd aux's inserts to stereo only
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // device changes: see mainDevice

    /** The interface Main L/R were last mapped on. Opening a different one puts main back on its
        outputs 1 and 2 (and switches those on). Message thread only. */
    juce::String mainDevice;

    std::array<std::atomic<int>, Patch::numDestinations> interfaceSource;
    std::array<std::atomic<int>, 2> mainOutput, monitorOutput;
    std::array<std::atomic<int>, Config::numAux> auxOutput;
    std::array<std::array<std::atomic<int>, 2>, Config::numSubs> subOutput;
    float limiterEnv = 0.0f, limiterRelease = 0.0f;
    std::atomic<bool> limiting { false };
    std::atomic<int> recordOffset { 0 }, reportedLatency { 0 };

    // device channel number -> index into the callback's buffer arrays (-1 = inactive)
    std::array<int, maxDeviceChannels> inputSlot {}, outputSlot {};

    std::array<std::atomic<float>, 2> mainPeak;
    std::array<std::array<std::atomic<float>, 2>, Config::numSubs> subPeak;
    std::array<std::atomic<float>, Config::numAux> auxPeak;
    std::atomic<std::uint64_t> callbackCount { 0 };
    double currentSampleRate = 48000.0;
    Dsp::Oscillator oscillator;
    TalkbackLimiter tbLimiter;
    Dsp::Smoothed talkbackGain;
    Dsp::Smoothed mainGainL;
    Dsp::Smoothed monitorSolo;   // 0 = monitor outs carry main, 1 = the solo bus
    Dsp::Smoothed monitorGainS;
    std::array<Dsp::Smoothed, Config::numSubs> subGain;
    std::array<Dsp::Smoothed, Config::numAux> auxGain;
    std::array<Dsp::Smoothed, Config::numAux> auxReturnGainL, auxReturnGainR;
    std::array<float, Config::numSubs> lastSubMain {};
    std::atomic<int> pluginLatency { 0 };

    // Scratch buffers, allocated once.
    using Block = std::array<float, Config::maxBlock>;
    std::array<Block, Config::numInputStrips> inputDirect {};
    std::array<Block, Config::numReturnStrips> returnDirect {};
    std::array<Block, Config::numTracks> tapeOut {};
    Block mixL {}, mixR {};
    Block oscOut {};
    Block tbLimOut {};
    Block clickOut {};
    Block soloL {}, soloR {};
    std::array<Block, Config::numSubs> subL {}, subR {};
    std::array<Block, Config::numAux> aux {};

    // Patch bay.
    // Each destination's cable mask, as two words (a 128-bit atomic would not be lock free). A change between the
    // two stores is at worst one block with half a change in it.
    std::array<std::array<std::atomic<std::uint64_t>, 2>, Patch::numDestinations> patch;
    PatchMask loadPatch (int dest) const noexcept
    {
        return { patch[(size_t) dest][0].load (std::memory_order_relaxed), patch[(size_t) dest][1].load (std::memory_order_relaxed) };
    }
    // On the heap: together they are far too big for the stack (the tests build engines as locals).
    std::vector<Block> patchPrevious = std::vector<Block> (Patch::numSources);   // last block of each source, for patches that point backwards
    std::vector<Block> patchInput = std::vector<Block> (Patch::numDestinations); // summed cables into each destination
    std::array<Block, Config::numSubs> subMono {};
    Block returnL {}, returnR {};
    Dsp::DelayLine mainDelayL, mainDelayR;
    // Outputs held back by the plugin delay so they stay in step with main (see pdcOn).
    std::array<Dsp::DelayLine, Config::numAux> auxOutDelay;
    std::array<Dsp::DelayLine, Config::numSubs * 2> subOutDelay;
    Dsp::DelayLine clickDelay, soloDelayL, soloDelayR;
    int clickTail = 0;    // samples of delayed click still to come out after it stops
    juce::SpinLock bounceLock;
    juce::AudioFormatWriter::ThreadedWriter* bounceWriter = nullptr;
    std::atomic<bool> bouncing { false }, bounceDone { false };
    std::atomic<juce::int64> bounceSkip { 0 }, bounceRemaining { 0 }, bounceWritten { 0 };
    Block subOutTemp {};
    std::array<Dsp::DelayLine, Config::numSubs * 2> subDelay;
    std::array<Dsp::DelayLine, Config::numAux * 2> returnDelay;
    const float* patchSource (int source, int phase) const noexcept;
    /** The signal arriving at a patch destination: the normal when nothing is plugged in (channel strip ins
        have none); otherwise its interface input and its cables, summed. */
    const float* patchedInput (int dest, const float* normal, const float* interfaceIn, int phase, int n) noexcept;

    void render (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                 int numSamples, float* mainLeft, float* mainRight) noexcept;
    static void raisePeak (std::atomic<float>& peak, float value) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
