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
    for (auto& p : patch)
        p.store (0);
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
        returnStrips.add (new ChannelStrip (params, "rt" + juce::String (i + 1), "Return " + juce::String (i + 1), true));

    for (int k = 0; k < Config::numSubs; ++k)
    {
        const auto id = "sub" + juce::String (k + 1);
        groupEq.add (new FourBandEq (params, id, 2));
        subInserts.add (new InsertChain ("Sub " + juce::String (k + 1)));
        subFader[(size_t) k] = &params.add (id + ".fader", "Sub " + juce::String (k + 1) + " fader", 0.0f, 100.0f, 80.0f);
        subPeak[(size_t) k][0].store (0.0f);
        subPeak[(size_t) k][1].store (0.0f);
    }
    for (int k = 0; k < Config::numAux; ++k)
    {
        auxMaster[(size_t) k] = &params.add ("aux" + juce::String (k + 1) + ".master", "Aux " + juce::String (k + 1) + " master", 0.0f, 100.0f, 80.0f);
        auxReturnPan[(size_t) k] = &params.add ("aux" + juce::String (k + 1) + ".returnPan", "Aux " + juce::String (k + 1) + " return pan", -1.0f, 1.0f, 0.0f);
        auxInserts.add (new InsertChain ("Aux " + juce::String (k + 1) + " return"));
        auxPeak[(size_t) k].store (0.0f);
    }

    mainFader = &params.add ("main.fader", "Main fader", 0.0f, 100.0f, 80.0f);
    resetBusNames();
}

void AudioEngine::resetBusNames()
{
    for (int k = 0; k < Config::numSubs; ++k) subNames[(size_t) k] = defaultSubName (k);
    for (int k = 0; k < Config::numAux; ++k)  auxNames[(size_t) k] = defaultAuxName (k);
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
    for (auto* c : subInserts) c->prepare (currentSampleRate, Config::maxBlock);
    for (auto* c : auxInserts) c->prepare (currentSampleRate, Config::maxBlock);
    masterInserts.prepare (currentSampleRate, Config::maxBlock);
    for (int k = 0; k < Config::numAux; ++k)
    {
        auxReturnGainL[(size_t) k].prepare (currentSampleRate, 0.0f);
        auxReturnGainR[(size_t) k].prepare (currentSampleRate, 0.0f);
    }
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
    callbackCount.fetch_add (1, std::memory_order_relaxed);
    render (inputs, numInputs, outputs, numOutputs, numSamples, nullptr, nullptr);
}

void AudioEngine::renderOffline (float* left, float* right, int numSamples)
{
    render (nullptr, 0, nullptr, 0, numSamples, left, right);
}

void AudioEngine::render (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                          int numSamples, float* mainLeft, float* mainRight) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    if (mainLeft != nullptr)  juce::FloatVectorOperations::clear (mainLeft, numSamples);
    if (mainRight != nullptr) juce::FloatVectorOperations::clear (mainRight, numSamples);

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
    if (mainLeft != nullptr || mainRight != nullptr)
    {
        mainOut[0] = mainLeft;   // offline render (export)
        mainOut[1] = mainRight;
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

    std::uint64_t patchedSources = 0;
    for (auto& p : patch)
        patchedSources |= p.load (std::memory_order_relaxed);

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

        // Input bank: interface input, plus any cables patched into the channel strip input.
        for (int c = 0; c < Config::numInputStrips; ++c)
            inputStrips[c]->process (patchedInput (Patch::channelIn + c, sources[c] != nullptr ? sources[c] + start : nullptr, 0, n),
                                     inputDirect[(size_t) c].data(), buses, n, anySolo);

        // Tape: channel direct outs are normalled to tape ins; a cable breaks the normal.
        for (int t = 0; t < Config::numTracks; ++t)
        {
            const float* normal = t < Config::numInputStrips ? inputDirect[(size_t) t].data() : nullptr;
            tapeInPtrs[t] = patchedInput (Patch::tapeIn + t, normal, 1, n);
            tapeOutPtrs[t] = tapeOut[(size_t) t].data();
        }
        tape.process (tapeInPtrs, tapeOutPtrs, n);

        // Return bank: tape outs are normalled to return strip ins; a cable breaks the normal.
        for (int r = 0; r < Config::numReturnStrips; ++r)
            returnStrips[r]->process (patchedInput (Patch::returnIn + r, tapeOut[(size_t) r].data(), 2, n),
                                      returnDirect[(size_t) r].data(), buses, n, anySolo);

        // Latency compensation: the dry mix, every sub and every active aux return are delayed to line up
        // with the slowest plug-in chain (latencies as of the previous block).
        int compensation = 0;
        for (auto* c : subInserts) compensation = juce::jmax (compensation, c->getLatency());
        for (auto* c : auxInserts) if (c->isActive()) compensation = juce::jmax (compensation, c->getLatency());
        compensation = juce::jmin (compensation, Dsp::DelayLine::capacity - 1);
        mainDelayL.process (mixL.data(), n, compensation);
        mainDelayR.process (mixR.data(), n, compensation);

        // Subs: group EQ -> inserts -> sub fader -> main (and optional sub outputs).
        for (int k = 0; k < Config::numSubs; ++k)
        {
            auto& eq = *groupEq[k];
            float* l = subL[(size_t) k].data();
            float* r = subR[(size_t) k].data();

            if (! eq.isFlat())
                for (int i = 0; i < n; ++i)
                {
                    l[i] = eq.process (0, l[i]);
                    r[i] = eq.process (1, r[i]);
                }

            subInserts[k]->process (l, r, n);

            const float target = Dsp::faderGain (subFader[(size_t) k]->get());
            auto& sg = subGain[(size_t) k];
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float g = sg.next (target);
                l[i] *= g;
                r[i] *= g;
                pl = juce::jmax (pl, std::abs (l[i]));
                pr = juce::jmax (pr, std::abs (r[i]));
            }
            raisePeak (subPeak[(size_t) k][0], pl);
            raisePeak (subPeak[(size_t) k][1], pr);
            for (int i = 0; i < n; ++i)
                subMono[(size_t) k][(size_t) i] = 0.5f * (l[i] + r[i]);   // the patch bay sub out is mono
            if (subOut[k][0] != nullptr) juce::FloatVectorOperations::add (subOut[k][0] + start, l, n);
            if (subOut[k][1] != nullptr) juce::FloatVectorOperations::add (subOut[k][1] + start, r, n);

            // Into the main mix, compensated. (The sub buffers themselves stay undelayed for the patch bay.)
            const int d = compensation - subInserts[k]->getLatency();
            std::memcpy (returnL.data(), l, sizeof (float) * (size_t) n);
            std::memcpy (returnR.data(), r, sizeof (float) * (size_t) n);
            subDelay[(size_t) k * 2].process (returnL.data(), n, d);
            subDelay[(size_t) k * 2 + 1].process (returnR.data(), n, d);
            juce::FloatVectorOperations::add (mixL.data(), returnL.data(), n);
            juce::FloatVectorOperations::add (mixR.data(), returnR.data(), n);
        }

        // Aux masters -> optional aux outputs, and the aux return: inserts -> return level -> main.
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

            // The return only carries audio when a plug-in is active, so sends are never doubled dry.
            std::memcpy (returnL.data(), a, sizeof (float) * (size_t) n);
            std::memcpy (returnR.data(), a, sizeof (float) * (size_t) n);
            const bool active = auxInserts[k]->process (returnL.data(), returnR.data(), n);
            // Return pan is a balance: centre leaves both sides at unity, turning it lowers the other side.
            const float pan = auxReturnPan[(size_t) k]->get();
            const float targetL = active ? juce::jmin (1.0f, 1.0f - pan) : 0.0f;
            const float targetR = active ? juce::jmin (1.0f, 1.0f + pan) : 0.0f;
            auto& gl = auxReturnGainL[(size_t) k];
            auto& gr = auxReturnGainR[(size_t) k];
            if (active || ! gl.settled (0.0f) || ! gr.settled (0.0f))
            {
                for (int i = 0; i < n; ++i)
                {
                    returnL[(size_t) i] *= gl.next (targetL);
                    returnR[(size_t) i] *= gr.next (targetR);
                }
                const int d = compensation - auxInserts[k]->getLatency();
                returnDelay[(size_t) k * 2].process (returnL.data(), n, d);
                returnDelay[(size_t) k * 2 + 1].process (returnR.data(), n, d);
                juce::FloatVectorOperations::add (mixL.data(), returnL.data(), n);
                juce::FloatVectorOperations::add (mixR.data(), returnR.data(), n);
            }
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

        // Master section: bus compressor -> master inserts -> main fader -> limiter -> outputs.
        busComp.process (mixL.data(), mixR.data(), n);
        masterInserts.process (mixL.data(), mixR.data(), n);

        // What you hear is late by the compensation plus the master inserts: overdubs must be too.
        const int extra = compensation + masterInserts.getLatency();
        pluginLatency.store (extra, std::memory_order_relaxed);
        tape.setLatency (reportedLatency.load (std::memory_order_relaxed) + recordOffset.load (std::memory_order_relaxed) + extra);

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

        // Keep this block of every patched source for cables that point backwards in the signal flow.
        if (patchedSources != 0)
            for (int s = 0; s < Patch::numSources; ++s)
                if ((patchedSources >> s) & 1u)
                    std::memcpy (patchPrevious[(size_t) s].data(), patchSource (s, 4), sizeof (float) * (size_t) n);
    }
}

//==============================================================================
void AudioEngine::setPatch (int dest, std::uint64_t sources)
{
    if (juce::isPositiveAndBelow (dest, Patch::numDestinations))
        patch[(size_t) dest].store (sources & ((std::uint64_t (1) << Patch::numSources) - 1));
}

std::uint64_t AudioEngine::getPatch (int dest) const
{
    return juce::isPositiveAndBelow (dest, Patch::numDestinations) ? patch[(size_t) dest].load() : 0;
}

void AudioEngine::clearPatch()
{
    for (auto& p : patch)
        p.store (0);
}

const float* AudioEngine::patchSource (int s, int phase) const noexcept
{
    // phase: 0 = nothing processed yet this block, 1 = input strips done, 2 = tape done, 3 = returns done, 4 = all.
    // Anything not processed yet comes from the previous block.
    const float* previous = patchPrevious[(size_t) s].data();
    if (s < Patch::returnOut) return phase >= 1 ? inputDirect[(size_t) s].data() : previous;
    if (s < Patch::tapeOut)   return phase >= 3 ? returnDirect[(size_t) (s - Patch::returnOut)].data() : previous;
    if (s < Patch::auxOut)    return phase >= 2 ? tapeOut[(size_t) (s - Patch::tapeOut)].data() : previous;
    if (s < Patch::subOut)    return phase >= 4 ? aux[(size_t) (s - Patch::auxOut)].data() : previous;
    if (s < Patch::mainL)     return phase >= 4 ? subMono[(size_t) (s - Patch::subOut)].data() : previous;
    return phase >= 4 ? (s == Patch::mainL ? mixL.data() : mixR.data()) : previous;
}

const float* AudioEngine::patchedInput (int dest, const float* normal, int phase, int n) noexcept
{
    const auto cables = patch[(size_t) dest].load (std::memory_order_relaxed);
    if (cables == 0)
        return normal;

    // A cable breaks a normal. Channel strip inputs are not normalled: the interface input stays.
    float* d = patchInput[(size_t) dest].data();
    if (! Patch::isNormalled (dest) && normal != nullptr)
        std::memcpy (d, normal, sizeof (float) * (size_t) n);
    else
        std::memset (d, 0, sizeof (float) * (size_t) n);

    for (int s = 0; s < Patch::numSources; ++s)
        if ((cables >> s) & 1u)
            juce::FloatVectorOperations::add (d, patchSource (s, phase), n);
    return d;
}
