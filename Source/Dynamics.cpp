#include "Dynamics.h"

namespace
{
    constexpr float twoPi = 6.28318530718f;
    float lin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    float coeff (double seconds, double sr) noexcept { return (float) std::exp (-1.0 / (seconds * sr)); }

    /** Gain reduction (dB) for a level this far over the threshold: ratio slope (1 - 1/ratio), soft knee this wide. */
    float kneeCurve (float over, float slope, float knee) noexcept
    {
        if (over > knee * 0.5f)
            return over * slope;
        if (knee > 0.0f && over > -knee * 0.5f)
        {
            const float k = over + knee * 0.5f;
            return slope * k * k / (2.0f * knee);
        }
        return 0.0f;
    }

    /** Ratio knob (1..60) to slope; the top of the knob (40 and up) is infinity. */
    float ratioSlope (float ratio) noexcept { return ratio >= 40.0f ? 1.0f : 1.0f - 1.0f / juce::jmax (1.0f, ratio); }
}

//==============================================================================
Gate::Gate (ParamSet& params, const juce::String& prefix)
{
    threshold = &params.add (prefix + ".gate.thr", "Gate threshold", -80.0f, -10.0f, -80.0f);
    range     = &params.add (prefix + ".gate.range", "Gate range", -60.0f, 0.0f, -40.0f);
    fast      = &params.addToggle (prefix + ".gate.fast", "Gate fast attack", false);
    release   = &params.add (prefix + ".gate.release", "Gate release", 0.02f, 2.0f, 0.12f, Param::Scale::log);
    scIn      = &params.addToggle (prefix + ".gate.sc", "Gate side chain in", false);
    scType    = &params.addStepped (prefix + ".gate.sc.type", "Gate side chain filter", 3, 0);
    scFreq    = &params.add (prefix + ".gate.sc.freq", "Gate side chain frequency", 20.0f, 20000.0f, 1000.0f, Param::Scale::log);
    scQ       = &params.add (prefix + ".gate.sc.q", "Gate side chain Q", 0.5f, 8.0f, 0.707f, Param::Scale::log);
}

void Gate::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    env = 0.0f;
    gain = 1.0f;
    open = true;
    hold = watch = cooldown = popT = 0;
    popAmp = 0.0f;
    scFilter.reset();
    scSetType = scSetFreq = scSetQ = -1.0f;
    beginBlock();
}

void Gate::beginBlock() noexcept
{
    off = isOff();
    fastOn = fast->on();
    dec = coeff (0.02, sr);
    aAttack = coeff (fastOn ? 0.0001 : 0.0015, sr);
    aRelease = coeff (release->get(), sr);

    // Side-chain filter: redesigned only when its knobs move; cleared when it comes in.
    const bool wasOn = scOn;
    scOn = scIn->on();
    if (scOn)
    {
        if (! wasOn)
            scFilter.reset();
        const float type = scType->get(), f = juce::jmin (scFreq->get(), (float) sr * 0.45f), q = scQ->get();
        if (type != scSetType || f != scSetFreq || q != scSetQ)
        {
            Dsp::Biquad design;
            if (type < 0.5f)      design.setHighPass (sr, f, q);
            else if (type < 1.5f) design.setLowPass (sr, f, q);
            else                  design.setBandPass (sr, f, q);
            scFilter.copyCoefficients (design);
            scSetType = type;
            scSetFreq = f;
            scSetQ = q;
        }
    }
    thrLin = lin (threshold->get());
    closeLin = lin (threshold->get() - 5.0f);   // 5 dB hysteresis
    rangeLin = lin (range->get());

    const float db = blockMinGain < 0.99999f ? -20.0f * std::log10 (blockMinGain + 1.0e-6f) : 0.0f;
    reductionPeak.store (juce::jmax (reductionPeak.load (std::memory_order_relaxed), db), std::memory_order_relaxed);
    blockMinGain = 1.0f;
}

float Gate::process (float x) noexcept
{
    const float pk = std::abs (x);   // the pop still answers to the real transient
    env = juce::jmax (scOn ? std::abs (scFilter.process (x)) : pk, env * dec);

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
    blockMinGain = juce::jmin (blockMinGain, gain);
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
    env = gr = 0.0f;
    beginBlock();
}

void FetCompressor::beginBlock() noexcept
{
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
    const float target = kneeCurve (20.0f * std::log10 (env + 1.0e-9f) - threshold, slope, knee);
    gr = target > gr ? aAttack * gr + (1.0f - aAttack) * target
                     : aRelease * gr + (1.0f - aRelease) * target;

    const float y = x * lin (-gr) * driveGain;
    return (std::tanh (satScale * y + satBias) - satOffset) / satScale * outGain;
}

//==============================================================================
VcaCompressor::VcaCompressor (ParamSet& params, const juce::String& prefix)
{
    threshold = &params.add (prefix + ".comp.60.thr", "60 threshold", -50.0f, 0.0f, -20.0f);
    ratio     = &params.add (prefix + ".comp.60.ratio", "60 ratio", 1.0f, 60.0f, 4.0f, Param::Scale::log);
    output    = &params.add (prefix + ".comp.60.out", "60 output", -20.0f, 20.0f, 0.0f);
    overEasy  = &params.addToggle (prefix + ".comp.60.oe", "60 OverEasy", false);
}

void VcaCompressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    meanSquare = gr = 0.0f;
    beginBlock();
}

void VcaCompressor::beginBlock() noexcept
{
    aRms = coeff (0.008, sr);   // RMS averaging
    releasePerSample = (float) (120.0 / sr);
    thresholdDb = threshold->get();
    slope = ratioSlope (ratio->get());
    knee = overEasy->on() ? 12.0f : 0.0f;
    outGain = lin (output->get());
}

float VcaCompressor::process (float x) noexcept
{
    meanSquare = aRms * meanSquare + (1.0f - aRms) * x * x;
    // RMS level, +3 dB so a sine's threshold matches its peak level (as on the other types).
    const float level = 10.0f * std::log10 (meanSquare + 1.0e-12f) + 3.01f;
    const float target = kneeCurve (level - thresholdDb, slope, knee);
    if (target > gr)
    {
        // The further over, the faster: about 15 ms for 10 dB, 5 ms for 30 dB.
        const float tau = juce::jlimit (0.002f, 0.03f, 0.15f / juce::jmax (target - gr, 1.0f));
        gr += (target - gr) * juce::jmin (1.0f, 1.0f / (tau * (float) sr));
    }
    else
    {
        gr = juce::jmax (target, gr - releasePerSample);   // 120 dB per second
    }
    return x * lin (-gr) * outGain;
}

//==============================================================================
OptoCompressor::OptoCompressor (ParamSet& params, const juce::String& prefix)
{
    peakReduction = &params.add (prefix + ".comp.la.peak", "LA peak reduction", 0.0f, 100.0f, 40.0f);
    gain          = &params.add (prefix + ".comp.la.gain", "LA gain", 0.0f, 30.0f, 0.0f);
    limit         = &params.addStepped (prefix + ".comp.la.limit", "LA comp / limit", 2, 0);
}

void OptoCompressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    env = fastGr = slowGr = gr = 0.0f;
    beginBlock();
}

void OptoCompressor::beginBlock() noexcept
{
    dec = coeff (0.002, sr);
    aAttack = coeff (0.010, sr);
    aFast = coeff (0.06, sr);
    aSlow = coeff (1.5, sr);
    const float p = peakReduction->get();
    active = p > 0.5f;
    thresholdDb = -2.0f - p * 0.4f;   // 0 = off .. 100 = -42 dBFS
    const bool lim = limit->get() > 0.5f;
    slope = lim ? 0.9f : 1.0f - 1.0f / 3.0f;
    knee = lim ? 6.0f : 12.0f;
    makeup = lin (gain->get());
}

float OptoCompressor::process (float x) noexcept
{
    env = juce::jmax (std::abs (x), env * dec);
    const float target = active ? kneeCurve (20.0f * std::log10 (env + 1.0e-9f) - thresholdDb, slope, knee) : 0.0f;
    // Two parts of the cell: both attack together; one lets go quickly, the other slowly.
    fastGr = target > fastGr ? aAttack * fastGr + (1.0f - aAttack) * target : aFast * fastGr + (1.0f - aFast) * target;
    slowGr = target > slowGr ? aAttack * slowGr + (1.0f - aAttack) * target : aSlow * slowGr + (1.0f - aSlow) * target;
    gr = 0.5f * (fastGr + slowGr);

    const float y = x * lin (-gr) * makeup;
    constexpr float drive = 0.5f, bias = 0.02f;   // gentle tube colour
    return (std::tanh (drive * y + bias) - std::tanh (bias)) / drive;
}

//==============================================================================
SslCompressor::SslCompressor (ParamSet& params, const juce::String& prefix)
{
    threshold = &params.add (prefix + ".comp.g.thr", "G threshold", -40.0f, 0.0f, -20.0f);
    ratio     = &params.add (prefix + ".comp.g.ratio", "G ratio", 1.0f, 60.0f, 4.0f, Param::Scale::log);
    release   = &params.add (prefix + ".comp.g.release", "G release", 0.1f, 4.0f, 0.3f, Param::Scale::log);
    fast      = &params.addToggle (prefix + ".comp.g.fast", "G fast attack", false);
}

void SslCompressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    env = gr = 0.0f;
    beginBlock();
}

void SslCompressor::beginBlock() noexcept
{
    dec = coeff (0.0005, sr);
    aAttack = coeff (fast->on() ? 0.0003 : 0.003, sr);
    aRelease = coeff (release->get(), sr);
    thresholdDb = threshold->get();
    slope = ratioSlope (ratio->get());
    makeup = lin (-thresholdDb * slope * 0.5f);   // automatic make-up: half of what a full-scale peak loses
}

float SslCompressor::process (float x) noexcept
{
    env = juce::jmax (std::abs (x), env * dec);
    const float target = kneeCurve (20.0f * std::log10 (env + 1.0e-9f) - thresholdDb, slope, 2.0f);
    gr = target > gr ? aAttack * gr + (1.0f - aAttack) * target
                     : aRelease * gr + (1.0f - aRelease) * target;
    return x * lin (-gr) * makeup;
}

//==============================================================================
ChannelCompressor::ChannelCompressor (ParamSet& params, const juce::String& prefix)
    : fet (params, prefix), vca (params, prefix), opto (params, prefix), ssl (params, prefix)
{
    in   = &params.addToggle (prefix + ".comp.in", "Compressor in", false);
    type = &params.addStepped (prefix + ".comp.type", "Compressor type", numTypes, vca60);
}

void ChannelCompressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    current = getType();
    wasIn = isIn();
    reset();
    blockMaxGr = 0.0f;
}

void ChannelCompressor::reset() noexcept
{
    fet.prepare (sr);
    vca.prepare (sr);
    opto.prepare (sr);
    ssl.prepare (sr);
}

void ChannelCompressor::beginBlock() noexcept
{
    const bool nowIn = isIn();
    const auto t = getType();
    if (nowIn != wasIn || t != current)
    {
        reset();   // from rest: no gain reduction left over from before
        wasIn = nowIn;
        current = t;
    }
    switch (current)
    {
        case vca60:  vca.beginBlock();  break;
        case fet76:  fet.beginBlock();  break;
        case optoLA: opto.beginBlock(); break;
        default:     ssl.beginBlock();  break;
    }
}

float ChannelCompressor::process (float x) noexcept
{
    float y, g;
    switch (current)
    {
        case vca60:  y = vca.process (x);  g = vca.gr;  break;
        case fet76:  y = fet.process (x);  g = fet.gr;  break;
        case optoLA: y = opto.process (x); g = opto.gr; break;
        default:     y = ssl.process (x);  g = ssl.gr;  break;
    }
    blockMaxGr = juce::jmax (blockMaxGr, g);
    return y;
}

void ChannelCompressor::finishBlock() noexcept
{
    auto peak = grPeak.load (std::memory_order_relaxed);
    while (blockMaxGr > peak && ! grPeak.compare_exchange_weak (peak, blockMaxGr, std::memory_order_relaxed)) {}
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
