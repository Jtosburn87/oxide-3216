#pragma once

#include "ChannelStrip.h"
#include "TapeMachine.h"
#include <juce_events/juce_events.h>
#include <map>
#include <vector>

/** Fader, pan and mute automation of the console's channel strips, drawn on the MIDI/WAV page or written by moving the
    controls. It runs on the tape: a curve's points are tape positions, so it plays with the tape at any tempo or speed.

    Every strip has a mode. Off: its curves do nothing. Read: they move the controls while the tape rolls, and when it
    is located. Touch: as Read, but a control you move takes over and is written until you let go, then it glides back
    to the curve. Latch: the same, but it stays yours until the tape stops. Any way of moving the control counts: the
    mouse, the Wi-Fi remote or a MIDI controller. A mute written in Touch stays until the tape stops, as there is
    nothing to let go of.

    Audio thread: process() reads the curves under a lock it only ever tries, and sets the controls without listeners
    (Param::setFromAudio). Message thread: everything else; tick() runs on a timer and passes the moves on to the
    faders on screen, stereo links and so on. */
class Automation : private Param::Listener,
                   private juce::Timer
{
public:
    enum Target { fader, pan, mute, numTargets };
    enum class Mode { off, read, touch, latch };

    /** A point on a curve: where on the tape, and the control's travel (0..1). Mute: 1 = muted. */
    struct Point
    {
        juce::int64 pos = 0;
        float value = 0.0f;
        bool operator== (const Point& o) const { return pos == o.pos && value == o.value; }
    };
    using Curve = std::vector<Point>;

    static constexpr int numStrips = Config::numInputStrips + Config::numReturnStrips;
    static int stripIndex (bool isReturn, int index) noexcept { return (isReturn ? Config::numInputStrips : 0) + index; }

    /** How long a control may rest before Touch lets go of it (unless the mouse is still holding it). */
    static constexpr double touchReleaseMs = 500.0;
    /** How long Touch takes to glide back to the curve, in seconds. */
    static constexpr double glideSeconds = 0.1;

    explicit Automation (TapeMachine&);
    ~Automation() override;

    /** Once, when the strips exist. */
    void bind (const juce::OwnedArray<ChannelStrip>& inputs, const juce::OwnedArray<ChannelStrip>& returns);

    // Message thread.
    const Curve& getCurve (int strip, Target t) const { return curves[(size_t) strip][(size_t) t]; }
    /** Replaces a curve (it gets sorted; two points may share a position, which makes a step). */
    void setCurve (int strip, Target, Curve);
    /** Mute from `from` to `to`: `muted` across the range, what was there on either side of it. */
    void setMuteRange (int strip, juce::int64 from, juce::int64 to, bool muted);

    Mode getMode (int strip) const noexcept { return modes[(size_t) strip].load(); }
    void setMode (int strip, Mode);
    bool hasCurves (int strip) const;
    /** The curve's value at a tape position; `fallback` with no points. */
    float valueAt (int strip, Target, juce::int64 pos, float fallback) const;

    /** The control a curve moves. */
    Param* getParam (int strip, Target t) const { return params[(size_t) strip][(size_t) t]; }

    /** What is being written right now (empty when nothing is), for the page to draw over the curve. */
    const Curve& getWriting (int strip, Target t) const { return writes[(size_t) strip][(size_t) t].points; }

    void clearCurve (int strip, Target t) { setCurve (strip, t, {}); }
    void clearStrip (int strip);
    /** No curves, every strip in Read. */
    void clearAll();

    /** Goes up with every change of a curve or a mode (the page's repaint, the session fingerprint). */
    std::uint32_t getRevision() const noexcept { return revision; }

    /** True while the mouse is still holding a control down, so Touch doesn't let go of a fader held still. */
    std::function<bool()> isMouseHeld;

    /** Passes moves on and ends writes. Runs 30 times a second; tests call it directly. */
    void tick();

    juce::var toVar() const;
    void fromVar (const juce::var&);

    // Audio thread, at the start of every block.
    void process() noexcept;

    /** A curve's value at a position (linear between points; mute holds each point until the next). */
    static float evaluate (const Curve&, Target, juce::int64 pos) noexcept;

private:
    struct Write
    {
        bool active = false;
        juce::int64 start = 0, last = 0;
        double lastMove = 0.0;
        float before = 0.0f;   // the control before it was moved
        Curve points;
    };

    TapeMachine& tape;
    std::array<std::array<Param*, numTargets>, numStrips> params {};
    std::map<const Param*, std::pair<int, int>> byParam;   // -> strip, target
    std::array<std::array<Curve, numTargets>, numStrips> curves;   // message thread
    std::array<std::array<Curve, numTargets>, numStrips> live;     // the audio thread's copy, swapped under lock
    std::array<std::atomic<Mode>, numStrips> modes;
    std::array<std::array<std::atomic<bool>, numTargets>, numStrips> writing;   // the audio thread leaves these alone
    std::array<std::array<Write, numTargets>, numStrips> writes;
    std::array<std::array<float, numTargets>, numStrips> shadow {};   // each control before the latest move
    juce::SpinLock lock;
    std::atomic<bool> anyCurves { false };
    juce::int64 lastPos = -1;   // audio thread
    std::uint32_t revision = 0;
    bool flushing = false;

    void swapIn (int strip, Target);
    void beginOrContinue (int strip, Target, float value);
    void endWrite (int strip, Target, juce::int64 endPos, bool glideBack);
    void updateAnyCurves();
    void paramChanged (Param&) override;
    void timerCallback() override { tick(); }

    JUCE_DECLARE_NON_COPYABLE (Automation)
};
