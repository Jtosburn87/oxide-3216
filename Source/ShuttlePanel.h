#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TapeMachine.h"

/** The jog / shuttle wheel as on a tape machine's remote: a spring-loaded shuttle ring (turn it to wind either way,
    faster the further it turns; let go and it springs back and stops) round a free-turning jog wheel (one turn =
    one second of tape). A mouse wheel over it jogs; double-click the ring to stop. */
class ShuttleWheel : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    explicit ShuttleWheel (TapeMachine&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int steps = 7;   // shuttle speeds each way: 0.5x .. 32x
    static float speedForStep (int step) noexcept;

    /** Keyboard: turn the jog wheel one notch (1/24 turn) back (-1) or forward (+1). */
    void jogNotch (int direction);
    /** Keyboard: turn the shuttle ring one step further back (-1) or forward (+1). It stays there (a key can't
        hold it like a hand) until the other key steps it back, the mouse takes it, or Stop / Play / a locate. */
    void shuttleStep (int direction);

private:
    TapeMachine& tape;
    float ring = 0.0f;          // -1..1: how far the ring is turned (1 = 60 degrees)
    float jogAngle = 0.0f;      // radians, for the drawing
    int sentStep = 0;
    bool heldByKeys = false;    // the ring was stepped from the keyboard: no spring back
    enum class Drag { none, ring, jog } drag = Drag::none;
    float lastAngle = 0.0f;

    float outerRadius() const noexcept;
    float innerRadius() const noexcept { return outerRadius() * 0.64f; }
    float angleAt (juce::Point<float>) const noexcept;
    void sendShuttle();
    void timerCallback() override;
};

/** The Jog / shuttle box in the centre section: tape position and speed readouts, Jog / Scrub, the In / Out keys
    (Set In, Set Out, Loop, Go In, Go Out, Punch - the same switches as under the tape machine), the wheel, and six
    locate memories (Store, then a number; a number locates; RTZ returns to zero). */
class ShuttlePanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit ShuttlePanel (TapeMachine&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Keyboard shortcuts: exactly what clicking that key on the panel does. jogScrub swaps Jog and Scrub. */
    enum class Key { jogScrub, store, rtz, setIn, setOut, loop, goIn, goOut, punch,
                     jogBack, jogForward, shuttleBack, shuttleForward };   // the wheel: one notch / one ring step
    void press (Key);
    void pressMemory (int m);

private:
    /** A locate memory key: an amber line along the bottom while it holds a position. */
    class MemoryButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        bool stored = false;
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    };

    TapeMachine& tape;
    ShuttleWheel wheel;
    juce::TextButton jogMode { "Jog" }, scrubMode { "Scrub" };
    juce::TextButton setIn { "Set In" }, setOut { "Set Out" }, loop { "Loop" },
                     goIn { juce::CharPointer_UTF8 ("\xe2\x97\x80 Go In") }, goOut { juce::CharPointer_UTF8 ("Go Out \xe2\x96\xb6") }, punch { "Punch" };
    juce::TextButton store { "Store" }, rtz { "RTZ" };
    std::array<MemoryButton, TapeMachine::numMemories> memories;
    bool storing = false;
    juce::String info { "Store, then 1-6" };

    juce::String shownTime, shownSpeed, shownInOut;
    juce::Rectangle<int> clockWell, speedWell;

    juce::String formatTime (juce::int64 samples) const;
    void memoryClicked (int m);
    void timerCallback() override;
};
