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
    for (auto& o : auxOutput)
        o.store (-1);
    for (auto& pair : subOutput)
        for (auto& o : pair)
            o.store (-1);
    mainPeak[0].store (0.0f);
    mainPeak[1].store (0.0f);
    inputSlot.fill (-1);
    outputSlot.fill (-1);

    for (int i = 0; i < Config::numInputStrips; ++i)
        inputStrips.add (new ChannelStrip (params, "ch" + juce::String (i + 1), "Input " + juce::String (i + 1)));
    for (int i = 0; i < Config::numReturnStrips; ++i)
        returnStrips.add (new ChannelStrip (params, "rt" + juce::String (i + 1), "Return " + juce::String (i + 1)));

    for (int k = 0; k < Config::numSubs; ++k)
    {
        const auto id = "sub" + juce::String (k + 1);
        groupEq.add (new FourBandEq (params, id, 2));
        subFader[(size_t) k] = &params.add (id + ".fader", "Sub " + juce::String (k + 1) + " fader", 0.0f, 100.0f, 80.0f);
        subPeak[(size_t) k][0].store (0.0f);
        subPeak[(size_t) k][1].store (0.0f);
    }
    for (int k = 0; k < Config::numAux; ++k)
    {
        auxMaster[(size_t) k] = &params.add ("aux" + juce::String (k + 1) + ".master", "Aux " + juce::String (k + 1) + " master", 0.0f, 100.0f, 80.0f);
        auxPeak[(size_t) k].store (0.0f);
    }

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
        for (int k = 0; k < Config::numAux; ++k)
            setAuxOutput (k, routing->getIntAttribute ("aux" + juce::String (k + 1), -1));
        for (int k = 0; k < Config::numSubs; ++k)
            for (int side = 0; side < 2; ++side)
                setSubOutput (k, side, routing->getIntAttribute ("sub" + juce::String (k + 1) + (side == 0 ? "L" : "R"), -1));
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
    for (int k = 0; k < Config::numAux; ++k)
        routing->setAttribute ("aux" + juce::String (k + 1), getAuxOutput (k));
    for (int k = 0; k < Config::numSubs; ++k)
        for (int side = 0; side < 2; ++side)
            routing->setAttribute ("sub" + juce::String (k + 1) + (side == 0 ? "L" : "R"), getSubOutput (k, side));
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

void AudioEngine::setAuxOutput (int k, int dev)
{
    if (juce::isPositiveAndBelow (k, Config::numAux))
        auxOutput[(size_t) k].store (juce::jlimit (-1, maxDeviceChannels - 1, dev));
}

int AudioEngine::getAuxOutput (int k) const
{
    return juce::isPositiveAndBelow (k, Config::numAux) ? auxOutput[(size_t) k].load() : -1;
}

void AudioEngine::setSubOutput (int k, int side, int dev)
{
    if (juce::isPositiveAndBelow (k, Config::numSubs) && (side == 0 || side == 1))
        subOutput[(size_t) k][(size_t) side].store (juce::jlimit (-1, maxDeviceChannels - 1, dev));
}

int AudioEngine::getSubOutput (int k, int side) const
{
    return juce::isPositiveAndBelow (k, Config::numSubs) && (side == 0 || side == 1) ? subOutput[(size_t) k][(size_t) side].load() : -1;
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

float AudioEngine::takeSubPeak (int sub, int side)
{
    return juce::isPositiveAndBelow (sub, Config::numSubs) && (side == 0 || side == 1) ? subPeak[(size_t) sub][(size_t) side].exchange (0.0f) : 0.0f;
}

float AudioEngine::takeAuxPeak (int k)
{
    return juce::isPositiveAndBelow (k, Config::numAux) ? auxPeak[(size_t) k].exchange (0.0f) : 0.0f;
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
    busComp.prepare (currentSampleRate);
    limiterEnv = 0.0f;
    limiterRelease = (float) std::exp (-1.0 / (0.1 * currentSampleRate));
    for (int k = 0; k < Config::numSubs; ++k)
    {
        groupEq[k]->prepare (currentSampleRate);
        subGain[(size_t) k].prepare (currentSampleRate, Dsp::faderGain (subFader[(size_t) k]->get()));
    }
    for (int k = 0; k < Config::numAux; ++k)
        auxGain[(size_t) k].prepare (currentSampleRate, Dsp::faderGain (auxMaster[(size_t) k]->get()));
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

    // Optional extra outputs: aux masters and subs.
    const auto outFor = [&] (int dev) -> float*
    {
        const int slot = juce::isPositiveAndBelow (dev, maxDeviceChannels) ? outputSlot[(size_t) dev] : -1;
        return juce::isPositiveAndBelow (slot, numOutputs) ? outputs[slot] : nullptr;
    };
    float* auxOut[Config::numAux];
    for (int k = 0; k < Config::numAux; ++k)
        auxOut[k] = outFor (auxOutput[(size_t) k].load (std::memory_order_relaxed));
    float* subOut[Config::numSubs][2];
    for (int k = 0; k < Config::numSubs; ++k)
        for (int side = 0; side < 2; ++side)
            subOut[k][side] = outFor (subOutput[(size_t) k][(size_t) side].load (std::memory_order_relaxed));

    bool anySolo = false;
    for (auto* s : inputStrips)  anySolo = anySolo || s->controls.solo->on();
    for (auto* s : returnStrips) anySolo = anySolo || s->controls.solo->on();

    const bool tone = testToneOn.load (std::memory_order_relaxed);
    const double toneInc = juce::MathConstants<double>::twoPi * 440.0 / currentSampleRate;
    const float mainT = Dsp::faderGain (mainFader->get());

    const float* tapeInPtrs[Config::numTracks];
    float* tapeOutPtrs[Config::numTracks];

    BusPointers buses;
    buses.mainL = mixL.data();
    buses.mainR = mixR.data();
    for (int k = 0; k < Config::numSubs; ++k)
    {
        buses.subL[k] = subL[(size_t) k].data();
        buses.subR[k] = subR[(size_t) k].data();
    }
    for (int k = 0; k < Config::numAux; ++k)
        buses.aux[k] = aux[(size_t) k].data();

    for (auto* eq : groupEq)
        eq->update();

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        juce::FloatVectorOperations::clear (mixL.data(), n);
        juce::FloatVectorOperations::clear (mixR.data(), n);
        for (int k = 0; k < Config::numSubs; ++k)
        {
            juce::FloatVectorOperations::clear (subL[(size_t) k].data(), n);
            juce::FloatVectorOperations::clear (subR[(size_t) k].data(), n);
        }
        for (auto& a : aux)
            juce::FloatVectorOperations::clear (a.data(), n);

        // Input bank.
        for (int c = 0; c < Config::numInputStrips; ++c)
            inputStrips[c]->process (sources[c] != nullptr ? sources[c] + start : nullptr,
                                     inputDirect[(size_t) c].data(), buses, n, anySolo);

        // Tape: channel direct outs are normalled to tape ins.
        for (int t = 0; t < Config::numTracks; ++t)
        {
            tapeInPtrs[t] = t < Config::numInputStrips ? inputDirect[(size_t) t].data() : nullptr;
            tapeOutPtrs[t] = tapeOut[(size_t) t].data();
        }
        tape.process (tapeInPtrs, tapeOutPtrs, n);

        // Return bank: tape outs are normalled to return strip ins.
        for (int r = 0; r < Config::numReturnStrips; ++r)
            returnStrips[r]->process (tapeOut[(size_t) r].data(), returnDirect[(size_t) r].data(), buses, n, anySolo);

        // Subs: group EQ -> sub fader -> main (and optional sub outputs).
        for (int k = 0; k < Config::numSubs; ++k)
        {
            auto& eq = *groupEq[k];
            float* l = subL[(size_t) k].data();
            float* r = subR[(size_t) k].data();
            const float target = Dsp::faderGain (subFader[(size_t) k]->get());
            auto& sg = subGain[(size_t) k];
            const bool eqOn = ! eq.isFlat();

            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float g = sg.next (target);
                float xl = l[i], xr = r[i];
                if (eqOn)
                {
                    xl = eq.process (0, xl);
                    xr = eq.process (1, xr);
                }
                xl *= g;
                xr *= g;
                l[i] = xl;
                r[i] = xr;
                mixL[(size_t) i] += xl;
                mixR[(size_t) i] += xr;
                pl = juce::jmax (pl, std::abs (xl));
                pr = juce::jmax (pr, std::abs (xr));
            }
            raisePeak (subPeak[(size_t) k][0], pl);
            raisePeak (subPeak[(size_t) k][1], pr);
            if (subOut[k][0] != nullptr) juce::FloatVectorOperations::add (subOut[k][0] + start, l, n);
            if (subOut[k][1] != nullptr) juce::FloatVectorOperations::add (subOut[k][1] + start, r, n);
        }

        // Aux masters -> optional aux outputs (returns to main arrive with the plug-in stage).
        for (int k = 0; k < Config::numAux; ++k)
        {
            float* a = aux[(size_t) k].data();
            const float target = Dsp::faderGain (auxMaster[(size_t) k]->get());
            auto& ag = auxGain[(size_t) k];
            float pk = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                a[i] *= ag.next (target);
                pk = juce::jmax (pk, std::abs (a[i]));
            }
            raisePeak (auxPeak[(size_t) k], pk);
            if (auxOut[k] != nullptr)
                juce::FloatVectorOperations::add (auxOut[k] + start, a, n);
        }

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

        // Master section: bus compressor -> [master inserts, stage 7] -> main fader -> limiter -> outputs.
        busComp.process (mixL.data(), mixR.data(), n);

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

        // Limiter at -3 dBFS: instant attack (nothing gets past), 100 ms release, no added delay.
        bool limited = false;
        for (int i = 0; i < n; ++i)
        {
            const float pk = juce::jmax (std::abs (mixL[(size_t) i]), std::abs (mixR[(size_t) i]));
            limiterEnv = juce::jmax (pk, limiterEnv * limiterRelease);
            if (limiterEnv > limiterCeiling)
            {
                const float g = limiterCeiling / limiterEnv;
                mixL[(size_t) i] *= g;
                mixR[(size_t) i] *= g;
                limited = limited || g < 0.944f;   // more than 0.5 dB
            }
        }
        if (limited)
            limiting.store (true, std::memory_order_relaxed);

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
