#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <map>

/** A MIDI note or controller that presses something: a shortcut, a key in the Jog / shuttle window. */
struct MidiBinding
{
    enum Kind { none = 0, note = 1, cc = 2 };
    Kind kind = none;
    int channel = 0;   // 0 = any, 1-16
    int number = 0;    // note or controller number

    bool isSet() const noexcept { return kind != none; }
    /** A note on, or a controller, on this number (and channel, unless any). */
    bool matches (const juce::MidiMessage&) const;
    /** A press: note on, or a controller above 0 (a button let go sends 0). */
    static bool isPress (const juce::MidiMessage&);
    /** The binding for a note on or controller (keepChannel: that channel, else any). Unset for anything else. */
    static MidiBinding from (const juce::MidiMessage&, bool keepChannel);
    juce::String text() const;          // "Note C3 (ch 1)", "CC 20", or a dash
    juce::String toString() const;      // "kind channel number"
    static MidiBinding fromString (const juce::String&);
    bool operator== (const MidiBinding& o) const { return kind == o.kind && channel == o.channel && number == o.number; }
};

/** MIDI on the keyboard shortcuts (Options > Keyboard shortcuts, the MIDI tab): a note or controller per command,
    from any MIDI input. Learn takes the next note or controller for a command (another command on it gives it up).
    Message thread. */
class MidiShortcuts
{
public:
    /** Does the command. */
    std::function<void (int commandId)> invoke;
    /** Something changed (save the settings). */
    std::function<void()> onChange;

    MidiBinding get (int command) const;
    void set (int command, MidiBinding);
    void clear (int command) { set (command, {}); }
    void learn (int command);   // -1: stop learning
    int getLearning() const noexcept { return learning; }
    std::uint32_t getRevision() const noexcept { return revision; }

    /** A message from a MIDI input: learnt, or the commands on it are done. True if it learnt or did something. */
    bool handle (const juce::MidiMessage&);

    juce::String toString() const;
    void fromString (const juce::String&);

private:
    std::map<int, MidiBinding> bindings;
    int learning = -1;
    std::uint32_t revision = 0;
    void changed();
};

/** MIDI control for the Jog / shuttle window: one input (or all) and channel, a controller for the jog wheel
    (relative encoder, or absolute) and the shuttle ring (absolute, 64 = centre), and a note or controller for each key.
    Learn as for the shortcuts. Message thread. */
class ShuttleMidi
{
public:
    enum Control { jogWheel, shuttleRing, jog, scrub, setIn, setOut, loop, goIn, goOut, punch, store, rtz,
                   pageUp, pageDown, slot1, slot2, slot3, slot4, slot5, slot6, numControls };
    static juce::String name (Control);
    static bool isContinuous (Control c) noexcept { return c == jogWheel || c == shuttleRing; }

    /** How the jog wheel's controller counts: relative (1..63 forward, 65..127 back), relative around 64
        (65.. forward, ..63 back), or absolute (the change from the last value). */
    enum class Encoder { twosComplement = 0, offset64 = 1, absolute = 2 };

    static constexpr const char* allInputs = "*";
    juce::String input;   // empty: none, allInputs, or a device identifier
    int channel = 0;      // 0 = any
    Encoder encoder = Encoder::twosComplement;

    /** Does a control: amount = jog notches (either way) for the wheel, a ring step (-7..7) for the ring, 1 for a key. */
    std::function<void (Control, int amount)> act;
    std::function<void()> onChange;

    MidiBinding get (Control c) const { return bindings[(size_t) c]; }
    void set (Control, MidiBinding);
    void setInput (const juce::String&);
    void setChannel (int);
    void setEncoder (Encoder);
    void learn (int control);   // -1: stop
    int getLearning() const noexcept { return learning; }
    std::uint32_t getRevision() const noexcept { return revision; }

    /** A message from a MIDI input. True if it learnt or did something. */
    bool handle (const juce::MidiMessage&, const juce::String& deviceIdentifier);

    juce::String toString() const;   // JSON
    void fromString (const juce::String&);

private:
    std::array<MidiBinding, numControls> bindings {};
    int learning = -1;
    int lastAbsolute = -1;
    std::uint32_t revision = 0;
    void changed();
};
