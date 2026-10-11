#include "ChannelStrip.h"

namespace
{
    constexpr float hpfOffBelow = 20.5f;   // knob fully left = filter out

    void raisePeak (std::atomic<float>& peak, float value) noexcept
    {
        auto current = peak.load (std::memory_order_relaxed);
        while (value > current && ! peak.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
    }
}

ChannelStrip::ChannelStrip (ParamSet& params, const juce::String& prefix, const juce::String& defaultName, bool withDeEsser, float defaultFader)
    : idPrefix (prefix), name (defaultName),
      eq (params, prefix, 1, true), gate (params, prefix), comp (params, prefix)
{
    const auto id = [&] (const juce::String& s) { return prefix + "." + s; };
    controls.trim  = &params.add (id ("trim"), "Trim", -20.0f, 30.0f, 0.0f);
    controls.hpf   = &params.add (id ("hpf"), "HPF", 20.0f, 300.0f, 20.0f, Param::Scale::log);
    controls.pan   = &params.add (id ("pan"), "Pan", -1.0f, 1.0f, 0.0f);
    controls.order = &params.addStepped (id ("order"), "Chain order", (int) chainOrders.size(), 0);
    controls.eqIn  = &params.addToggle (id ("eq.in"), "EQ in", true);
    controls.gateIn = &params.addToggle (id ("gate.in"), "Gate in", true);
    if (withDeEsser)
        deEsser = std::make_unique<DeEsser> (params, prefix);
    controls.main  = &params.addToggle (id ("main"), "Main", true);
    controls.mute  = &params.addToggle (id ("mute"), "Mute", false);
    controls.solo  = &params.addToggle (id ("solo"), "Solo", false);
    controls.fader = &params.add (id ("fader"), "Fader", 0.0f, 100.0f, defaultFader);

    for (int k = 0; k < Config::numSubs; ++k)
        controls.sub[k] = &params.addToggle (id ("sub" + juce::String (k + 1)), "Sub " + juce::String (k + 1), false);

    for (int k = 0; k < Config::numAux; ++k)
    {
        controls.auxLevel[k] = &params.add (id ("aux" + juce::String (k + 1)), "Aux " + juce::String (k + 1), 0.0f, 1.0f, 0.0f);
        controls.auxPre[k] = &params.addToggle (id ("aux" + juce::String (k + 1) + ".pre"), "Aux " + juce::String (k + 1) + " pre", false);
    }
}

void ChannelStrip::setChain (const Chain& c)
{
    for (size_t i = 0; i < chainOrders.size(); ++i)
        if (chainOrders[i] == c)
            controls.order->set ((float) i);
}

float ChannelStrip::targetFader() const noexcept
{
    return controls.mute->on() ? 0.0f : Dsp::faderGain (controls.fader->get());
}

ChannelStrip::Targets ChannelStrip::targets() const noexcept
{
    Targets t {};
    float l, r;
    Dsp::panGains (controls.pan->get(), l, r);

    const bool toMain = controls.main->on();
    t.main[0] = toMain ? l : 0.0f;
    t.main[1] = toMain ? r : 0.0f;

    // Solo never touches main or the subs: a soloed strip (after fader and pan) goes to the solo bus,
    // which the monitor outs carry instead of the main mix.
    const bool soloed = controls.solo->on();
    t.solo[0] = soloed ? l : 0.0f;
    t.solo[1] = soloed ? r : 0.0f;

    for (int k = 0; k < Config::numSubs; ++k)
    {
        const bool on = controls.sub[k]->on();
        t.sub[k][0] = on ? l : 0.0f;
        t.sub[k][1] = on ? r : 0.0f;
    }

    for (int k = 0; k < Config::numAux; k += 2)
    {
        if (isAuxLinked (k / 2))
        {
            // Stereo send: odd knob = level, even knob = pan.
            float sl, sr;
            Dsp::panGains (controls.auxLevel[k + 1]->get() * 2.0f - 1.0f, sl, sr);
            const float level = auxGain (controls.auxLevel[k]->get());
            t.aux[k] = level * sl;
            t.aux[k + 1] = level * sr;
        }
        else
        {
            t.aux[k] = auxGain (controls.auxLevel[k]->get());
            t.aux[k + 1] = auxGain (controls.auxLevel[k + 1]->get());
        }
    }
    return t;
}

void ChannelStrip::prepare (double sr)
{
    sampleRate = sr;
    hp.reset();
    hpFreqSet = -1.0f;
    eq.prepare (sr);
    gate.prepare (sr);
    if (deEsser != nullptr)
        deEsser->prepare (sr);
    comp.prepare (sr);
    trimGain.prepare (sr, Dsp::dbToGain (controls.trim->get()));
    preamp.prepare (sr, controls.trim->get(), isColoured());
    faderGain.prepare (sr, targetFader());

    const auto t = targets();
    lastMain[0] = t.main[0];
    lastMain[1] = t.main[1];
    lastSolo[0] = t.solo[0];
    lastSolo[1] = t.solo[1];
    for (int k = 0; k < Config::numSubs; ++k)
    {
        lastSub[k][0] = t.sub[k][0];
        lastSub[k][1] = t.sub[k][1];
    }
    for (int k = 0; k < Config::numAux; ++k)
        lastAux[k] = t.aux[k];
}

void ChannelStrip::process (const float* in, float* direct, const BusPointers& buses, int n) noexcept
{
    const float trimT = Dsp::dbToGain (controls.trim->get());
    const float faderT = targetFader();
    preamp.setTrim (controls.trim->get());
    const bool coloured = isColoured();

    const float hpf = controls.hpf->get();
    const bool hpOn = hpf >= hpfOffBelow;
    if (hpOn && hpf != hpFreqSet)
    {
        hp.setHighPass (sampleRate, hpf, 0.7071);
        hpFreqSet = hpf;
    }
    else if (! hpOn && hpFreqSet > 0.0f)
    {
        hp.reset();
        hpFreqSet = -1.0f;
    }

    // EQ and gate In switches: out = bypassed. The EQ comes back in with clear filters.
    const bool eqIn = controls.eqIn->on(), gateIn = controls.gateIn->on();
    if (eqIn && ! eqWasIn)
        eq.prepare (sampleRate);
    eqWasIn = eqIn;
    eq.update();
    // Return strips: the gate slot is either the gate or the de-esser.
    const bool deEssMode = deEsser != nullptr && deEsser->on->on();
    const bool deEssOn = deEssMode && ! deEsser->bypass->on();
    if (deEssOn)
        deEsser->beginBlock();
    gate.beginBlock();
    comp.beginBlock();
    const bool eqOn = eqIn && ! eq.isFlat();
    const bool gateOn = gateIn && ! deEssMode && (! gate.isOff() || ! gate.isIdle());
    const bool compOn = comp.isIn();
    const auto order = chain();

    float pk = 0.0f, inPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float x = in != nullptr ? in[i] : 0.0f;
        x *= trimGain.next (trimT);
        x = preamp.process (x, coloured);
        inPk = juce::jmax (inPk, std::abs (x));
        if (hpOn)   x = hp.process (x);
        for (const int stage : order)
        {
            if (stage == eqStage)        { if (eqOn)   x = eq.process (0, x); }
            else if (stage == gateStage) { if (gateOn) x = gate.process (x); else if (deEssOn) x = deEsser->process (x); }
            else                         { if (compOn) x = comp.process (x); }
        }

        pre[(size_t) i] = x;
        x *= faderGain.next (faderT);
        direct[i] = x;
        pk = juce::jmax (pk, std::abs (x));
    }
    comp.finishBlock();
    raisePeak (peak, pk);
    raisePeak (inputPeak, inPk);

    // Bus sends, each with its gain ramped across the block.
    const auto t = targets();

    Dsp::addRamped (buses.mainL, direct, lastMain[0], t.main[0], n);
    Dsp::addRamped (buses.mainR, direct, lastMain[1], t.main[1], n);
    lastMain[0] = t.main[0];
    lastMain[1] = t.main[1];

    if (buses.soloL != nullptr && (lastSolo[0] != 0.0f || t.solo[0] != 0.0f)) Dsp::addRamped (buses.soloL, direct, lastSolo[0], t.solo[0], n);
    if (buses.soloR != nullptr && (lastSolo[1] != 0.0f || t.solo[1] != 0.0f)) Dsp::addRamped (buses.soloR, direct, lastSolo[1], t.solo[1], n);
    lastSolo[0] = t.solo[0];
    lastSolo[1] = t.solo[1];

    for (int k = 0; k < Config::numSubs; ++k)
    {
        if (buses.subL[k] != nullptr) Dsp::addRamped (buses.subL[k], direct, lastSub[k][0], t.sub[k][0], n);
        if (buses.subR[k] != nullptr) Dsp::addRamped (buses.subR[k], direct, lastSub[k][1], t.sub[k][1], n);
        lastSub[k][0] = t.sub[k][0];
        lastSub[k][1] = t.sub[k][1];
    }

    for (int k = 0; k < Config::numAux; ++k)
    {
        const int preSwitch = (k % 2 == 1 && isAuxLinked (k / 2)) ? k - 1 : k;
        if (buses.aux[k] != nullptr)
            Dsp::addRamped (buses.aux[k], controls.auxPre[preSwitch]->on() ? pre.data() : direct, lastAux[k], t.aux[k], n);
        lastAux[k] = t.aux[k];
    }
}
