#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

/*  Owns the audio device and runs the real-time audio callback.

    Routing (stage 1): each of the 16 console input channels reads one interface
    input (chosen in the audio window), all channels are summed to main L/R, and
    main L/R go to two interface outputs (also chosen in the audio window).

    Real-time rules: the audio callback never allocates, locks or blocks. The UI
    talks to it only through atomics.
*/
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int numConsoleInputs  = 16;
    static constexpr int maxDeviceChannels = 128;

    AudioEngine();
    ~AudioEngine() override;

    /** Opens the audio device: the saved one if possible, otherwise the first ASIO driver,
        otherwise the system default. */
    void initialise (const juce::XmlElement* savedState);
    std::unique_ptr<juce::XmlElement> createStateXml() const;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // Routing. Device channel numbers are 0-based; -1 means "not connected".
    void setInputSource (int consoleChannel, int deviceInput);
    int  getInputSource (int consoleChannel) const;
    void setMainOutput (int side, int deviceOutput);   // side 0 = left, 1 = right
    int  getMainOutput (int side) const;

    // Metering: peak linear level since the last call.
    float takeInputPeak (int consoleChannel);
    float takeMainPeak (int side);

    /** Number of audio callbacks so far: lets the UI tell whether audio is actually running. */
    std::uint64_t getCallbackCount() const noexcept { return callbackCount.load (std::memory_order_relaxed); }

    std::atomic<bool>  testToneOn { false };   // 440 Hz at -18 dBFS into main
    std::atomic<float> mainGain   { 1.0f };    // linear

    /** Called by audioDeviceAboutToStart; also used by the tests to run without a device. */
    void prepare (double sampleRate, const juce::BigInteger& activeInputs, const juce::BigInteger& activeOutputs);

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

    // device channel number -> index into the callback's buffer arrays (-1 = inactive)
    std::array<int, maxDeviceChannels> inputSlot {}, outputSlot {};

    std::array<std::atomic<float>, numConsoleInputs> inputPeak;
    std::array<std::atomic<float>, 2> mainPeak;

    std::atomic<std::uint64_t> callbackCount { 0 };
    double currentSampleRate = 48000.0;
    double tonePhase = 0.0;

    static void raisePeak (std::atomic<float>& peak, float value) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
