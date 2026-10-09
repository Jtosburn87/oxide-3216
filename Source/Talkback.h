#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>
#include "Dsp.h"

/** The talkback limiter, after the SSL 4000 E's listen mic compressor: a fixed, very fast and very hard
    compressor (about 30:1 above -24 dBFS, 0.2 ms attack, 120 ms release, with a soft clip behind it for
    anything quicker) that flattens a talkback mic and pumps hard on anything loud.
    Input drives it, Blend mixes the uncompressed signal back in (0 % dry .. 100 % compressed), Makeup sets
    the level after the blend. */
class TalkbackLimiter
{
public:
    static constexpr float thresholdDb = -24.0f, ratio = 30.0f;

    void prepare (double sampleRate) noexcept
    {
        attack = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.0002));
        release = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.12));
        env = 0.0f;
        inGain.prepare (sampleRate, 1.0f, 20.0f);
        mix.prepare (sampleRate, 1.0f, 20.0f);
        outGain.prepare (sampleRate, 1.0f, 20.0f);
    }

    /** inputDb, makeupDb: the knobs in dB; blend 0..1 (1 = all compressed). in and out may be the same. */
    void process (const float* in, float* out, int n, float inputDb, float blend, float makeupDb) noexcept
    {
        const float threshold = Dsp::dbToGain (thresholdDb), clipLevel = threshold * 2.0f;
        const float inT = Dsp::dbToGain (inputDb), outT = Dsp::dbToGain (makeupDb);
        float lowest = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i] * inGain.next (inT);
            const float a = std::abs (x);
            env += (a > env ? attack : release) * (a - env);
            const float g = env > threshold ? std::pow (threshold / env, 1.0f - 1.0f / ratio) : 1.0f;
            lowest = std::min (lowest, g);
            const float wet = clipLevel * std::tanh (x * g / clipLevel);
            const float b = mix.next (blend);
            out[i] = outGain.next (outT) * (b * wet + (1.0f - b) * x);
        }
        const float grDb = -20.0f * std::log10 (std::max (lowest, 1.0e-6f));
        auto current = reduction.load (std::memory_order_relaxed);
        while (grDb > current && ! reduction.compare_exchange_weak (current, grDb, std::memory_order_relaxed)) {}
    }

    /** Most gain reduction (dB) since the last call, for the meter. */
    float takeReduction() noexcept { return reduction.exchange (0.0f); }

private:
    float attack = 0.0f, release = 0.0f, env = 0.0f;
    Dsp::Smoothed inGain, mix, outGain;
    std::atomic<float> reduction { 0.0f };
};
