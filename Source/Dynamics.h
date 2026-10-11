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
    attack 1.5 ms (0.1 ms with Fast Attack), release 120 ms by default. Threshold at minimum = off.
    Side chain (channel window): the detector can listen through a high-pass, low-pass or band-pass
    filter; the audio itself is not filtered. */
class Gate
{
public:
    Gate (ParamSet&, const juce::String& prefix);

    Param* threshold;   // dBFS, -80..-10 (-80 = off)
    Param* range;       // dB, -60..0
    Param* fast;        // fast attack + pop
    Param* release;     // seconds, 0.02..2
    Param* scIn;        // side-chain filter in
    Param* scType;      // 0 HPF, 1 LPF, 2 BPF
    Param* scFreq;      // Hz
    Param* scQ;

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

    Dsp::Biquad scFilter;
    bool scOn = false;
    float scSetType = -1.0f, scSetFreq = -1.0f, scSetQ = -1.0f;   // what the filter is designed for
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

/** "76": 1176-style FET compressor (from the prototype). Input drives the signal into a fixed -14 dBFS
    threshold (3 dB a step); peak detection, soft knee 4 dB, then asymmetric tanh saturation and Output. */
class FetCompressor
{
public:
    FetCompressor (ParamSet&, const juce::String& prefix);

    Param* input;     // 0..10, 3 dB a step
    Param* output;    // dB, -12..+12
    Param* attack;    // 1..7, 7 fastest (800 us .. 20 us); the knob reads Slow .. Fast
    Param* release;   // 1..7, 7 fastest (1.1 s .. 50 ms)
    Param* ratio;     // 0..3 = 4, 8, 12, 20
    Param* all;       // all buttons in

    static constexpr float ratios[4] = { 4.0f, 8.0f, 12.0f, 20.0f };

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    float gr = 0.0f;   // current gain reduction, dB

private:
    double sr = 48000.0;
    float env = 0.0f;
    float driveGain = 1, outGain = 1, aAttack = 0, aRelease = 0, dec = 0, slope = 0, knee = 4, satScale = 0.9f;
    static constexpr float satBias = 0.08f;
    const float satOffset = std::tanh (satBias);
};

/** "60": dbx 160-style VCA compressor. RMS detection; attack gets faster the further the signal is over
    (about 15 ms for 10 dB, 5 ms for 30 dB); release at a fixed 120 dB/s. Hard knee, or OverEasy (a wide
    soft knee). Ratio up to infinity; clean VCA, Output after it. */
class VcaCompressor
{
public:
    VcaCompressor (ParamSet&, const juce::String& prefix);

    Param* threshold;   // dBFS, -50..0
    Param* ratio;       // 1..60 (60 shown as infinity)
    Param* output;      // dB, -20..+20
    Param* overEasy;    // soft knee

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    float gr = 0.0f;

private:
    double sr = 48000.0;
    float meanSquare = 0.0f;
    float aRms = 0, releasePerSample = 0, thresholdDb = -20, slope = 0, knee = 0, outGain = 1;
};

/** "LA": LA-2A-style opto compressor. Peak Reduction sets how hard it works, Gain is the make-up.
    Attack about 10 ms; release in two stages like the T4 cell (half back in about 60 ms, the rest over
    about 1.5 s). Comp about 3:1 with a wide knee; Limit about 10:1, firmer. A little tube colour. */
class OptoCompressor
{
public:
    OptoCompressor (ParamSet&, const juce::String& prefix);

    Param* peakReduction;   // 0..100
    Param* gain;            // dB, 0..30
    Param* limit;           // 0 = Comp, 1 = Limit (two-position switch)

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    float gr = 0.0f;

private:
    double sr = 48000.0;
    float env = 0.0f, fastGr = 0.0f, slowGr = 0.0f;
    float dec = 0, aAttack = 0, aFast = 0, aSlow = 0, thresholdDb = 0, slope = 0, knee = 10, makeup = 1;
    bool active = false;
};

/** "G": SSL G-series channel compressor style. Peak-sensing VCA, 3 ms attack (0.3 ms with Fast attack),
    Release knob, ratio up to infinity, small knee. The output is compensated automatically as threshold and
    ratio change (as on the console), so there is no make-up knob. */
class SslCompressor
{
public:
    SslCompressor (ParamSet&, const juce::String& prefix);

    Param* threshold;   // dBFS, -40..0
    Param* ratio;       // 1..60 (60 shown as infinity)
    Param* release;     // seconds, 0.1..4
    Param* fast;        // fast attack

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    float gr = 0.0f;

private:
    double sr = 48000.0;
    float env = 0.0f;
    float dec = 0, aAttack = 0, aRelease = 0, thresholdDb = -20, slope = 0, makeup = 1;
};

/** The channel compressor: In, and a choice of four (the channel window's 60 / 76 / LA / G buttons).
    Each type keeps its own settings; switching type starts the new one from rest. */
class ChannelCompressor
{
public:
    ChannelCompressor (ParamSet&, const juce::String& prefix);

    enum Type { vca60, fet76, optoLA, sslG, numTypes };
    static constexpr const char* typeLabels[numTypes] = { "60", "76", "LA", "G" };

    Param* in;     // in/out, default out
    Param* type;   // Type, default 60

    FetCompressor fet;
    VcaCompressor vca;
    OptoCompressor opto;
    SslCompressor ssl;

    Type getType() const noexcept { return (Type) juce::jlimit (0, numTypes - 1, juce::roundToInt (type->get())); }

    void prepare (double sampleRate) noexcept;
    void beginBlock() noexcept;
    float process (float x) noexcept;
    bool isIn() const noexcept { return in->on(); }

    /** Peak gain reduction (dB) since the last call. */
    float takeGainReduction() noexcept { return grPeak.exchange (0.0f); }

private:
    double sr = 48000.0;
    Type current = vca60;
    bool wasIn = false;
    float blockMaxGr = 0.0f;
    std::atomic<float> grPeak { 0.0f };

    void reset() noexcept;
    void finishBlock() noexcept;
    friend class ChannelStrip;
};
