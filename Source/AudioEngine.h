#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include "Config.h"
#include "Params.h"
#include "ChannelStrip.h"
#include "TapeMachine.h"
#include "BusCompressor.h"
#include "InsertChain.h"

/** Owns the audio device, the console's parameters and its DSP, and runs the real-time callback.

    Signal flow (stage 2):
        interface input (mapped) -> input strip -> direct out -> tape in (normalled)
        tape out -> return strip
        input strips + return strips (main assign, pan) -> main fader -> mapped outputs
        soloed strips -> solo bus -> monitor outputs (which otherwise copy main)

    Real-time rules: the audio callback never allocates, locks or blocks. The UI changes
    Params (atomics) and calls TapeMachine transport methods (atomics). */
class AudioEngine : public juce::AudioIODeviceCallback
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
        static constexpr int channelOut = 0, returnOut = 16, tapeOut = 32, auxOut = 48, subOut = 56, mainL = 60, mainR = 61;
        static constexpr int numSources = 62;
        // Destinations
        static constexpr int tapeIn = 0, returnIn = 16, channelIn = 32;
        static constexpr int numDestinations = 48;

    };

    /** The interface input wired straight into a patch destination (-1 = none). It acts like a cable:
        into a tape in or a return strip in it breaks the normal. Channel strip ins are what setInputSource maps. */
    void setInterfaceInput (int destination, int deviceInput);
    int  getInterfaceInput (int destination) const;

    /** Which sources are plugged into a destination (bit n = source n). Message thread. */
    void setPatch (int destination, std::uint64_t sources);
    std::uint64_t getPatch (int destination) const;
    void clearPatch();

    /** Extra recording offset in samples, added to the latency the driver reports.
        Positive = the driver under-reports its latency. */
    void setRecordOffset (int samples);
    int  getRecordOffset() const { return recordOffset.load(); }

    // The console.
    ParamSet params;
    juce::OwnedArray<ChannelStrip> inputStrips, returnStrips;
    TapeMachine tape { params };
    juce::OwnedArray<FourBandEq> groupEq;                  // one stereo group EQ per sub
    std::array<Param*, Config::numSubs> subFader {};       // 0..100, unity at 80 (faders arrive in stage 5)
    std::array<Param*, Config::numAux> auxMaster {};       // 0..100, unity at 80
    BusCompressor busComp { params };

    // VST3 inserts: 4 slots on each sub (after the group EQ), each aux return, and the master bus.
    juce::OwnedArray<InsertChain> subInserts, auxInserts;
    InsertChain masterInserts { "Master" };
    std::array<Param*, Config::numAux> auxReturnPan {};   // -1..1: balance of the stereo return (it comes back at unity)

    // Scribble-strip names of the subs and the aux masters (message thread; saved with the session).
    std::array<juce::String, Config::numSubs> subNames;
    std::array<juce::String, Config::numAux> auxNames;
    static juce::String defaultSubName (int k) { return "Sub " + juce::String (k + 1); }
    static juce::String defaultAuxName (int k) { return "A" + juce::String (k + 1); }
    void resetBusNames();

    /** Extra delay added by plugins (latency compensation + master inserts), in samples. */
    int getPluginLatency() const noexcept { return pluginLatency.load (std::memory_order_relaxed); }
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

    std::atomic<bool> testToneOn { false };   // 440 Hz at -18 dBFS into main

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
    double tonePhase = 0.0;
    Dsp::Smoothed mainGainL;
    Dsp::Smoothed monitorSolo;   // 0 = monitor outs carry main, 1 = the solo bus
    Dsp::Smoothed monitorGainS;
    std::array<Dsp::Smoothed, Config::numSubs> subGain;
    std::array<Dsp::Smoothed, Config::numAux> auxGain;
    std::array<Dsp::Smoothed, Config::numAux> auxReturnGainL, auxReturnGainR;
    std::atomic<int> pluginLatency { 0 };

    // Scratch buffers, allocated once.
    using Block = std::array<float, Config::maxBlock>;
    std::array<Block, Config::numInputStrips> inputDirect {};
    std::array<Block, Config::numReturnStrips> returnDirect {};
    std::array<Block, Config::numTracks> tapeOut {};
    Block mixL {}, mixR {};
    Block soloL {}, soloR {};
    std::array<Block, Config::numSubs> subL {}, subR {};
    std::array<Block, Config::numAux> aux {};

    // Patch bay.
    std::array<std::atomic<std::uint64_t>, Patch::numDestinations> patch;
    std::array<Block, Patch::numSources> patchPrevious {};      // last block of each source, for patches that point backwards
    std::array<Block, Patch::numDestinations> patchInput {};    // summed cables into each destination
    std::array<Block, Config::numSubs> subMono {};
    Block returnL {}, returnR {};
    Dsp::DelayLine mainDelayL, mainDelayR;
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
