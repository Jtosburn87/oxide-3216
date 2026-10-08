#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include "Config.h"
#include "Params.h"
#include "ChannelStrip.h"
#include "TapeMachine.h"

/** Owns the audio device, the console's parameters and its DSP, and runs the real-time callback.

    Signal flow (stage 2):
        interface input (mapped) -> input strip -> direct out -> tape in (normalled)
        tape out -> return strip
        input strips + return strips (main assign, pan) -> main fader -> mapped outputs

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

    /** Extra recording offset in samples, added to the latency the driver reports.
        Positive = the driver under-reports its latency. */
    void setRecordOffset (int samples);
    int  getRecordOffset() const { return recordOffset.load(); }

    // The console.
    ParamSet params;
    juce::OwnedArray<ChannelStrip> inputStrips, returnStrips;
    TapeMachine tape { params };
    Param* mainFader = nullptr;

    // Metering: peak linear level since the last call.
    float takeInputPeak (int strip);   // post-fader, input bank
    float takeMainPeak (int side);

    /** Number of audio callbacks so far: lets the UI tell whether audio is actually running. */
    std::uint64_t getCallbackCount() const noexcept { return callbackCount.load (std::memory_order_relaxed); }

    std::atomic<bool> testToneOn { false };   // 440 Hz at -18 dBFS into main

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

    std::array<std::atomic<int>, numConsoleInputs> inputSource;
    std::array<std::atomic<int>, 2> mainOutput;
    std::atomic<int> recordOffset { 0 }, reportedLatency { 0 };

    // device channel number -> index into the callback's buffer arrays (-1 = inactive)
    std::array<int, maxDeviceChannels> inputSlot {}, outputSlot {};

    std::array<std::atomic<float>, 2> mainPeak;
    std::atomic<std::uint64_t> callbackCount { 0 };
    double currentSampleRate = 48000.0;
    double tonePhase = 0.0;
    Dsp::Smoothed mainGainL;

    // Scratch buffers, allocated once.
    using Block = std::array<float, Config::maxBlock>;
    std::array<Block, Config::numInputStrips> inputDirect {};
    std::array<Block, Config::numReturnStrips> returnDirect {};
    std::array<Block, Config::numTracks> tapeOut {};
    Block mixL {}, mixR {};

    static void raisePeak (std::atomic<float>& peak, float value) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
