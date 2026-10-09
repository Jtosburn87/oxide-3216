#pragma once

#include "Params.h"
#include "Dsp.h"

/** Fast-attack pop: the exaggerated click a hardware gate makes when a loud transient
    slams it open. Change these numbers to taste. */
namespace GatePop
{
    constexpr float triggerLevel = 0.25f;     // transient peak that fires it (-12 dBFS)
    constexpr float amount       = 1.2f;      // pop level = amount x transient peak ...
    constexpr float maxLevel     = 0.8f;      // ... capped here
    constexpr float thumpHz      = 150.0f;    // low thump
    constexpr float thumpDecayS  = 0.014f;
    constexpr float thumpMix     = 0.8f;
    constexpr float clickDecayS  = 0.0007f;   // short noise click
    constexpr float clickMix     = 0.6f;
    constexpr float lockoutS     = 0.040f;    // no re-trigger for this long
    constexpr float watchS       = 0.004f;    // after opening, how long a transient may still fire it
}

/** Channel gate (from the prototype): peak detector, 5 dB hysteresis, 50 ms hold,
    attack 1.5 ms (0.1 ms with Fast Attack), release 120 ms. Threshold at minimum = off. */
class Gate
{
public:
    Gate (ParamSet&, const juce::String& prefix);

    Param* threshold;   // dBFS, -80..-10 (-80 = off)
    Param* range;       // dB, -60..0
    Param* fast;        // fast attack + pop

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    bool isOff() const noexcept { return threshold->get() <= -79.0f; }

    /** Fully open with no pop running: safe to skip processing while off. */
    bool isIdle() const noexcept { return gain >= 0.99999f && popAmp == 0.0f && watch == 0; }

    /** Counts pops; the UI flashes the Fast Attack button when this changes. */
    std::atomic<std::uint32_t> popCount { 0 };

    /** Most the gate has turned the signal down since the last call (dB, positive), for its meter. */
    float takeReduction() noexcept { return reductionPeak.exchange (0.0f); }

private:
    double sr = 48000.0;
    float env = 0.0f, gain = 1.0f, blockMinGain = 1.0f;
    std::atomic<float> reductionPeak { 0.0f };
    bool open = true;
    int hold = 0, watch = 0, cooldown = 0, popT = 0;
    float popAmp = 0.0f;
    Dsp::Noise noise;

    // per-block values
    bool off = true, fastOn = false;
    float dec = 0, aAttack = 0, aRelease = 0, thrLin = 0, closeLin = 0, rangeLin = 0;
};

/** De-esser for the return strips, switched in place of the gate. The detector compares the level in the
    sibilant band with the whole signal, so it acts the same on loud and quiet takes. Narrow: a band around
    the frequency; wide: everything above it. Full: the whole signal is turned down during an "s";
    gentle: only the band is. Reduction is the most it turns down. */
class DeEsser
{
public:
    DeEsser (ParamSet&, const juce::String& prefix);

    Param* on;          // the gate slot is a de-esser instead of a gate
    Param* frequency;   // Hz, 2000..12000
    Param* reduction;   // dB, 0..20
    Param* wide;        // off: narrow band around the frequency; on: everything above it
    Param* gentle;      // off: full band is turned down; on: only the sibilant band
    Param* bypass;

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    bool isActive() const noexcept { return on->on() && ! bypass->on(); }

    /** Largest gain reduction since the last call (dB, positive), for a lamp or meter. */
    float takeReduction() noexcept { return reductionPeak.exchange (0.0f); }

    static constexpr float narrowQ = 2.0f;
    static constexpr float dominanceDb = 9.0f;   // starts acting when the band is within 9 dB of the whole signal
    static constexpr float slope = 1.5f;         // dB of reduction per dB above that

private:
    double sr = 48000.0;
    Dsp::Biquad detect, split;
    float shownFreq = -1.0f;
    bool shownWide = false;
    float bandEnv = 0.0f, fullEnv = 0.0f, gain = 1.0f;
    float aAttack = 0.0f, aRelease = 0.0f, gAttack = 0.0f, gRelease = 0.0f;

    // per-block values
    float maxReduction = 0.0f, blockPeak = 0.0f;
    bool gentleOn = false;
    std::atomic<float> reductionPeak { 0.0f };
};

/** 1176-style FET compressor (from the prototype), with a threshold knob instead of the 1176's input drive.
    Peak detection, soft knee 4 dB, make-up gain, then asymmetric tanh saturation (so heavy make-up drives it). */
class FetCompressor
{
public:
    FetCompressor (ParamSet&, const juce::String& prefix);

    Param* in;          // in/out, default out
    Param* threshold;   // dBFS, -50..0
    Param* makeup;      // dB, 0..+24
    Param* attack;      // 1..7, 7 fastest (800 us .. 20 us); the knob reads Slow .. Fast
    Param* release;     // 1..7, 7 fastest (1.1 s .. 50 ms)
    Param* ratio;     // 0..3 = 4, 8, 12, 20
    Param* all;       // all buttons in

    static constexpr float ratios[4] = { 4.0f, 8.0f, 12.0f, 20.0f };

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    bool isIn() const noexcept { return in->on(); }

    /** Peak gain reduction (dB) since the last call. */
    float takeGainReduction() noexcept { return grPeak.exchange (0.0f); }

private:
    double sr = 48000.0;
    float env = 0.0f, gr = 0.0f, blockMaxGr = 0.0f;
    std::atomic<float> grPeak { 0.0f };

    // per-block values
    float thresholdDb = -20, makeupGain = 1, aAttack = 0, aRelease = 0, dec = 0, slope = 0, knee = 4, satScale = 0.9f;
    static constexpr float satBias = 0.08f;
    const float satOffset = std::tanh (satBias);
    bool wasIn = false;

    void finishBlock() noexcept;
    friend class ChannelStrip;
};
