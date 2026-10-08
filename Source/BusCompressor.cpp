#include "BusCompressor.h"

namespace
{
    float coeff (double seconds, double sr) noexcept { return (float) std::exp (-1.0 / (seconds * sr)); }
}

BusCompressor::BusCompressor (ParamSet& params)
{
    in        = &params.addToggle ("buscomp.in", "Bus compressor in", false);
    threshold = &params.add ("buscomp.threshold", "Bus compressor threshold", -20.0f, 20.0f, 0.0f);
    makeup    = &params.add ("buscomp.makeup", "Bus compressor make up", -5.0f, 15.0f, 0.0f);
    attack    = &params.addStepped ("buscomp.attack", "Bus compressor attack", 6, 3);     // 3 ms
    release   = &params.addStepped ("buscomp.release", "Bus compressor release", 6, 2);   // 0.4 s
    ratio     = &params.addStepped ("buscomp.ratio", "Bus compressor ratio", 6, 1);       // 2:1
    hpf       = &params.addStepped ("buscomp.hpf", "Bus compressor side-chain HPF", 6, 0);
}

void BusCompressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    env = gr = grFast = grSlow = 0.0f;
    scL.reset();
    scR.reset();
    scHpfIndex = -1;
    wet.prepare (sr, in->on() ? 1.0f : 0.0f, 10.0f);
    makeupGain.prepare (sr, Dsp::dbToGain (makeup->get()));
}

void BusCompressor::process (float* left, float* right, int n) noexcept
{
    const float wetT = in->on() ? 1.0f : 0.0f;
    if (wetT == 0.0f && wet.settled (0.0f))
    {
        env = gr = grFast = grSlow = 0.0f;   // fully out: untouched audio
        return;
    }

    const auto idx = [] (Param* p) { return juce::jlimit (0, 5, juce::roundToInt (p->get())); };

    const int hpfIdx = idx (hpf);
    if (hpfIdx != scHpfIndex)
    {
        if (hpfIdx > 0)
        {
            scL.setHighPass (sr, hpfHz[hpfIdx], 0.7071);
            scR.copyCoefficients (scL);
        }
        scL.reset();
        scR.reset();
        scHpfIndex = hpfIdx;
    }

    const float thr = thresholdDb (threshold->get());
    const float slope = 1.0f - 1.0f / ratios[idx (ratio)];
    constexpr float knee = 6.0f;
    const float rel = releaseS[idx (release)];
    const bool autoRelease = rel <= 0.0f;
    const float aA = coeff (juce::jmax (attackMs[idx (attack)] * 0.001, 0.00005), sr);
    const float aR = coeff (juce::jmax ((double) rel, 0.01), sr);
    const float aFast = coeff (0.1, sr), aSlow = coeff (1.2, sr);
    const float dec = coeff (0.001, sr);
    const float makeupT = Dsp::dbToGain (makeup->get());
    const bool sideChainHpf = hpfIdx > 0;

    float blockMax = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        const float dl = sideChainHpf ? scL.process (l) : l;
        const float dr = sideChainHpf ? scR.process (r) : r;

        // Stereo-linked peak detector.
        env = juce::jmax (juce::jmax (std::abs (dl), std::abs (dr)), env * dec);
        const float over = 20.0f * std::log10 (env + 1.0e-9f) - thr;

        float target = 0.0f;
        if (over > knee * 0.5f)
            target = over * slope;
        else if (over > -knee * 0.5f)
        {
            const float k = over + knee * 0.5f;
            target = slope * k * k / (2.0f * knee);
        }

        if (autoRelease)
        {
            grFast = target > grFast ? aA * grFast + (1.0f - aA) * target : aFast * grFast + (1.0f - aFast) * target;
            grSlow = target > grSlow ? aA * grSlow + (1.0f - aA) * target : aSlow * grSlow + (1.0f - aSlow) * target;
            gr = 0.5f * (grFast + grSlow);
        }
        else
        {
            gr = target > gr ? aA * gr + (1.0f - aA) * target : aR * gr + (1.0f - aR) * target;
            grFast = grSlow = gr;
        }
        blockMax = juce::jmax (blockMax, gr);

        const float g = Dsp::dbToGain (-gr) * makeupGain.next (makeupT);
        const float w = wet.next (wetT);
        left[i] = l + (l * g - l) * w;
        right[i] = r + (r * g - r) * w;
    }

    auto current = grPeak.load (std::memory_order_relaxed);
    const float shown = blockMax * (wetT > 0.0f ? 1.0f : 0.0f);
    while (shown > current && ! grPeak.compare_exchange_weak (current, shown, std::memory_order_relaxed)) {}
}
