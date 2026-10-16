#pragma once

#include "Params.h"
#include "Dsp.h"
#include <array>

/** Stereo bus compressor (after the prototype): stereo-linked feed-forward VCA style,
    peak detector with ~1 ms decay, soft knee ~6 dB, no automatic make-up.
    Auto release blends a 0.1 s and a 1.2 s release. The side-chain high-pass (12 dB/oct)
    only affects what the detector hears, never the audio. */
class BusCompressor
{
public:
    explicit BusCompressor (ParamSet&);

    Param* in;          // IN button (default out)
    Param* threshold;   // knob -20..+20, 0 at the top; -18 dBFS at 0, 0.9 dB per unit
    Param* makeup;      // dB, -5..+15
    Param* attack;      // switch: .1 .3 1 3 10 30 ms
    Param* release;     // switch: .1 .2 .4 .8 1.6 s, Auto
    Param* ratio;       // switch: 1.5 2 3 4 5 10
    Param* hpf;         // switch: off 30 60 105 125 185 Hz (side chain only)

    static constexpr float attackMs[6]   = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
    static constexpr float releaseS[6]   = { 0.1f, 0.2f, 0.4f, 0.8f, 1.6f, 0.0f };   // 0 = auto
    static constexpr float ratios[6]     = { 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 10.0f };
    static constexpr float hpfHz[6]      = { 0.0f, 30.0f, 60.0f, 105.0f, 125.0f, 185.0f };
    static constexpr const char* attackLabels[6]  = { ".1", ".3", "1", "3", "10", "30" };
    static constexpr const char* releaseLabels[6] = { ".1", ".2", ".4", ".8", "1.6", "AUTO" };
    static constexpr const char* ratioLabels[6]   = { "1.5", "2", "3", "4", "5", "10" };
    static constexpr const char* hpfLabels[6]     = { "OFF", "30", "60", "105", "125", "185" };

    static float thresholdDb (float knob) noexcept { return -18.0f + 0.9f * knob; }

    void prepare (double sampleRate) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    /** Peak gain reduction (dB) since the last call. Each reader keeps its own peak: 0 = the VU needle,
        1 = the GR bar under the SBC button. */
    static constexpr int numGrReaders = 2;
    float takeGainReduction (int reader = 0) noexcept { return grPeak[(size_t) juce::jlimit (0, numGrReaders - 1, reader)].exchange (0.0f); }

private:
    double sr = 48000.0;
    float env = 0.0f, gr = 0.0f, grFast = 0.0f, grSlow = 0.0f;
    Dsp::Biquad scL, scR;
    int scHpfIndex = -1;
    Dsp::Smoothed wet, makeupGain;
    std::array<std::atomic<float>, numGrReaders> grPeak {};
};
