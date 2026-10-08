#pragma once

#include "Params.h"
#include "Dsp.h"

/** DSP for one mono console channel (input or tape return).

    Signal order (stage 2): input -> trim -> high-pass -> fader -> mute -> direct out -> pan -> main.
    Stage 3 adds EQ, gate and compressor between the high-pass and the fader, plus aux/sub sends. */
class ChannelStrip
{
public:
    /** idPrefix is e.g. "ch1" or "rt1"; parameters are created as "ch1.trim", "ch1.fader", ... */
    ChannelStrip (ParamSet&, const juce::String& idPrefix, const juce::String& defaultName);

    struct Controls
    {
        Param* trim;    // dB, -20..+30
        Param* hpf;     // Hz, 20..300 (20 = off)
        Param* pan;     // -1..+1
        Param* main;    // main assign
        Param* mute;
        Param* solo;
        Param* fader;   // 0..100, see Dsp::faderGain
    } controls;

    const juce::String idPrefix;
    juce::String name;   // scribble strip

    void prepare (double sampleRate);

    /** in may be null (silence). direct receives the post-fader, post-mute signal.
        The panned signal is added to mainL/mainR when main is assigned and the strip is not cut by solo. */
    void process (const float* in, float* direct, float* mainL, float* mainR, int numSamples, bool anySoloActive) noexcept;

    float takePeak() noexcept { return peak.exchange (0.0f); }

private:
    double sampleRate = 48000.0;
    Dsp::Biquad hp;
    float hpFreqSet = -1.0f;
    Dsp::Smoothed trimGain, faderGain, leftGain, rightGain;
    std::atomic<float> peak { 0.0f };

    float targetFader() const noexcept;
    void targetPan (bool anySoloActive, float& l, float& r) const noexcept;
};
