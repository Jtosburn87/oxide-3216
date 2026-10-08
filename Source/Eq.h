#pragma once

#include "Params.h"
#include "Dsp.h"

/** The console's 4-band sweepable EQ, used on every channel strip and as the group EQ on each sub.
    HF and LF are shelves, HMF and LMF are bells (Q 1). Gain +/-15 dB. A band at 0 dB is bypassed. */
class FourBandEq
{
public:
    enum Band { HF, HMF, LMF, LF, numBands };

    struct BandSpec
    {
        const char* id;
        const char* label;
        float minHz, maxHz, defHz;
        juce::uint32 capColour;
    };

    static constexpr BandSpec bands[numBands] = {
        { "hf",  "HF",  1500.0f, 16000.0f, 8000.0f, 0xffb8473a },   // red
        { "hmf", "HMF",  600.0f,  7000.0f, 3000.0f, 0xff3f8a52 },   // green
        { "lmf", "LMF",  200.0f,  2500.0f,  500.0f, 0xff3e6fae },   // blue
        { "lf",  "LF",    30.0f,   450.0f,  100.0f, 0xff3a3d3f },   // black
    };

    /** Creates prefix.eq.hf.gain, prefix.eq.hf.freq, ... */
    FourBandEq (ParamSet& params, const juce::String& prefix, int numChannels)
        : channels (juce::jlimit (1, maxChannels, numChannels))
    {
        for (int b = 0; b < numBands; ++b)
        {
            const auto& s = bands[b];
            const auto id = prefix + ".eq." + s.id;
            gain[b] = &params.add (id + ".gain", juce::String (s.label) + " gain", -15.0f, 15.0f, 0.0f);
            freq[b] = &params.add (id + ".freq", juce::String (s.label) + " frequency", s.minHz, s.maxHz, s.defHz, Param::Scale::log);
        }
    }

    Param* gain[numBands] {};
    Param* freq[numBands] {};

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int b = 0; b < numBands; ++b)
        {
            setGain[b] = setFreq[b] = -1000.0f;
            for (auto& ch : filters)
                ch[(size_t) b].reset();
        }
        update();
    }

    /** Once per block: recompute coefficients for bands whose knobs moved. */
    void update() noexcept
    {
        for (int b = 0; b < numBands; ++b)
        {
            const float g = gain[b]->get(), f = freq[b]->get();
            const bool wasActive = active[b];
            active[b] = g != 0.0f;
            if (active[b] && ! wasActive)
                for (int c = 0; c < channels; ++c)
                    filters[(size_t) c][(size_t) b].reset();   // no stale state from before the bypass
            if (! active[b] || (g == setGain[b] && f == setFreq[b]))
                continue;

            Dsp::Biquad design;
            if (b == HF)      design.setHighShelf (sampleRate, f, g);
            else if (b == LF) design.setLowShelf (sampleRate, f, g);
            else              design.setPeak (sampleRate, f, 1.0, g);

            for (int c = 0; c < channels; ++c)
            {
                auto& bq = filters[(size_t) c][(size_t) b];
                bq.b0 = design.b0; bq.b1 = design.b1; bq.b2 = design.b2; bq.a1 = design.a1; bq.a2 = design.a2;
            }
            setGain[b] = g;
            setFreq[b] = f;
        }
    }

    bool isFlat() const noexcept { return ! (active[0] || active[1] || active[2] || active[3]); }

    /** Signal order LF -> LMF -> HMF -> HF, as in the prototype. */
    float process (int channel, float x) noexcept
    {
        auto& f = filters[(size_t) channel];
        if (active[LF])  x = f[LF].process (x);
        if (active[LMF]) x = f[LMF].process (x);
        if (active[HMF]) x = f[HMF].process (x);
        if (active[HF])  x = f[HF].process (x);
        return x;
    }

private:
    static constexpr int maxChannels = 2;
    const int channels;
    double sampleRate = 48000.0;
    std::array<std::array<Dsp::Biquad, numBands>, maxChannels> filters {};
    bool active[numBands] {};
    float setGain[numBands] {}, setFreq[numBands] {};
};
