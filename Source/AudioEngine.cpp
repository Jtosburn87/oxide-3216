#include "AudioEngine.h"

namespace
{
    constexpr float testToneLevel = 0.125893f;   // -18 dBFS
    constexpr int maxBlock = Config::maxBlock;
}

AudioEngine::AudioEngine()
{
    for (int c = 0; c < numConsoleInputs; ++c)
        inputSource[(size_t) c].store (c);
    mainOutput[0].store (0);
    mainOutput[1].store (1);
    mainPeak[0].store (0.0f);
    mainPeak[1].store (0.0f);
    inputSlot.fill (-1);
    outputSlot.fill (-1);

    for (int i = 0; i < Config::numInputStrips; ++i)
        inputStrips.add (new ChannelStrip (params, "ch" + juce::String (i + 1), "Input " + juce::String (i + 1)));
    for (int i = 0; i < Config::numReturnStrips; ++i)
        returnStrips.add (new ChannelStrip (params, "rt" + juce::String (i + 1), "Return " + juce::String (i + 1)));

    mainFader = &params.add ("main.fader", "Main fader", 0.0f, 100.0f, 80.0f);
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
        setRecordOffset (routing->getIntAttribute ("recordOffset", 0));
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
    routing->setAttribute ("recordOffset", getRecordOffset());
    return xml;
}

void AudioEngine::setPower (bool on)
{
    if (on == isPowered())
        return;

    if (on)
    {
        deviceManager.restartLastAudioDevice();
    }
    else
    {
        tape.stop();
        deviceManager.closeAudioDevice();
    }
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

void AudioEngine::setRecordOffset (int samples)
{
    recordOffset.store (juce::jlimit (-4800, 4800, samples));

    tape.setLatency (reportedLatency.load() + recordOffset.load());   // takes effect for the next take
}

float AudioEngine::takeInputPeak (int strip)
{
    return juce::isPositiveAndBelow (strip, inputStrips.size()) ? inputStrips[strip]->takePeak() : 0.0f;
}

float AudioEngine::takeMainPeak (int side)
{
    return (side == 0 || side == 1) ? mainPeak[(size_t) side].exchange (0.0f) : 0.0f;
}

void AudioEngine::raisePeak (std::atomic<float>& peak, float value) noexcept
{
    auto current = peak.load (std::memory_order_relaxed);
    while (value > current && ! peak.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
}

void AudioEngine::prepare (double sampleRate, const juce::BigInteger& activeInputs, const juce::BigInteger& activeOutputs,
                           int reportedRoundTripLatency)
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

    for (auto* s : inputStrips)  s->prepare (currentSampleRate);
    for (auto* s : returnStrips) s->prepare (currentSampleRate);
    reportedLatency.store (reportedRoundTripLatency);
    tape.prepare (currentSampleRate, reportedRoundTripLatency + recordOffset.load());
    mainGainL.prepare (currentSampleRate, Dsp::faderGain (mainFader->get()));
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getActiveInputChannels(), device->getActiveOutputChannels(),
             device->getInputLatencyInSamples() + device->getOutputLatencyInSamples());
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
    const float* sources[Config::numInputStrips] {};
    for (int c = 0; c < Config::numInputStrips; ++c)
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

    bool anySolo = false;
    for (auto* s : inputStrips)  anySolo = anySolo || s->controls.solo->on();
    for (auto* s : returnStrips) anySolo = anySolo || s->controls.solo->on();

    const bool tone = testToneOn.load (std::memory_order_relaxed);
    const double toneInc = juce::MathConstants<double>::twoPi * 440.0 / currentSampleRate;
    const float mainT = Dsp::faderGain (mainFader->get());

    const float* tapeInPtrs[Config::numTracks];
    float* tapeOutPtrs[Config::numTracks];

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        juce::FloatVectorOperations::clear (mixL.data(), n);
        juce::FloatVectorOperations::clear (mixR.data(), n);

        // Input bank.
        for (int c = 0; c < Config::numInputStrips; ++c)
            inputStrips[c]->process (sources[c] != nullptr ? sources[c] + start : nullptr,
                                     inputDirect[(size_t) c].data(), mixL.data(), mixR.data(), n, anySolo);

        // Tape: channel direct outs are normalled to tape ins.
        for (int t = 0; t < Config::numTracks; ++t)
        {
            tapeInPtrs[t] = t < Config::numInputStrips ? inputDirect[(size_t) t].data() : nullptr;
            tapeOutPtrs[t] = tapeOut[(size_t) t].data();
        }
        tape.process (tapeInPtrs, tapeOutPtrs, n);

        // Return bank: tape outs are normalled to return strip ins.
        for (int r = 0; r < Config::numReturnStrips; ++r)
            returnStrips[r]->process (tapeOut[(size_t) r].data(), returnDirect[(size_t) r].data(), mixL.data(), mixR.data(), n, anySolo);

        if (tone)
        {
            for (int i = 0; i < n; ++i)
            {
                const float s = testToneLevel * (float) std::sin (tonePhase);
                mixL[(size_t) i] += s;
                mixR[(size_t) i] += s;
                tonePhase += toneInc;
            }
            tonePhase = std::fmod (tonePhase, juce::MathConstants<double>::twoPi);
        }

        // Main fader (one smoothed gain for both sides).
        if (mainGainL.settled (mainT))
        {
            juce::FloatVectorOperations::multiply (mixL.data(), mainT, n);
            juce::FloatVectorOperations::multiply (mixR.data(), mainT, n);
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const float g = mainGainL.next (mainT);
                mixL[(size_t) i] *= g;
                mixR[(size_t) i] *= g;
            }
        }

        float* mix[2] = { mixL.data(), mixR.data() };
        for (int side = 0; side < 2; ++side)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (mix[side], n);
            raisePeak (mainPeak[(size_t) side], juce::jmax (range.getEnd(), -range.getStart()));

            if (mainOut[side] != nullptr)
                juce::FloatVectorOperations::add (mainOut[side] + start, mix[side], n);
        }
    }
}
