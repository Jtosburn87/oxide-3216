#pragma once

#include <juce_core/juce_core.h>
#include "Params.h"
#include "Config.h"
#include "Eq.h"
#include <array>

/** The song's tempo and time signature, the bar | beat | tick position they give the tape, and the click.

    The grid is laid on the tape: bar 1 starts at the start of the tape, and positions are tape samples
    (so vari-speed moves the click with the tape, as a click printed on a track would). One tempo for the
    whole song. Saved with the session; scenes leave it alone (see SceneMemory). */
class Tempo
{
public:
    explicit Tempo (ParamSet&);

    Param* bpm = nullptr;          // 20..300 beats a minute
    Param* beatsPerBar = nullptr;  // stepped 0..15 = 1..16 beats a bar
    Param* beatUnit = nullptr;     // stepped 0..3 = 2, 4, 8, 16 (the note that gets the beat)
    Param* clickOn = nullptr;
    Param* clickLevel = nullptr;   // dB, -40..0
    // The click's own strip (Setup, next to Click): EQ, pan, the main send and sends to the aux outputs (for the
    // headphone mixes; like the talkback they join after the aux returns, so the click never reaches the mix).
    Param* clickPan = nullptr;      // -1..1
    Param* clickMain = nullptr;     // to the main and monitor outs (on)
    Param* clickEqIn = nullptr;
    std::array<Param*, Config::numAux> clickAux {};   // 0..1, the channel aux send law
    FourBandEq clickEq;             // click.eq.hf.gain ...

    static constexpr float minBpm = 20.0f, maxBpm = 300.0f;
    static constexpr int ticksPerBeat = 960;

    int getBeatsPerBar() const noexcept { return juce::roundToInt (beatsPerBar->get()) + 1; }
    int getBeatUnit() const noexcept    { return 2 << juce::jlimit (0, 3, juce::roundToInt (beatUnit->get())); }
    void setSignature (int beats, int unit);   // unit: 2, 4, 8 or 16
    juce::String signatureText() const { return juce::String (getBeatsPerBar()) + "/" + juce::String (getBeatUnit()); }

    /** Length of one beat in samples at a sample rate. */
    double samplesPerBeat (double sampleRate) const noexcept;

    struct Position { int bar = 1, beat = 1, tick = 0; };
    Position positionAt (juce::int64 samples, double sampleRate) const noexcept;
    /** "012 | 3 | 000" */
    static juce::String format (Position);

    // Click (audio thread) --------------------------------------------------
    void prepare (double sampleRate);
    /** Left / right gains of the click on the main outs (pan, constant power). */
    std::pair<float, float> clickPanGains() const noexcept;
    /** Writes this block's click into out[0..n) (cleared first). The tape went from tape sample `from` to `to`
        during the block; running = the tape is playing (not winding or stopped). A jump (locate, loop) clicks
        nothing until the tape runs smoothly again. Returns false when the block is silent. */
    bool renderClick (float* out, int n, juce::int64 from, juce::int64 to, double tapeSampleRate, bool running) noexcept;

private:
    double sampleRate = 48000.0;
    // One voice: a short sine blip, higher on the first beat of the bar.
    double phase = 0.0, phaseInc = 0.0;
    float env = 0.0f, envDecay = 0.0f, attack = 0.0f;
    void trigger (bool downbeat) noexcept;
};
