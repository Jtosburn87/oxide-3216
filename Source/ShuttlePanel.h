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
    /** MIDI: the ring to a step (-7..7), held there like the keys hold it (0: let go, the tape stops). */
    void setStep (int step);
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
    (Set In, Set Out, Loop, Go In, Go Out, Punch - the same switches as under the tape machine), the wheel, and the
    locate memories (Store, then a number; a number locates; RTZ returns to zero).

    On the console it shows memories 1-6; right-click it anywhere, or double-click its background, for the Jog /
    shuttle window. The window's copy (windowCopy) shows six of the 99 memories at a time: the mouse wheel over them,
    or the up / down keys beside them, move the numbers on (two at a time, a row). Store and the window's page are
    shared by every copy. */
class ShuttlePanel : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    explicit ShuttlePanel (TapeMachine&, bool windowCopy = false);
    ~ShuttlePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int designW = 526, designH = 236;   // the console's box
    static constexpr int copyW = designW + 30;            // the window's copy: room for the up / down keys
    static constexpr int shownMemories = 6;

    /** Right-click / double-click on the console's box: open the Jog / shuttle window. */
    std::function<void()> onOpenWindow;

    /** Keyboard shortcuts and MIDI: exactly what clicking that key on the panel does. jogScrub swaps Jog and Scrub. */
    enum class Key { jogScrub, store, rtz, setIn, setOut, loop, goIn, goOut, punch,
                     jogBack, jogForward, shuttleBack, shuttleForward,   // the wheel: one notch / one ring step
                     jog, scrub, pageUp, pageDown };                      // Jog, Scrub as they are; the window's rows
    void press (Key);
    /** A locate memory, 0..98: locates there (or keeps the tape position there after Store). */
    void pressMemory (int m);
    /** One of the window's six memory keys (0..5), whatever number it shows now. */
    void pressSlot (int slot);
    /** MIDI: turn the jog wheel by notches (1/24 turn each, either way); set the shuttle ring to a step (-7..7, held). */
    void jogBy (int notches);
    void setShuttleStep (int step);

    /** The first memory the window's six keys show (0, 2, 4 ... 94). */
    static int getPage() noexcept { return page; }
    static void scrollPage (int rows);

    /** For the Wi-Fi remote: Store is waiting for a number, and the box's last message ("To 3 (00:12.0)"; empty at first). */
    static bool isStoring() noexcept { return storing; }
    static juce::String getMessage() { return sharedInfo; }

private:
    /** A key that only answers the left button: a right-click on the box opens the window instead of pressing it. */
    class KeyButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::TextButton::mouseDown (e); }
        void mouseUp (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::TextButton::mouseUp (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::TextButton::mouseDrag (e); }
    };

    /** A locate memory key: an amber line along the bottom while it holds a position. */
    class MemoryButton : public KeyButton
    {
    public:
        using KeyButton::KeyButton;
        bool stored = false;
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    };

    TapeMachine& tape;
    const bool copy;
    ShuttleWheel wheel;
    KeyButton jogMode { "Jog" }, scrubMode { "Scrub" };
    KeyButton setIn { "Set In" }, setOut { "Set Out" }, loop { "Loop" },
              goIn { juce::CharPointer_UTF8 ("\xe2\x97\x80 Go In") }, goOut { juce::CharPointer_UTF8 ("Go Out \xe2\x96\xb6") }, punch { "Punch" };
    KeyButton store { "Store" }, rtz { "RTZ" };
    KeyButton up { juce::CharPointer_UTF8 ("\xe2\x96\xb2") }, down { juce::CharPointer_UTF8 ("\xe2\x96\xbc") };
    std::array<MemoryButton, shownMemories> memories;
    static inline bool storing = false;   // Store pressed, waiting for a number (on any copy)
    static inline int page = 0;
    static inline juce::String sharedInfo;
    juce::String info;

    juce::String shownTime, shownSpeed, shownInOut;
    int shownPage = -1;
    juce::Rectangle<int> clockWell, speedWell;

    int memoryFor (int slot) const noexcept { return (copy ? page : 0) + slot; }
    juce::String formatTime (juce::int64 samples) const;
    juce::String idleInfo() const { return copy ? "Store, then a number" : "Store, then 1-6"; }
    void memoryClicked (int m);
    void timerCallback() override;
};
