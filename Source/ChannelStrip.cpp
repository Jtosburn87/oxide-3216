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
    : idPrefix (prefix), name (defaultName)
{
    const auto id = [&] (const char* s) { return prefix + "." + s; };
    controls.trim  = &params.add (id ("trim"), "Trim", -20.0f, 30.0f, 0.0f);
    controls.hpf   = &params.add (id ("hpf"), "HPF", 20.0f, 300.0f, 20.0f, Param::Scale::log);
    controls.pan   = &params.add (id ("pan"), "Pan", -1.0f, 1.0f, 0.0f);
    controls.main  = &params.addToggle (id ("main"), "Main", true);
    controls.mute  = &params.addToggle (id ("mute"), "Mute", false);
    controls.solo  = &params.addToggle (id ("solo"), "Solo", false);
    controls.fader = &params.add (id ("fader"), "Fader", 0.0f, 100.0f, 80.0f);
}

float ChannelStrip::targetFader() const noexcept
{
    return controls.mute->on() ? 0.0f : Dsp::faderGain (controls.fader->get());
}

void ChannelStrip::targetPan (bool anySoloActive, float& l, float& r) const noexcept
{
    const bool toMain = controls.main->on() && (! anySoloActive || controls.solo->on());
    Dsp::panGains (controls.pan->get(), l, r);
    if (! toMain)
        l = r = 0.0f;
}

void ChannelStrip::prepare (double sr)
{
    sampleRate = sr;
    hp.reset();
    hpFreqSet = -1.0f;
    trimGain.prepare (sr, Dsp::dbToGain (controls.trim->get()));
    faderGain.prepare (sr, targetFader());
    float l, r;
    targetPan (false, l, r);
    leftGain.prepare (sr, l);
    rightGain.prepare (sr, r);
}

void ChannelStrip::process (const float* in, float* direct, float* mainL, float* mainR, int n, bool anySoloActive) noexcept
{
    const float trimT = Dsp::dbToGain (controls.trim->get());
    const float faderT = targetFader();
    float panL, panR;
    targetPan (anySoloActive, panL, panR);

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

    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float x = in != nullptr ? in[i] : 0.0f;
        x *= trimGain.next (trimT);
        if (hpOn)
            x = hp.process (x);
        x *= faderGain.next (faderT);

        direct[i] = x;
        pk = juce::jmax (pk, std::abs (x));
        mainL[i] += x * leftGain.next (panL);
        mainR[i] += x * rightGain.next (panR);
    }

    raisePeak (peak, pk);
}
