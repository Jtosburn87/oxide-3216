#include "Dynamics.h"

namespace
{
    constexpr float twoPi = 6.28318530718f;
    float lin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    float coeff (double seconds, double sr) noexcept { return (float) std::exp (-1.0 / (seconds * sr)); }
}

//==============================================================================
Gate::Gate (ParamSet& params, const juce::String& prefix)
{
    threshold = &params.add (prefix + ".gate.thr", "Gate threshold", -80.0f, -10.0f, -80.0f);
    range     = &params.add (prefix + ".gate.range", "Gate range", -60.0f, 0.0f, -40.0f);
    fast      = &params.addToggle (prefix + ".gate.fast", "Gate fast attack", false);
}

void Gate::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    env = 0.0f;
    gain = 1.0f;
    open = true;
    hold = watch = cooldown = popT = 0;
    popAmp = 0.0f;
    beginBlock();
}

void Gate::beginBlock() noexcept
{
    off = isOff();
    fastOn = fast->on();
    dec = coeff (0.02, sr);
    aAttack = coeff (fastOn ? 0.0001 : 0.0015, sr);
    aRelease = coeff (0.12, sr);
    thrLin = lin (threshold->get());
    closeLin = lin (threshold->get() - 5.0f);   // 5 dB hysteresis
    rangeLin = lin (range->get());
}

float Gate::process (float x) noexcept
{
    const float pk = std::abs (x);
    env = juce::jmax (pk, env * dec);

    float target = 1.0f;
    if (! off)
    {
        if (env > thrLin)
        {
            if (! open)
            {
                if (fastOn && gain < 0.5f && cooldown <= 0)
                    watch = (int) (GatePop::watchS * sr);
                open = true;
            }
            hold = (int) (0.05 * sr);
        }
        else if (open && env < closeLin)
        {
            if (hold > 0) --hold;
            else open = false;
        }
        target = open ? 1.0f : rangeLin;
    }

    if (watch > 0)
    {
        if (pk > GatePop::triggerLevel)
        {
            popT = 0;
            popAmp = juce::jmin (GatePop::maxLevel, pk * GatePop::amount);
            cooldown = (int) (GatePop::lockoutS * sr);
            watch = 0;
            popCount.fetch_add (1, std::memory_order_relaxed);
        }
        else
        {
            --watch;
        }
    }

    gain = target > gain ? target + (gain - target) * aAttack
                         : target + (gain - target) * aRelease;
    if (cooldown > 0)
        --cooldown;

    float pop = 0.0f;
    if (popAmp > 0.0f)
    {
        const float t = (float) popT / (float) sr;
        pop = popAmp * (GatePop::thumpMix * std::sin (twoPi * GatePop::thumpHz * t) * std::exp (-t / GatePop::thumpDecayS)
                        + GatePop::clickMix * noise.next() * std::exp (-t / GatePop::clickDecayS));
        ++popT;
        if (t > 0.08f)
            popAmp = 0.0f;
    }

    return x * gain + pop;
}

//==============================================================================
FetCompressor::FetCompressor (ParamSet& params, const juce::String& prefix)
{
    in      = &params.addToggle (prefix + ".comp.in", "Compressor in", false);
    input   = &params.add (prefix + ".comp.input", "Compressor input", 0.0f, 10.0f, 0.0f);
    output  = &params.add (prefix + ".comp.output", "Compressor output", -12.0f, 12.0f, 0.0f);
    attack  = &params.add (prefix + ".comp.attack", "Compressor attack", 1.0f, 7.0f, 3.0f);
    release = &params.add (prefix + ".comp.release", "Compressor release", 1.0f, 7.0f, 4.0f);
    ratio   = &params.addStepped (prefix + ".comp.ratio", "Compressor ratio", 4, 0);
    all     = &params.addToggle (prefix + ".comp.all", "Compressor all buttons", false);
}

void FetCompressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    env = gr = blockMaxGr = 0.0f;
    beginBlock();
}

void FetCompressor::beginBlock() noexcept
{
    const bool nowIn = isIn();
    if (nowIn != wasIn)
    {
        env = gr = 0.0f;
        wasIn = nowIn;
    }

    const bool allIn = all->on();
    driveGain = lin (input->get() * 3.0f);
    outGain = lin (output->get());

    const double att = 0.0008 * std::pow (0.025, (attack->get() - 1.0) / 6.0);    // 800 us .. 20 us
    const double rel = 1.1 * std::pow (0.0455, (release->get() - 1.0) / 6.0)      // 1.1 s .. 50 ms
                       * (allIn ? 1.4 : 1.0);
    aAttack = coeff (juce::jmax (att, 0.00002), sr);
    aRelease = coeff (rel, sr);
    dec = coeff (0.0005, sr);

    const float r = allIn ? 60.0f : ratios[juce::jlimit (0, 3, juce::roundToInt (ratio->get()))];
    slope = 1.0f - 1.0f / r;
    knee = allIn ? 0.0f : 4.0f;
    satScale = 0.9f * (allIn ? 1.8f : 1.0f);   // all buttons in: extra drive into the saturation
}

float FetCompressor::process (float x) noexcept
{
    constexpr float threshold = -14.0f;

    env = juce::jmax (std::abs (x) * driveGain, env * dec);
    const float over = 20.0f * std::log10 (env + 1.0e-9f) - threshold;

    float target = 0.0f;
    if (over > knee * 0.5f)
        target = over * slope;
    else if (knee > 0.0f && over > -knee * 0.5f)
    {
        const float k = over + knee * 0.5f;
        target = slope * k * k / (2.0f * knee);
    }

    gr = target > gr ? aAttack * gr + (1.0f - aAttack) * target
                     : aRelease * gr + (1.0f - aRelease) * target;
    blockMaxGr = juce::jmax (blockMaxGr, gr);

    const float y = x * lin (-gr) * driveGain;
    return (std::tanh (satScale * y + satBias) - satOffset) / satScale * outGain;
}

void FetCompressor::finishBlock() noexcept
{
    auto current = grPeak.load (std::memory_order_relaxed);
    while (blockMaxGr > current && ! grPeak.compare_exchange_weak (current, blockMaxGr, std::memory_order_relaxed)) {}
    blockMaxGr = 0.0f;
}

//==============================================================================
DeEsser::DeEsser (ParamSet& params, const juce::String& prefix)
{
    on        = &params.addToggle (prefix + ".deess.on", "De-esser", false);
    frequency = &params.add (prefix + ".deess.freq", "De-esser frequency", 2000.0f, 12000.0f, 6000.0f, Param::Scale::log);
    reduction = &params.add (prefix + ".deess.reduction", "De-esser reduction", 0.0f, 20.0f, 8.0f);
    wide      = &params.addToggle (prefix + ".deess.wide", "De-esser wide", false);
    gentle    = &params.addToggle (prefix + ".deess.gentle", "De-esser gentle", false);
    bypass    = &params.addToggle (prefix + ".deess.bypass", "De-esser bypass", false);
}

void DeEsser::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    const auto coeff = [this] (double ms) { return (float) std::exp (-1.0 / (0.001 * ms * sr)); };
    aAttack = coeff (0.5);
    aRelease = coeff (60.0);
    gAttack = coeff (1.0);
    gRelease = coeff (40.0);
    detect.reset();
    split.reset();
    shownFreq = -1.0f;
    bandEnv = fullEnv = 0.0f;
    gain = 1.0f;
}

void DeEsser::beginBlock() noexcept
{
    const float f = juce::jmin (frequency->get(), (float) (sr * 0.45));
    const bool w = wide->on();
    if (f != shownFreq || w != shownWide)
    {
        if (w) detect.setHighPass (sr, f, 0.7071);
        else   detect.setBandPass (sr, f, narrowQ);
        split.copyCoefficients (detect);
        shownFreq = f;
        shownWide = w;
    }
    maxReduction = reduction->get();
    gentleOn = gentle->on();

    reductionPeak.store (juce::jmax (reductionPeak.load (std::memory_order_relaxed), blockPeak), std::memory_order_relaxed);
    blockPeak = 0.0f;
}

float DeEsser::process (float x) noexcept
{
    const float b = std::abs (detect.process (x)), a = std::abs (x);
    bandEnv = b > bandEnv ? b + aAttack * (bandEnv - b) : b + aRelease * (bandEnv - b);
    fullEnv = a > fullEnv ? a + aAttack * (fullEnv - a) : a + aRelease * (fullEnv - a);

    // How close the sibilant band comes to the whole signal decides the reduction.
    float reductionDb = 0.0f;
    if (fullEnv > 1.0e-4f)
    {
        const float excess = 20.0f * std::log10 ((bandEnv + 1.0e-9f) / fullEnv) + dominanceDb;
        reductionDb = juce::jlimit (0.0f, maxReduction, excess * slope);
    }
    const float target = Dsp::dbToGain (-reductionDb);
    gain = target < gain ? target + gAttack * (gain - target) : target + gRelease * (gain - target);
    blockPeak = juce::jmax (blockPeak, reductionDb);

    if (gentleOn)
        return x + (gain - 1.0f) * split.process (x);   // only the band comes down
    split.process (x);                                    // keep its state current for switching
    return x * gain;
}
