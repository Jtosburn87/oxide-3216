#pragma once

#include <juce_events/juce_events.h>

/** The Wi-Fi remote's server: one port on every network adapter (or one address) serving the remote's web page over
    HTTP and a WebSocket on the same port. Every request needs the secret key from the link (?k=...), so only phones
    that scanned the QR code get in. The page sends JSON messages ({"type":"press","action":"play"} etc.); the server
    sends the state to every phone whenever it changes (and every 2 s anyway).

    Message thread: makeState, onMessage and tick(). Each phone is read on its own thread. */
class RemoteServer : private juce::Thread,
                     private juce::Timer,
                     private juce::AsyncUpdater
{
public:
    struct Options
    {
        juce::String address;   // "" = every network adapter
        juce::String key;       // not empty: every request needs ?k=<key>
        juce::String page;      // served for GET / (the remote's web page)
    };

    RemoteServer (int port, Options options);
    ~RemoteServer() override;

    /** The state message for the phones (JSON text). */
    std::function<juce::String()> makeState;
    /** More messages for the phones besides the state (the channel strips, the meters). Each goes to new phones and
        whenever its text changes; everyTick ones go out on every tick. An empty text sends nothing. */
    void addFeed (std::function<juce::String()> make, bool everyTick);
    /** A message from a phone. */
    std::function<void (const juce::var& message)> onMessage;

    int numClients() const;
    bool isListening() const { return listening.load(); }
    /** True once the listener couldn't be opened (port taken). */
    bool failed() const { return listenFailed.load(); }
    /** Sends the state to everyone on the next tick even if it didn't change. */
    void resend() { lastState = {}; }

    /** Drops phones that went away and sends the state where needed. Runs on a timer; tests call it. */
    void tick();
    /** Hands messages from the phones to onMessage. Runs by itself on the message thread; tests call it. */
    void processIncoming();

    /** RFC 6455: Sec-WebSocket-Accept for a Sec-WebSocket-Key. */
    static juce::String acceptKey (const juce::String& key);

    static constexpr int ticksPerSecond = 15;

private:
    class Client;

    void run() override;            // accepts connections
    void timerCallback() override { tick(); }
    void handleAsyncUpdate() override { processIncoming(); }

    const int port;
    const Options options;
    juce::StreamingSocket server;
    std::atomic<bool> listening { false }, listenFailed { false };
    juce::CriticalSection clientLock;
    juce::OwnedArray<Client> clients;
    juce::CriticalSection incomingLock;
    juce::StringArray incoming;
    juce::String lastState;
    struct Feed { std::function<juce::String()> make; bool everyTick; juce::String last; };
    std::vector<Feed> feeds;
    int ticksSinceSent = 0;

    JUCE_DECLARE_NON_COPYABLE (RemoteServer)
};
