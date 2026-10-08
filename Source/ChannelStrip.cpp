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

ChannelStrip::ChannelStrip (ParamSet& params, const juce::String& prefix, const juce::String& defaultName)
    : idPrefix (prefix), name (defaultName),
      eq (params, prefix, 1), gate (params, prefix), comp (params, prefix)
{
    const auto id = [&] (const juce::String& s) { return prefix + "." + s; };
    controls.trim  = &params.add (id ("trim"), "Trim", -20.0f, 30.0f, 0.0f);
    controls.hpf   = &params.add (id ("hpf"), "HPF", 20.0f, 300.0f, 20.0f, Param::Scale::log);
    controls.pan   = &params.add (id ("pan"), "Pan", -1.0f, 1.0f, 0.0f);
    controls.order = &params.addStepped (id ("order"), "Chain order", (int) chainOrders.size(), 0);
    controls.main  = &params.addToggle (id ("main"), "Main", true);
    controls.mute  = &params.addToggle (id ("mute"), "Mute", false);
    controls.solo  = &params.addToggle (id ("solo"), "Solo", false);
    controls.fader = &params.add (id ("fader"), "Fader", 0.0f, 100.0f, 80.0f);

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

ChannelStrip::Targets ChannelStrip::targets (bool anySoloActive) const noexcept
{
    Targets t {};
    float l, r;
    Dsp::panGains (controls.pan->get(), l, r);

    // Solo in place: when anything is soloed, strips that are not soloed leave main and the subs.
    const bool heard = ! anySoloActive || controls.solo->on();
    const bool toMain = heard && controls.main->on();
    t.main[0] = toMain ? l : 0.0f;
    t.main[1] = toMain ? r : 0.0f;

    for (int k = 0; k < Config::numSubs; ++k)
    {
        const bool on = heard && controls.sub[k]->on();
        t.sub[k][0] = on ? l : 0.0f;
        t.sub[k][1] = on ? r : 0.0f;
    }

    for (int k = 0; k < Config::numAux; ++k)
        t.aux[k] = auxGain (controls.auxLevel[k]->get());
    return t;
}

void ChannelStrip::prepare (double sr)
{
    sampleRate = sr;
    hp.reset();
    hpFreqSet = -1.0f;
    eq.prepare (sr);
    gate.prepare (sr);
    comp.prepare (sr);
    trimGain.prepare (sr, Dsp::dbToGain (controls.trim->get()));
    faderGain.prepare (sr, targetFader());

    const auto t = targets (false);
    lastMain[0] = t.main[0];
    lastMain[1] = t.main[1];
    for (int k = 0; k < Config::numSubs; ++k)
    {
        lastSub[k][0] = t.sub[k][0];
        lastSub[k][1] = t.sub[k][1];
    }
    for (int k = 0; k < Config::numAux; ++k)
        lastAux[k] = t.aux[k];
}

void ChannelStrip::process (const float* in, float* direct, const BusPointers& buses, int n, bool anySoloActive) noexcept
{
    const float trimT = Dsp::dbToGain (controls.trim->get());
    const float faderT = targetFader();

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

    eq.update();
    gate.beginBlock();
    comp.beginBlock();
    const bool eqOn = ! eq.isFlat();
    const bool gateOn = ! gate.isOff() || ! gate.isIdle();
    const bool compOn = comp.isIn();
    const auto order = chain();

    float pk = 0.0f, inPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float x = in != nullptr ? in[i] : 0.0f;
        x *= trimGain.next (trimT);
        inPk = juce::jmax (inPk, std::abs (x));
        if (hpOn)   x = hp.process (x);
        for (const int stage : order)
        {
            if (stage == eqStage)        { if (eqOn)   x = eq.process (0, x); }
            else if (stage == gateStage) { if (gateOn) x = gate.process (x); }
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
    const auto t = targets (anySoloActive);

    Dsp::addRamped (buses.mainL, direct, lastMain[0], t.main[0], n);
    Dsp::addRamped (buses.mainR, direct, lastMain[1], t.main[1], n);
    lastMain[0] = t.main[0];
    lastMain[1] = t.main[1];

    for (int k = 0; k < Config::numSubs; ++k)
    {
        if (buses.subL[k] != nullptr) Dsp::addRamped (buses.subL[k], direct, lastSub[k][0], t.sub[k][0], n);
        if (buses.subR[k] != nullptr) Dsp::addRamped (buses.subR[k], direct, lastSub[k][1], t.sub[k][1], n);
        lastSub[k][0] = t.sub[k][0];
        lastSub[k][1] = t.sub[k][1];
    }

    for (int k = 0; k < Config::numAux; ++k)
    {
        if (buses.aux[k] != nullptr)
            Dsp::addRamped (buses.aux[k], controls.auxPre[k]->on() ? pre.data() : direct, lastAux[k], t.aux[k], n);
        lastAux[k] = t.aux[k];
    }
}
