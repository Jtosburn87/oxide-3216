#pragma once

#include "Config.h"
#include "Params.h"
#include "Dsp.h"
#include "Eq.h"
#include "Dynamics.h"
#include "Preamp.h"

/** DSP for one mono console channel (input or tape return).

    Signal order (as in the prototype):
        input -> phase -> gain -> preamp colour -> trim -> high-pass -> EQ (LF, LMF, HMF, HF) -> gate -> compressor
              -> [pre-fader sends] -> fader -> mute -> direct out -> [post-fader sends] -> pan -> main / subs
                                                                                     -> solo bus (when soloed) */
class ChannelStrip
{
public:
    /** idPrefix is e.g. "ch1" or "rt1"; parameters are created as "ch1.gain", "ch1.eq.hf.gain", ... */
    ChannelStrip (ParamSet&, const juce::String& idPrefix, const juce::String& defaultName, bool withDeEsser = false,
                  float defaultFader = 80.0f);

    const juce::String idPrefix;
    juce::String name;   // scribble strip

    struct Controls
    {
        Param* phase;   // flips the input's polarity
        Param* gain;    // dB, -20..+30: the preamp's gain (drives the colour)
        Param* trim;    // dB, -30..+20: level after the preamp
        Param* gainLink;   // on: turning the gain by hand turns the trim the other way (turnGain)
        Param* hpf;     // Hz, 20..300 (20 = off)
        Param* pan;     // -1..+1
        Param* main;    // main assign
        Param* mute;
        Param* solo;
        Param* fader;   // 0..100, see Dsp::faderGain
        Param* sub[Config::numSubs];        // sub assigns
        Param* auxLevel[Config::numAux];    // 0..1
        Param* auxPre[Config::numAux];      // pre-fader send
        Param* order;                       // processing order of EQ, gate and compressor (chainOrders)
        Param* eqIn;                        // EQ in (out = bypassed)
        Param* gateIn;                      // gate in (out = bypassed; not the de-esser, which has its own)
    } controls;

    /** The gain knob turned by hand. While the link is on, the trim moves the other way by the same amount,
        so the preamp is driven harder (or cleaner) at the same level. Message thread only. */
    void turnGain (float db);

    /** The six orders the EQ, gate and compressor can run in (stage numbers below). Order 0 is the default. */
    enum Stage { eqStage, gateStage, compStage };
    using Chain = std::array<int, 3>;
    static constexpr std::array<Chain, 6> chainOrders { { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } } };
    Chain chain() const noexcept { return chainOrders[(size_t) juce::jlimit (0, 5, juce::roundToInt (controls.order->get()))]; }
    void setChain (const Chain&);

    /** Linked aux pairs (A1+A2, A3+A4, ...), set by the engine; null = never linked. While a pair is linked
        the strip feeds it in stereo: the odd send knob is the level, the even one its pan (0.5 = centre,
        same pan law as the main pan), and the odd pre switch serves both. */
    std::array<const Param*, Config::numAux / 2> auxLink {};
    bool isAuxLinked (int pair) const noexcept { return auxLink[(size_t) pair] != nullptr && auxLink[(size_t) pair]->on(); }

    /** The console colour switch (one for the whole desk), set by the engine; null = clean preamp. */
    const Param* colour = nullptr;
    bool isColoured() const noexcept { return colour != nullptr && colour->on(); }

    FourBandEq eq;
    Gate gate;
    std::unique_ptr<DeEsser> deEsser;   // return strips: the gate slot can be a de-esser instead
    ChannelCompressor comp;

    void prepare (double sampleRate);

    /** in may be null (silence). direct receives the post-fader, post-mute signal (the direct out). */
    void process (const float* in, float* direct, const BusPointers& buses, int numSamples) noexcept;

    float takePeak() noexcept { return peak.exchange (0.0f); }             // output: post-fader
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f); }   // input: after the preamp and trim

    /** Aux send gain law from the prototype: level^2 x 1.5 (about +3.5 dB at full). */
    static float auxGain (float level) noexcept { return level * level * 1.5f; }

private:
    double sampleRate = 48000.0;
    Preamp preamp;
    Dsp::Biquad hp;
    float hpFreqSet = -1.0f;
    bool eqWasIn = true;
    Dsp::Smoothed gainGain, trimGain, faderGain;
    std::atomic<float> peak { 0.0f }, inputPeak { 0.0f };

    std::array<float, Config::maxBlock> pre {};
    float lastMain[2] {}, lastSolo[2] {}, lastSub[Config::numSubs][2] {}, lastAux[Config::numAux] {};

    struct Targets
    {
        float main[2], solo[2], sub[Config::numSubs][2], aux[Config::numAux];
    };
    Targets targets() const noexcept;
    float targetGain() const noexcept;
    float targetFader() const noexcept;
};
