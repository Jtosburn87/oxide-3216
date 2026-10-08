#include "AudioEngine.h"

namespace
{
    constexpr float centrePan = 0.70710678f;   // -3 dB per side for a centred mono source
    constexpr float testToneLevel = 0.125893f; // -18 dBFS
    constexpr int   maxBlock = 512;            // larger device blocks are processed in chunks
}

AudioEngine::AudioEngine()
{
    for (int c = 0; c < numConsoleInputs; ++c)
    {
        inputSource[(size_t) c].store (c);
        inputPeak[(size_t) c].store (0.0f);
    }
    mainOutput[0].store (0);
    mainOutput[1].store (1);
    mainPeak[0].store (0.0f);
    mainPeak[1].store (0.0f);
    inputSlot.fill (-1);
    outputSlot.fill (-1);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::initialise (const juce::XmlElement* savedState)
{
    const juce::XmlElement* deviceXml = savedState != nullptr ? savedState->getChildByName ("DEVICESETUP") : nullptr;

    deviceManager.initialise (64, 64, deviceXml, true);

    // First run: prefer ASIO when a driver is installed.
    if (deviceXml == nullptr)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                if (! type->getDeviceNames().isEmpty())
                    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                break;
            }
        }
    }

    if (auto* routing = savedState != nullptr ? savedState->getChildByName ("ROUTING") : nullptr)
    {
        for (int c = 0; c < numConsoleInputs; ++c)
            setInputSource (c, routing->getIntAttribute ("in" + juce::String (c + 1), c));
        setMainOutput (0, routing->getIntAttribute ("mainL", 0));
        setMainOutput (1, routing->getIntAttribute ("mainR", 1));
    }

    deviceManager.addAudioCallback (this);
}

std::unique_ptr<juce::XmlElement> AudioEngine::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("AUDIOENGINE");

    if (auto device = deviceManager.createStateXml())
        xml->addChildElement (device.release());

    auto* routing = xml->createNewChildElement ("ROUTING");
    for (int c = 0; c < numConsoleInputs; ++c)
        routing->setAttribute ("in" + juce::String (c + 1), getInputSource (c));
    routing->setAttribute ("mainL", getMainOutput (0));
    routing->setAttribute ("mainR", getMainOutput (1));
    return xml;
}

void AudioEngine::setInputSource (int consoleChannel, int deviceInput)
{
    if (juce::isPositiveAndBelow (consoleChannel, numConsoleInputs))
        inputSource[(size_t) consoleChannel].store (juce::jlimit (-1, maxDeviceChannels - 1, deviceInput));
}

int AudioEngine::getInputSource (int consoleChannel) const
{
    return juce::isPositiveAndBelow (consoleChannel, numConsoleInputs) ? inputSource[(size_t) consoleChannel].load() : -1;
}

void AudioEngine::setMainOutput (int side, int deviceOutput)
{
    if (side == 0 || side == 1)
        mainOutput[(size_t) side].store (juce::jlimit (-1, maxDeviceChannels - 1, deviceOutput));
}

int AudioEngine::getMainOutput (int side) const
{
    return (side == 0 || side == 1) ? mainOutput[(size_t) side].load() : -1;
}

float AudioEngine::takeInputPeak (int consoleChannel)
{
    return juce::isPositiveAndBelow (consoleChannel, numConsoleInputs) ? inputPeak[(size_t) consoleChannel].exchange (0.0f) : 0.0f;
}

float AudioEngine::takeMainPeak (int side)
{
    return (side == 0 || side == 1) ? mainPeak[(size_t) side].exchange (0.0f) : 0.0f;
}

void AudioEngine::raisePeak (std::atomic<float>& peak, float value) noexcept
{
    auto current = peak.load (std::memory_order_relaxed);
    while (value > current && ! peak.compare_exchange_weak (current, value, std::memory_order_relaxed))
    {
    }
}

void AudioEngine::prepare (double sampleRate, const juce::BigInteger& activeInputs, const juce::BigInteger& activeOutputs)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    tonePhase = 0.0;

    // The callback only receives the active channels, packed in order.
    inputSlot.fill (-1);
    outputSlot.fill (-1);
    int slot = 0;
    for (int ch = 0; ch < maxDeviceChannels; ++ch)
        if (activeInputs[ch])
            inputSlot[(size_t) ch] = slot++;
    slot = 0;
    for (int ch = 0; ch < maxDeviceChannels; ++ch)
        if (activeOutputs[ch])
            outputSlot[(size_t) ch] = slot++;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getActiveInputChannels(), device->getActiveOutputChannels());
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    callbackCount.fetch_add (1, std::memory_order_relaxed);

    for (int o = 0; o < numOutputs; ++o)
        if (outputs[o] != nullptr)
            juce::FloatVectorOperations::clear (outputs[o], numSamples);

    // Resolve routing once per callback.
    const float* sources[numConsoleInputs] {};
    for (int c = 0; c < numConsoleInputs; ++c)
    {
        const int dev = inputSource[(size_t) c].load (std::memory_order_relaxed);
        const int slot = juce::isPositiveAndBelow (dev, maxDeviceChannels) ? inputSlot[(size_t) dev] : -1;
        sources[c] = juce::isPositiveAndBelow (slot, numInputs) ? inputs[slot] : nullptr;
    }

    float* mainOut[2] {};
    for (int side = 0; side < 2; ++side)
    {
        const int dev = mainOutput[(size_t) side].load (std::memory_order_relaxed);
        const int slot = juce::isPositiveAndBelow (dev, maxDeviceChannels) ? outputSlot[(size_t) dev] : -1;
        mainOut[side] = juce::isPositiveAndBelow (slot, numOutputs) ? outputs[slot] : nullptr;
    }

    const float gain = mainGain.load (std::memory_order_relaxed);
    const bool tone = testToneOn.load (std::memory_order_relaxed);
    const double toneInc = juce::MathConstants<double>::twoPi * 440.0 / currentSampleRate;

    float mix[2][maxBlock];

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        juce::FloatVectorOperations::clear (mix[0], n);
        juce::FloatVectorOperations::clear (mix[1], n);

        for (int c = 0; c < numConsoleInputs; ++c)
        {
            if (sources[c] == nullptr)
                continue;

            const float* in = sources[c] + start;
            const auto range = juce::FloatVectorOperations::findMinAndMax (in, n);
            raisePeak (inputPeak[(size_t) c], juce::jmax (range.getEnd(), -range.getStart()));
            juce::FloatVectorOperations::addWithMultiply (mix[0], in, centrePan, n);
            juce::FloatVectorOperations::addWithMultiply (mix[1], in, centrePan, n);
        }

        if (tone)
        {
            for (int i = 0; i < n; ++i)
            {
                const float s = testToneLevel * (float) std::sin (tonePhase);
                mix[0][i] += s;
                mix[1][i] += s;
                tonePhase += toneInc;
            }
            tonePhase = std::fmod (tonePhase, juce::MathConstants<double>::twoPi);
        }

        for (int side = 0; side < 2; ++side)
        {
            juce::FloatVectorOperations::multiply (mix[side], gain, n);
            const auto range = juce::FloatVectorOperations::findMinAndMax (mix[side], n);
            raisePeak (mainPeak[(size_t) side], juce::jmax (range.getEnd(), -range.getStart()));

            if (mainOut[side] != nullptr)
                juce::FloatVectorOperations::add (mainOut[side] + start, mix[side], n);
        }
    }
}
