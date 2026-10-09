#include "AudioEngine.h"

namespace
{
    constexpr int maxBlock = Config::maxBlock;
}

AudioEngine::AudioEngine()
{
    for (auto& i : interfaceSource)
        i.store (-1);
    for (int c = 0; c < numConsoleInputs; ++c)
        setInputSource (c, c);
    mainOutput[0].store (0);
    mainOutput[1].store (1);
    monitorOutput[0].store (-1);
    monitorOutput[1].store (-1);
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
        inputStrips.add (new ChannelStrip (params, "ch" + juce::String (i + 1), "Input " + juce::String (i + 1), false, 0.0f));   // input faders start down
    for (int i = 0; i < Config::numReturnStrips; ++i)
        returnStrips.add (new ChannelStrip (params, "rt" + juce::String (i + 1), "Return " + juce::String (i + 1), true));

    for (int k = 0; k < Config::numSubs; ++k)
    {
        const auto id = "sub" + juce::String (k + 1);
        groupEq.add (new FourBandEq (params, id, 2));
        subInserts.add (new InsertChain ("Sub " + juce::String (k + 1)));
        subFader[(size_t) k] = &params.add (id + ".fader", "Sub " + juce::String (k + 1) + " fader", 0.0f, 100.0f, 80.0f);
        subMain[(size_t) k] = &params.addToggle (id + ".main", "Sub " + juce::String (k + 1) + " to main", true);
        subPeak[(size_t) k][0].store (0.0f);
        subPeak[(size_t) k][1].store (0.0f);
    }
    for (int k = 0; k < Config::numAux; ++k)
    {
        auxMaster[(size_t) k] = &params.add ("aux" + juce::String (k + 1) + ".master", "Aux " + juce::String (k + 1) + " master", 0.0f, 100.0f, 80.0f);
        auxReturnPan[(size_t) k] = &params.add ("aux" + juce::String (k + 1) + ".returnPan", "Aux " + juce::String (k + 1) + " return pan", -1.0f, 1.0f, 0.0f);
        auxMain[(size_t) k] = &params.addToggle ("aux" + juce::String (k + 1) + ".main", "Aux " + juce::String (k + 1) + " to main", false);
        auxInserts.add (new InsertChain ("Aux " + juce::String (k + 1) + " return"));
        auxPeak[(size_t) k].store (0.0f);
    }

    mainFader = &params.add ("main.fader", "Main fader", 0.0f, 100.0f, 80.0f);
    monitorLevel = &params.add ("monitor.level", "Monitor level", -60.0f, 10.0f, 0.0f);
    oscType = &params.addStepped ("osc.type", "Oscillator type", 3, Dsp::Oscillator::sine);
    oscFreq = &params.add ("osc.freq", "Oscillator frequency", 20.0f, 20000.0f, 1000.0f, Param::Scale::log);
    oscLevel = &params.add ("osc.level", "Oscillator level", -60.0f, 0.0f, -18.0f);
    preampColour = &params.addToggle ("console.colour", "Console colour", true);

    tbLevel = &params.add ("tb.level", "Talkback level", -60.0f, 10.0f, 0.0f);
    for (int k = 0; k < numTalkbackAux; ++k)
    {
        const auto n = juce::String (firstTalkbackAux + k + 1);
        tbToAux[(size_t) k] = &params.addToggle ("tb.aux" + n, "Talkback to aux " + n, k == 0);   // Aux 5 to start with
    }
    tbDim = &params.addToggle ("tb.dim", "Dim monitors while talking", true);
    tbThroughLimiter = &params.addToggle ("tb.limiter", "Talkback through the TB limiter", true);
    tbLimInput = &params.add ("tbl.input", "TB limiter input", 0.0f, 30.0f, 10.0f);
    tbLimBlend = &params.add ("tbl.blend", "TB limiter blend", 0.0f, 100.0f, 100.0f);
    tbLimMakeup = &params.add ("tbl.makeup", "TB limiter makeup", -12.0f, 24.0f, 6.0f);
    for (auto* strips : { &inputStrips, &returnStrips })
        for (auto* s : *strips)
            s->colour = preampColour;

    // Stereo links, made last so every strip control exists.
    const auto makeLinks = [this] (juce::OwnedArray<ChannelStrip>& strips, auto& links, const juce::String& bank)
    {
        for (int k = 0; k < (int) links.size(); ++k)
        {
            auto& a = *strips[2 * k];
            auto& b = *strips[2 * k + 1];
            const auto pair = juce::String (2 * k + 1) + "-" + juce::String (2 * k + 2);
            links[(size_t) k] = &params.addToggle ("link." + a.idPrefix, bank + " " + pair + " link", false);
            stripLinks.add (new StripLink (params, a, b, *links[(size_t) k]));
        }
    };
    makeLinks (inputStrips, inputLink, "Input");
    makeLinks (returnStrips, returnLink, "Return");

    for (int j = 0; j < numAuxPairs; ++j)
    {
        const int a = 2 * j, b = 2 * j + 1;
        auxLink[(size_t) j] = &params.addToggle ("link.aux" + juce::String (a + 1), "Aux " + juce::String (a + 1) + "-" + juce::String (b + 1) + " link", false);
        auxLink[(size_t) j]->addListener (this);
        stripLinks.add (new StripLink (*auxLink[(size_t) j], { { auxMaster[(size_t) a], auxMaster[(size_t) b] }, { auxMain[(size_t) a], auxMain[(size_t) b] } }));
        for (auto* s : inputStrips)  s->auxLink[(size_t) j] = auxLink[(size_t) j];
        for (auto* s : returnStrips) s->auxLink[(size_t) j] = auxLink[(size_t) j];
    }
    resetBusNames();
}

void AudioEngine::resetBusNames()
{
    for (int k = 0; k < Config::numSubs; ++k) subNames[(size_t) k] = defaultSubName (k);
    for (int k = 0; k < Config::numAux; ++k)  auxNames[(size_t) k] = defaultAuxName (k);
}

juce::StringArray AudioEngine::setAuxLink (int pair, bool on)
{
    juce::StringArray notStereo;
    if (! juce::isPositiveAndBelow (pair, numAuxPairs))
        return notStereo;

    auto& link = *auxLink[(size_t) pair];
    const bool linking = on && ! link.on();
    link.set (on ? 1.0f : 0.0f);
    if (linking)
    {
        // The even send knob turns into the pair's pan: start every channel where its main pan is, so a
        // linked channel pair panned left/right sends in stereo. (Set after the link, so linked channels mirror.)
        for (auto* strips : { &inputStrips, &returnStrips })
            for (auto* s : *strips)
                s->controls.auxLevel[2 * pair + 1]->set ((s->controls.pan->get() + 1.0f) * 0.5f);
    }

    auto& chain = *auxInserts[2 * pair];
    for (int i = 0; i < InsertChain::numSlots; ++i)
        if (chain.isStereoRefused (i))
            notStereo.add (chain.getDescription (i).name);
    return notStereo;
}

void AudioEngine::paramChanged (Param& p)
{
    for (int j = 0; j < numAuxPairs; ++j)
        if (&p == auxLink[(size_t) j])
            auxInserts[2 * j]->setStereoOnly (p.on());
}

AudioEngine::~AudioEngine()
{
    for (auto* l : auxLink)
        l->removeListener (this);
    deviceManager.removeChangeListener (this);
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
        for (int t = 0; t < Config::numTracks; ++t)
            setInterfaceInput (Patch::tapeIn + t, routing->getIntAttribute ("tapeIn" + juce::String (t + 1), -1));
        for (int r = 0; r < Config::numReturnStrips; ++r)
            setInterfaceInput (Patch::returnIn + r, routing->getIntAttribute ("returnIn" + juce::String (r + 1), -1));
        setInterfaceInput (Patch::talkbackIn, routing->getIntAttribute ("talkbackIn", -1));
        setInterfaceInput (Patch::tbLimiterIn, routing->getIntAttribute ("tbLimiterIn", -1));
        setInterfaceInput (Patch::tbSend, routing->getIntAttribute ("tbSend", -1));
        for (int k = 0; k < Patch::numInterfaceOuts; ++k)
            setInterfaceInput (Patch::interfaceOut + k, routing->getIntAttribute ("interfaceOut" + juce::String (k + 1), -1));
        setMainOutput (0, routing->getIntAttribute ("mainL", 0));
        setMainOutput (1, routing->getIntAttribute ("mainR", 1));
        mainDevice = routing->getStringAttribute ("mainDevice");
        setMonitorOutput (0, routing->getIntAttribute ("monitorL", -1));
        setMonitorOutput (1, routing->getIntAttribute ("monitorR", -1));
        setRecordOffset (routing->getIntAttribute ("recordOffset", 0));
        for (int k = 0; k < Config::numAux; ++k)
            setAuxOutput (k, routing->getIntAttribute ("aux" + juce::String (k + 1), -1));
        for (int k = 0; k < Config::numSubs; ++k)
            for (int side = 0; side < 2; ++side)
                setSubOutput (k, side, routing->getIntAttribute ("sub" + juce::String (k + 1) + (side == 0 ? "L" : "R"), -1));
    }

    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);
    changeListenerCallback (&deviceManager);
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || device->getName() == mainDevice)
        return;

    // A different interface (or none recorded yet): main goes to its outputs 1 and 2, switched on.
    mainDevice = device->getName();
    const int numOuts = device->getOutputChannelNames().size();
    setMainOutput (0, 0);
    setMainOutput (1, numOuts > 1 ? 1 : 0);

    auto setup = deviceManager.getAudioDeviceSetup();
    bool missing = false;
    for (int ch = 0; ch < juce::jmin (2, numOuts); ++ch)
        if (! setup.outputChannels[ch])
        {
            setup.outputChannels.setBit (ch);
            missing = true;
        }
    if (missing)
    {
        setup.useDefaultOutputChannels = false;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

std::unique_ptr<juce::XmlElement> AudioEngine::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("AUDIOENGINE");

    if (auto device = deviceManager.createStateXml())
        xml->addChildElement (device.release());

    auto* routing = xml->createNewChildElement ("ROUTING");
    for (int c = 0; c < numConsoleInputs; ++c)
        routing->setAttribute ("in" + juce::String (c + 1), getInputSource (c));
    for (int t = 0; t < Config::numTracks; ++t)
        if (getInterfaceInput (Patch::tapeIn + t) >= 0)
            routing->setAttribute ("tapeIn" + juce::String (t + 1), getInterfaceInput (Patch::tapeIn + t));
    for (int r = 0; r < Config::numReturnStrips; ++r)
        if (getInterfaceInput (Patch::returnIn + r) >= 0)
            routing->setAttribute ("returnIn" + juce::String (r + 1), getInterfaceInput (Patch::returnIn + r));
    for (auto [dest, key] : { std::pair { Patch::talkbackIn, "talkbackIn" }, std::pair { Patch::tbLimiterIn, "tbLimiterIn" }, std::pair { Patch::tbSend, "tbSend" } })
        if (getInterfaceInput (dest) >= 0)
            routing->setAttribute (key, getInterfaceInput (dest));
    for (int k = 0; k < Patch::numInterfaceOuts; ++k)
        if (getInterfaceInput (Patch::interfaceOut + k) >= 0)
            routing->setAttribute ("interfaceOut" + juce::String (k + 1), getInterfaceInput (Patch::interfaceOut + k));
    routing->setAttribute ("mainL", getMainOutput (0));
    routing->setAttribute ("mainR", getMainOutput (1));
    routing->setAttribute ("mainDevice", mainDevice);
    routing->setAttribute ("monitorL", getMonitorOutput (0));
    routing->setAttribute ("monitorR", getMonitorOutput (1));
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
        setInterfaceInput (Patch::channelIn + consoleChannel, deviceInput);
}

int AudioEngine::getInputSource (int consoleChannel) const
{
    return juce::isPositiveAndBelow (consoleChannel, numConsoleInputs) ? getInterfaceInput (Patch::channelIn + consoleChannel) : -1;
}

void AudioEngine::setInterfaceInput (int dest, int deviceInput)
{
    if (juce::isPositiveAndBelow (dest, Patch::numDestinations))
        interfaceSource[(size_t) dest].store (juce::jlimit (-1, maxDeviceChannels - 1, deviceInput));
}

int AudioEngine::getInterfaceInput (int dest) const
{
    return juce::isPositiveAndBelow (dest, Patch::numDestinations) ? interfaceSource[(size_t) dest].load() : -1;
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

void AudioEngine::setMonitorOutput (int side, int deviceOutput)
{
    if (side == 0 || side == 1)
        monitorOutput[(size_t) side].store (juce::jlimit (-1, maxDeviceChannels - 1, deviceOutput));
}

int AudioEngine::getMonitorOutput (int side) const
{
    return (side == 0 || side == 1) ? monitorOutput[(size_t) side].load() : -1;
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
    oscillator.prepare (currentSampleRate, oscFreq->get());
    tbLimiter.prepare (currentSampleRate);
    talkbackGain.prepare (currentSampleRate, 0.0f);

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
    monitorSolo.prepare (currentSampleRate, 0.0f, 10.0f);
    monitorGainS.prepare (currentSampleRate, monitorGain (monitorLevel->get()));
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
        lastSubMain[(size_t) k] = subMain[(size_t) k]->on() ? 1.0f : 0.0f;
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
    const float* interfaceIn[Patch::numDestinations] {};
    for (int d = 0; d < Patch::numDestinations; ++d)
    {
        const int dev = interfaceSource[(size_t) d].load (std::memory_order_relaxed);
        const int slot = juce::isPositiveAndBelow (dev, maxDeviceChannels) ? inputSlot[(size_t) dev] : -1;
        interfaceIn[d] = juce::isPositiveAndBelow (slot, numInputs) ? inputs[slot] : nullptr;
    }
    const auto interfaceAt = [&] (int dest, int start) { return interfaceIn[dest] != nullptr ? interfaceIn[dest] + start : nullptr; };

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
    float* monitorOut[2] {};
    if (mainLeft == nullptr && mainRight == nullptr)   // not when exporting
        for (int side = 0; side < 2; ++side)
            monitorOut[side] = outFor (monitorOutput[(size_t) side].load (std::memory_order_relaxed));

    bool anySolo = false;
    for (auto* s : inputStrips)  anySolo = anySolo || s->controls.solo->on();
    for (auto* s : returnStrips) anySolo = anySolo || s->controls.solo->on();

    const float mainT = Dsp::faderGain (mainFader->get());

    const float* tapeInPtrs[Config::numTracks];
    float* tapeOutPtrs[Config::numTracks];

    BusPointers buses;
    buses.mainL = mixL.data();
    buses.mainR = mixR.data();
    buses.soloL = soloL.data();
    buses.soloR = soloR.data();
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

    // The oscillator only runs while a cable carries it.
    const bool osc = ((patchedSources >> Patch::oscillator) & 1u) != 0;
    const int oscTypeNow = juce::roundToInt (oscType->get());
    const float oscFreqNow = oscFreq->get();
    const float oscGain = oscillatorMuted.load (std::memory_order_relaxed) ? 0.0f : Dsp::dbToGain (oscLevel->get());

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        if (osc)
            oscillator.process (oscOut.data(), n, oscTypeNow, oscFreqNow, oscGain);
        juce::FloatVectorOperations::clear (mixL.data(), n);
        juce::FloatVectorOperations::clear (mixR.data(), n);
        juce::FloatVectorOperations::clear (soloL.data(), n);
        juce::FloatVectorOperations::clear (soloR.data(), n);
        for (int k = 0; k < Config::numSubs; ++k)
        {
            juce::FloatVectorOperations::clear (subL[(size_t) k].data(), n);
            juce::FloatVectorOperations::clear (subR[(size_t) k].data(), n);
        }
        for (auto& a : aux)
            juce::FloatVectorOperations::clear (a.data(), n);

        // Input bank: interface input, plus any cables patched into the channel strip input.
        for (int c = 0; c < Config::numInputStrips; ++c)
            inputStrips[c]->process (patchedInput (Patch::channelIn + c, nullptr, interfaceAt (Patch::channelIn + c, start), 0, n),
                                     inputDirect[(size_t) c].data(), buses, n);

        // Tape: channel direct outs are normalled to tape ins; a cable (or an interface input) breaks the normal.
        for (int t = 0; t < Config::numTracks; ++t)
        {
            const float* normal = t < Config::numInputStrips ? inputDirect[(size_t) t].data() : nullptr;
            tapeInPtrs[t] = patchedInput (Patch::tapeIn + t, normal, interfaceAt (Patch::tapeIn + t, start), 1, n);
            tapeOutPtrs[t] = tapeOut[(size_t) t].data();
        }
        tape.process (tapeInPtrs, tapeOutPtrs, n);

        // Return bank: tape outs are normalled to return strip ins; a cable (or an interface input) breaks the normal.
        // An armed track's normal is open (its tape out carries the input, already heard on the channel strip).
        const bool exportingNow = tape.exporting.load (std::memory_order_relaxed);
        for (int r = 0; r < Config::numReturnStrips; ++r)
        {
            const bool armed = tape.arm[(size_t) r]->on() && ! exportingNow;
            returnStrips[r]->process (patchedInput (Patch::returnIn + r, armed ? nullptr : tapeOut[(size_t) r].data(),
                                                    interfaceAt (Patch::returnIn + r, start), 2, n),
                                      returnDirect[(size_t) r].data(), buses, n);
        }

        // Latency compensation: the dry mix, every sub and every active aux return are delayed to line up
        // with the slowest plug-in chain (latencies as of the previous block).
        bool auxLinked[numAuxPairs];
        for (int j = 0; j < numAuxPairs; ++j)
            auxLinked[j] = auxLink[(size_t) j]->on();
        const auto idleReturn = [&] (int k) { return k % 2 == 1 && auxLinked[k / 2]; };   // the even aux of a linked pair

        int compensation = 0;
        for (auto* c : subInserts) compensation = juce::jmax (compensation, c->getLatency());
        for (int k = 0; k < Config::numAux; ++k)
            if (! idleReturn (k) && auxMain[(size_t) k]->on() && auxInserts[k]->isActive())
                compensation = juce::jmax (compensation, auxInserts[k]->getLatency());
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

            // Into the main mix when assigned, compensated. (The sub buffers themselves stay undelayed for the patch bay.)
            const int d = compensation - subInserts[k]->getLatency();
            std::memcpy (returnL.data(), l, sizeof (float) * (size_t) n);
            std::memcpy (returnR.data(), r, sizeof (float) * (size_t) n);
            subDelay[(size_t) k * 2].process (returnL.data(), n, d);
            subDelay[(size_t) k * 2 + 1].process (returnR.data(), n, d);
            const float toMain = subMain[(size_t) k]->on() ? 1.0f : 0.0f;
            auto& last = lastSubMain[(size_t) k];
            if (last != 0.0f || toMain != 0.0f)
            {
                Dsp::addRamped (mixL.data(), returnL.data(), last, toMain, n);
                Dsp::addRamped (mixR.data(), returnR.data(), last, toMain, n);
            }
            last = toMain;
        }

        // Aux masters -> optional aux outputs (and the patch bay).
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
        }

        // Aux returns: inserts -> return pan -> main, while assigned to main. A linked pair returns in stereo
        // through the odd aux's inserts (odd = left, even = right); the even aux's own return sits idle.
        for (int k = 0; k < Config::numAux; ++k)
        {
            auto& gl = auxReturnGainL[(size_t) k];
            auto& gr = auxReturnGainR[(size_t) k];
            if (idleReturn (k))
            {
                gl.current = gr.current = 0.0f;
                continue;
            }

            const bool toMain = auxMain[(size_t) k]->on();
            if (! toMain && gl.settled (0.0f) && gr.settled (0.0f))
                continue;

            const bool stereo = k % 2 == 0 && auxLinked[k / 2];
            std::memcpy (returnL.data(), aux[(size_t) k].data(), sizeof (float) * (size_t) n);
            std::memcpy (returnR.data(), aux[(size_t) (stereo ? k + 1 : k)].data(), sizeof (float) * (size_t) n);
            auxInserts[k]->process (returnL.data(), returnR.data(), n);
            // Return pan is a balance: centre leaves both sides at unity, turning it lowers the other side.
            const float pan = auxReturnPan[(size_t) k]->get();
            const float targetL = toMain ? juce::jmin (1.0f, 1.0f - pan) : 0.0f;
            const float targetR = toMain ? juce::jmin (1.0f, 1.0f + pan) : 0.0f;
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

        // Talkback: the mic through the TB limiter (while the limiter's input is normalled to it), into the
        // chosen aux outputs. After the aux returns, so it never reaches the main mix through an effect.
        {
            const float* mic = patchedInput (Patch::talkbackIn, nullptr, interfaceAt (Patch::talkbackIn, start), 3, n);
            const bool throughLimiter = tbThroughLimiter->on();
            const float* limIn = patchedInput (Patch::tbLimiterIn, throughLimiter ? mic : nullptr, interfaceAt (Patch::tbLimiterIn, start), 3, n);
            if (limIn != nullptr)
                tbLimiter.process (limIn, tbLimOut.data(), n, tbLimInput->get(), tbLimBlend->get() * 0.01f, tbLimMakeup->get());
            else
                juce::FloatVectorOperations::clear (tbLimOut.data(), n);

            // TB Send: normalled from the limiter while the mic feeds it, otherwise straight from TB In.
            const bool normalled = patch[(size_t) Patch::tbLimiterIn].load (std::memory_order_relaxed) == 0
                                   && interfaceIn[Patch::tbLimiterIn] == nullptr;
            const float* sendNormal = throughLimiter && normalled ? (mic != nullptr ? tbLimOut.data() : nullptr) : mic;
            const float* talk = patchedInput (Patch::tbSend, sendNormal, interfaceAt (Patch::tbSend, start), 3, n);
            const float target = talkbackOn.load (std::memory_order_relaxed) ? monitorGain (tbLevel->get()) : 0.0f;
            if (talk != nullptr && ! (target == 0.0f && talkbackGain.settled (0.0f)))
            {
                for (int i = 0; i < n; ++i)
                {
                    const float x = talk[i] * talkbackGain.next (target);
                    for (int k = 0; k < numTalkbackAux; ++k)
                        if (tbToAux[(size_t) k]->on())
                            aux[(size_t) (firstTalkbackAux + k)][(size_t) i] += x;
                }
            }
            else
            {
                talkbackGain.current = target;
            }
        }

        // Aux outputs (after the talkback).
        for (int k = 0; k < Config::numAux; ++k)
            if (auxOut[k] != nullptr)
                juce::FloatVectorOperations::add (auxOut[k] + start, aux[(size_t) k].data(), n);

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

        // Monitor outs: main, or the solo bus while anything is soloed (a short crossfade between them),
        // at the monitor level.
        const float soloT = anySolo ? 1.0f : 0.0f;
        // Talking dims the control room, as on a real console, so the talkback mic does not feed back.
        const bool dimmed = talkbackOn.load (std::memory_order_relaxed) && tbDim->on();
        const float levelT = monitorGain (monitorLevel->get()) * (dimmed ? Dsp::dbToGain (talkbackDimDb) : 1.0f);
        if (monitorOut[0] != nullptr || monitorOut[1] != nullptr)
        {
            float* solo[2] = { soloL.data(), soloR.data() };
            for (int i = 0; i < n; ++i)
            {
                const float b = monitorSolo.next (soloT), g = monitorGainS.next (levelT);
                for (int side = 0; side < 2; ++side)
                    if (monitorOut[side] != nullptr)
                        monitorOut[side][start + i] += g * (mix[side][i] + b * (solo[side][i] - mix[side][i]));
            }
        }
        else
        {
            // Nothing to fade: jump.
            monitorSolo.prepare (currentSampleRate, soloT, 10.0f);
            monitorGainS.prepare (currentSampleRate, levelT);
        }

        // Interface outs on the patch bay: whatever is patched into one (any output, or an interface input)
        // is added to what that output already carries. Not while exporting.
        if (mainLeft == nullptr && mainRight == nullptr)
            for (int k = 0; k < Patch::numInterfaceOuts; ++k)
            {
                const int d = Patch::interfaceOut + k;
                if (patch[(size_t) d].load (std::memory_order_relaxed) == 0 && interfaceIn[d] == nullptr)
                    continue;
                if (float* out = outFor (k))
                    if (const float* signal = patchedInput (d, nullptr, interfaceAt (d, start), 4, n))
                        juce::FloatVectorOperations::add (out + start, signal, n);
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
    if (! juce::isPositiveAndBelow (dest, Patch::numDestinations))
        return;
    constexpr auto all = Patch::numSources >= 64 ? ~std::uint64_t (0) : (std::uint64_t (1) << (Patch::numSources % 64)) - 1;
    sources &= all;
    if (dest == Patch::tbLimiterIn)
        sources &= ~(std::uint64_t (1) << Patch::tbLimiterOut);   // the TB limiter cannot be patched into itself
    patch[(size_t) dest].store (sources);
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
    if (s == Patch::oscillator) return oscOut.data();   // made at the start of every block
    if (s == Patch::tbLimiterOut) return phase >= 4 ? tbLimOut.data() : previous;
    if (s < Patch::returnOut) return phase >= 1 ? inputDirect[(size_t) s].data() : previous;
    if (s < Patch::tapeOut)   return phase >= 3 ? returnDirect[(size_t) (s - Patch::returnOut)].data() : previous;
    if (s < Patch::auxOut)    return phase >= 2 ? tapeOut[(size_t) (s - Patch::tapeOut)].data() : previous;
    if (s < Patch::subOut)    return phase >= 4 ? aux[(size_t) (s - Patch::auxOut)].data() : previous;
    if (s < Patch::mainL)     return phase >= 4 ? subMono[(size_t) (s - Patch::subOut)].data() : previous;
    return phase >= 4 ? (s == Patch::mainL ? mixL.data() : mixR.data()) : previous;
}

const float* AudioEngine::patchedInput (int dest, const float* normal, const float* interfaceIn, int phase, int n) noexcept
{
    const auto cables = patch[(size_t) dest].load (std::memory_order_relaxed);
    if (cables == 0)
        return interfaceIn != nullptr ? interfaceIn : normal;

    // A cable or an interface input breaks a normal; the interface input and the cables add up.
    float* d = patchInput[(size_t) dest].data();
    if (interfaceIn != nullptr)
        std::memcpy (d, interfaceIn, sizeof (float) * (size_t) n);
    else
        std::memset (d, 0, sizeof (float) * (size_t) n);

    for (int s = 0; s < Patch::numSources; ++s)
        if ((cables >> s) & 1u)
            juce::FloatVectorOperations::add (d, patchSource (s, phase), n);
    return d;
}
