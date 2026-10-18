#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

/** The computer's MIDI outputs, for the MIDI page: tracks playing hardware synths and MIDI clock.

    Each output a track or the clock asks for gets a port number (0..maxPorts-1) for as long as the app runs, so the
    audio thread only ever deals in numbers. The audio thread queues short messages with the time they should go
    out (Time::getMillisecondCounterHiRes) through a lock-free FIFO; a 1 ms high resolution timer sends each one on time.
    Devices are only opened after enable() (the app calls it; the tests never do, and read the queue instead). */
class MidiOuts : private juce::HighResolutionTimer
{
public:
    static constexpr int maxPorts = 32;

    MidiOuts();
    ~MidiOuts() override;

    /** Opens the ports asked for so far (and any asked for later), and starts sending. */
    void enable();
    bool isEnabled() const noexcept { return enabled; }
    /** Something sends through the ports (a track's MIDI out, the clock). The 1 ms sending timer only runs while it
        does (and until what is queued has gone out), so a session without MIDI out leaves the system timer alone. */
    void setActive (bool);

    // Message thread -----------------------------------------------------------------
    /** The port for a device identifier (opened if it can be), or -1 for an empty identifier or no room. */
    int portFor (const juce::String& identifier);
    juce::String identifier (int port) const;
    /** The port's device is open (it was there and nothing else had it). */
    bool isOpen (int port) const noexcept;
    /** Tries again to open ports whose device was missing (plugged in later). */
    void reopenMissing();
    /** The MIDI outputs on this computer. */
    static juce::Array<juce::MidiDeviceInfo> availableDevices() { return juce::MidiOutput::getAvailableDevices(); }
    /** A device's name from its identifier (the identifier itself if it is not plugged in). */
    static juce::String nameOf (const juce::String& identifier);

    // Audio thread -------------------------------------------------------------------------
    /** Queues a message of 1 to 3 bytes for a port, to go out at timeMs. False if the queue is full. */
    bool push (int port, double timeMs, const std::uint8_t* data, int size) noexcept;
    bool push (int port, double timeMs, const juce::MidiMessage& m) noexcept
    {
        return m.getRawDataSize() <= 3 && push (port, timeMs, m.getRawData(), m.getRawDataSize());
    }

    /** A queued message. */
    struct Item
    {
        double time = 0.0;
        int port = -1;
        std::uint8_t data[3] {};
        std::uint8_t size = 0;
        juce::MidiMessage message() const { return juce::MidiMessage (data, size, time * 0.001); }
    };
    /** For the tests (sending not enabled): takes everything queued so far, in the order it was queued. */
    std::vector<Item> takeQueued();

    /** Sends a message straight away, past the queue. Message thread. */
    void sendNow (int port, const juce::MidiMessage&);
    /** Sustain up and all notes off on every channel, straight away. Message thread. */
    void allNotesOff (int port);

private:
    struct Port
    {
        juce::String identifier;
        std::unique_ptr<juce::MidiOutput> device;
        std::atomic<bool> open { false };
    };
    std::array<Port, maxPorts> ports;
    std::atomic<int> numPorts { 0 };
    bool enabled = false;
    std::atomic<bool> active { false };
    juce::CriticalSection portLock;   // opening (message thread) against sending (the timer thread)

    juce::AbstractFifo fifo { 8192 };
    std::vector<Item> queue = std::vector<Item> (8192);

    void open (Port&);
    std::vector<Item> pending;   // the timer thread's, sorted by time
    void hiResTimerCallback() override;

    JUCE_DECLARE_NON_COPYABLE (MidiOuts)
};
