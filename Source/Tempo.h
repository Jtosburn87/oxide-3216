#pragma once

#include <juce_core/juce_core.h>
#include "Params.h"
#include "Config.h"
#include "Eq.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/** The song's tempo and time signature, the bar | beat | tick position they give the tape, and the click.

    The grid is laid on the tape: bar 1 starts at the start of the tape, and positions are tape samples
    (so vari-speed moves the click with the tape, as a click printed on a track would). One tempo for the
    whole song from the controls, or a tempo map (imported from a MIDI file) with tempo and signature changes
    along the song. MIDI positions are ticks of a quarter note (ticksPerQuarter), so a clip stays on its bars
    whatever the map does. Saved with the session; scenes leave it alone (see SceneMemory). */
class Tempo
{
public:
    explicit Tempo (ParamSet&);

    Param* bpm = nullptr;          // 20..300 quarter notes a minute
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
    static constexpr int ticksPerQuarter = 960;

    int getBeatsPerBar() const noexcept { return juce::roundToInt (beatsPerBar->get()) + 1; }
    int getBeatUnit() const noexcept    { return 2 << juce::jlimit (0, 3, juce::roundToInt (beatUnit->get())); }
    void setSignature (int beats, int unit);   // unit: 2, 4, 8 or 16
    juce::String signatureText() const { return juce::String (getBeatsPerBar()) + "/" + juce::String (getBeatUnit()); }

    // The tempo map ------------------------------------------------------------------------
    /** From `tick` on: this tempo (quarter notes a minute) and time signature. */
    struct Change
    {
        double tick = 0.0;
        double bpm = 120.0;
        int beats = 4, unit = 4;
        bool operator== (const Change&) const = default;
    };
    /** Replaces the map (message thread). Empty = no map: one tempo from the controls again. The changes are
        sorted, the first one moved to tick 0, and the controls take its tempo and signature (so the song keeps
        its opening tempo if the map is cleared). */
    void setMap (std::vector<Change>);
    void clearMap() { setMap ({}); }
    bool hasMap() const noexcept { return mapSegments.load (std::memory_order_acquire) != nullptr; }
    /** The map's changes (empty without one). Message thread. */
    const std::vector<Change>& getMap() const noexcept { return changes; }
    /** The changes in force now: the map's, or the controls' tempo and signature as one change at tick 0. */
    std::vector<Change> getChanges() const;
    std::uint32_t getMapRevision() const noexcept { return mapRevision; }
    juce::var mapToVar() const;
    void mapFromVar (const juce::var&);

    // Ticks, seconds and bars (any thread): they follow the map when there is one --------------------
    double secondsAt (double ticks) const noexcept;
    double ticksAtSeconds (double seconds) const noexcept;
    double ticksAt (double tapeSamples, double sampleRate) const noexcept;
    double samplesAt (double ticks, double sampleRate) const noexcept;
    double bpmAt (double ticks) const noexcept;
    struct Signature { int beats = 4, unit = 4; };
    Signature signatureAt (double ticks) const noexcept;
    static juce::String signatureText (Signature s) { return juce::String (s.beats) + "/" + juce::String (s.unit); }
    /** Ticks of one beat of a signature's unit (an eighth in 6/8). */
    static double beatTicks (int unit) noexcept { return ticksPerQuarter * 4.0 / (double) unit; }

    struct Bar
    {
        double tick = 0.0, length = 4.0 * ticksPerQuarter;   // a signature change part-way through cuts a bar short
        int number = 1;                                       // bar 1 = the start of the tape
        int beats = 4, unit = 4;
        double end() const noexcept { return tick + length; }
    };
    /** The bar a tick is in (before the tape start: bar 1). */
    Bar barAt (double ticks) const noexcept;

    struct Position { int bar = 1, beat = 1, tick = 0; };   // tick: within the beat (960 a quarter note)
    Position positionAtTicks (double ticks) const noexcept;
    Position positionAt (juce::int64 samples, double sr) const noexcept { return positionAtTicks (ticksAt ((double) samples, sr)); }
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
    // The map as the audio thread reads it: one segment for every change, with where it starts in seconds and
    // the bar grid it carries on. Swapped whole; old ones are kept until the Tempo goes (imports are rare).
    struct Segment
    {
        double tick = 0.0, seconds = 0.0, bpm = 120.0;
        int beats = 4, unit = 4;
        double barTick = 0.0;       // a bar line at or before `tick` that the bars count on from
        int barIndex = 0;           // its bar (0 = bar 1)
        double nextSigTick = 1.0e18;   // the next signature change (cuts the bar running then short)
    };
    std::atomic<const std::vector<Segment>*> mapSegments { nullptr };
    std::vector<std::unique_ptr<std::vector<Segment>>> keptMaps;
    std::vector<Change> changes;
    std::uint32_t mapRevision = 0;

    Segment constantSegment() const noexcept;
    template <typename Fn>
    auto withSegments (Fn&& fn) const noexcept
    {
        if (auto* m = mapSegments.load (std::memory_order_acquire))
            return fn (m->data(), (int) m->size());
        const Segment one = constantSegment();
        return fn (&one, 1);
    }
    static int segmentAtTick (const Segment*, int n, double ticks) noexcept;
    static int segmentAtSeconds (const Segment*, int n, double seconds) noexcept;
    static Bar barIn (const Segment&, double ticks) noexcept;

    double sampleRate = 48000.0;
    // One voice: a short sine blip, higher on the first beat of the bar.
    double phase = 0.0, phaseInc = 0.0;
    float env = 0.0f, envDecay = 0.0f, attack = 0.0f;
    void trigger (bool downbeat) noexcept;
};
