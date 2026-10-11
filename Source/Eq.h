#pragma once

#include "Params.h"
#include "Dsp.h"

/** The console's 4-band sweepable EQ, used on every channel strip and as the group EQ on each sub.
    HF and LF are shelves, HMF and LMF are bells (Q 1). Gain +/-15 dB. A band at 0 dB is bypassed.
    Channel strips also get extras (shown in the channel window): HF and LF can be bells (Q 1), and HMF
    and LMF have a Q knob. The defaults sound as before. The group EQs have fixed bands: gain knobs only,
    each band at its default frequency. */
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

    /** Creates prefix.eq.hf.gain, prefix.eq.hf.freq, ...; with extras also prefix.eq.hf.bell,
        prefix.eq.lf.bell, prefix.eq.hmf.q and prefix.eq.lmf.q. With fixedBands there are no .freq
        controls (freq[] stays null). */
    FourBandEq (ParamSet& params, const juce::String& prefix, int numChannels, bool extras = false, bool fixedBands = false)
        : channels (juce::jlimit (1, maxChannels, numChannels))
    {
        for (int b = 0; b < numBands; ++b)
        {
            const auto& s = bands[b];
            const auto id = prefix + ".eq." + s.id;
            gain[b] = &params.add (id + ".gain", juce::String (s.label) + " gain", -15.0f, 15.0f, 0.0f);
            if (! fixedBands)
                freq[b] = &params.add (id + ".freq", juce::String (s.label) + " frequency", s.minHz, s.maxHz, s.defHz, Param::Scale::log);
        }
        if (extras)
        {
            for (const int b : { HF, LF })
                bell[b] = &params.addToggle (prefix + ".eq." + bands[b].id + ".bell", juce::String (bands[b].label) + " bell", false);
            for (const int b : { HMF, LMF })
                q[b] = &params.add (prefix + ".eq." + bands[b].id + ".q", juce::String (bands[b].label) + " Q", 0.4f, 4.0f, 1.0f, Param::Scale::log);
        }
    }

    Param* gain[numBands] {};
    Param* freq[numBands] {};     // null with fixed bands
    Param* bell[numBands] {};     // extras, HF and LF only: a bell instead of a shelf
    Param* q[numBands] {};        // extras, HMF and LMF only

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int b = 0; b < numBands; ++b)
        {
            setGain[b] = setFreq[b] = setShape[b] = -1000.0f;
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
            const float g = gain[b]->get(), f = freq[b] != nullptr ? freq[b]->get() : bands[b].defHz;
            // Shape: the HF/LF bell switch (1 = bell), or the band's Q.
            const bool shelfBand = b == HF || b == LF;
            const float shape = shelfBand ? (bell[b] != nullptr && bell[b]->on() ? 1.0f : 0.0f)
                                          : (q[b] != nullptr ? q[b]->get() : 1.0f);
            const bool wasActive = active[b];
            active[b] = g != 0.0f;
            if (active[b] && ! wasActive)
                for (int c = 0; c < channels; ++c)
                    filters[(size_t) c][(size_t) b].reset();   // no stale state from before the bypass
            if (! active[b] || (g == setGain[b] && f == setFreq[b] && shape == setShape[b]))
                continue;

            Dsp::Biquad design;
            if (shelfBand && shape > 0.5f) design.setPeak (sampleRate, f, 1.0, g);
            else if (b == HF)              design.setHighShelf (sampleRate, f, g);
            else if (b == LF)              design.setLowShelf (sampleRate, f, g);
            else                           design.setPeak (sampleRate, f, shape, g);

            for (int c = 0; c < channels; ++c)
            {
                auto& bq = filters[(size_t) c][(size_t) b];
                bq.b0 = design.b0; bq.b1 = design.b1; bq.b2 = design.b2; bq.a1 = design.a1; bq.a2 = design.a2;
            }
            setGain[b] = g;
            setFreq[b] = f;
            setShape[b] = shape;
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
    float setGain[numBands] {}, setFreq[numBands] {}, setShape[numBands] {};
};
