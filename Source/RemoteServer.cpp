#include "RemoteServer.h"

namespace
{
    /** SHA-1 (only for the WebSocket handshake). */
    std::array<juce::uint8, 20> sha1 (const juce::String& text)
    {
        juce::MemoryBlock data (text.toRawUTF8(), text.getNumBytesAsUTF8());
        const juce::uint64 bitLength = (juce::uint64) data.getSize() * 8;
        data.append ("\x80", 1);
        while (data.getSize() % 64 != 56)
            data.append ("\0", 1);
        for (int i = 7; i >= 0; --i)
        {
            const auto b = (char) ((bitLength >> (i * 8)) & 0xff);
            data.append (&b, 1);
        }

        juce::uint32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
        auto rol = [] (juce::uint32 v, int n) { return (v << n) | (v >> (32 - n)); };
        const auto* bytes = static_cast<const juce::uint8*> (data.getData());
        for (size_t chunk = 0; chunk < data.getSize(); chunk += 64)
        {
            juce::uint32 w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (juce::uint32) bytes[chunk + (size_t) i * 4] << 24 | (juce::uint32) bytes[chunk + (size_t) i * 4 + 1] << 16
                     | (juce::uint32) bytes[chunk + (size_t) i * 4 + 2] << 8 | (juce::uint32) bytes[chunk + (size_t) i * 4 + 3];
            for (int i = 16; i < 80; ++i)
                w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            juce::uint32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i)
            {
                juce::uint32 f, k;
                if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
                const juce::uint32 t = rol (a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol (b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        std::array<juce::uint8, 20> out {};
        for (int i = 0; i < 20; ++i)
            out[(size_t) i] = (juce::uint8) (h[i / 4] >> (24 - (i % 4) * 8));
        return out;
    }

}

juce::String RemoteServer::acceptKey (const juce::String& key)
{
    const auto digest = sha1 (key.trim() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return juce::Base64::toBase64 (digest.data(), digest.size());
}

// ---------------------------------------------------------------------------------------------

/** One connected phone: reads its frames on its own thread, sends from the message thread. */
class RemoteServer::Client : private juce::Thread
{
public:
    Client (std::unique_ptr<juce::StreamingSocket> s, std::function<void (const juce::String&)> onText, const Options& o)
        : juce::Thread ("Remote phone"), socket (std::move (s)), received (std::move (onText)), options (o)
    {
        startThread();
    }

    ~Client() override
    {
        signalThreadShouldExit();
        socket->close();
        stopThread (2000);
    }

    bool isOpen() const { return open.load(); }
    bool isNew() { return fresh.exchange (false); }

    void send (const juce::String& text)
    {
        if (! open.load() || ! handshakeDone.load())
            return;
        const auto size = (size_t) text.getNumBytesAsUTF8();
        juce::MemoryBlock frame;
        juce::uint8 head[10];
        int headSize = 2;
        head[0] = 0x81;   // final frame, text
        if (size < 126)
            head[1] = (juce::uint8) size;
        else if (size < 65536)
        {
            head[1] = 126;
            head[2] = (juce::uint8) (size >> 8);
            head[3] = (juce::uint8) size;
            headSize = 4;
        }
        else
        {
            head[1] = 127;
            for (int i = 0; i < 8; ++i)
                head[2 + i] = (juce::uint8) ((juce::uint64) size >> (56 - i * 8));
            headSize = 10;
        }
        frame.append (head, (size_t) headSize);
        frame.append (text.toRawUTF8(), size);
        const juce::ScopedLock sl (writeLock);
        if (socket->write (frame.getData(), (int) frame.getSize()) < 0)
            open.store (false);
    }

private:
    bool readExactly (void* dest, int bytes)
    {
        auto* p = static_cast<char*> (dest);
        while (bytes > 0 && ! threadShouldExit())
        {
            const int ready = socket->waitUntilReady (true, 200);
            if (ready < 0)
                return false;
            if (ready == 0)
                continue;
            const int n = socket->read (p, bytes, false);
            if (n <= 0)
                return false;
            p += n;
            bytes -= n;
        }
        return bytes == 0;
    }

    /** An HTTP reply, then the connection closes. */
    bool reply (const juce::String& status, const juce::String& type, const juce::String& body)
    {
        const auto all = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + juce::String ((int) body.getNumBytesAsUTF8())
                       + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
        const juce::ScopedLock sl (writeLock);
        socket->write (all.toRawUTF8(), (int) all.getNumBytesAsUTF8());
        return false;
    }

    bool handshake()
    {
        juce::String request;
        char c = 0;
        while (request.length() < 8192 && ! request.endsWith ("\r\n\r\n"))
        {
            if (! readExactly (&c, 1))
                return false;
            request += c;
        }
        const auto lines = juce::StringArray::fromLines (request);
        juce::String key;
        for (auto& line : lines)
            if (line.startsWithIgnoreCase ("Sec-WebSocket-Key:"))
                key = line.fromFirstOccurrenceOf (":", false, false).trim();

        // "GET /path?k=... HTTP/1.1"
        const auto target = lines[0].fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
        const auto path = target.upToFirstOccurrenceOf ("?", false, false);
        juce::String linkKey;
        for (auto& param : juce::StringArray::fromTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", {}))
            if (param.startsWith ("k="))
                linkKey = param.substring (2);
        if (options.key.isNotEmpty() && linkKey != options.key)
            return reply ("403 Forbidden", "text/plain; charset=utf-8", "This link is old or wrong: scan the QR code in Oxide-3216 again.");

        if (key.isEmpty())
        {
            if (options.page.isNotEmpty() && (path == "/" || path == "/remote"))
                return reply ("200 OK", "text/html; charset=utf-8", options.page);
            return reply ("404 Not Found", "text/plain; charset=utf-8", "Not here.");
        }
        const auto answer = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " + acceptKey (key) + "\r\n\r\n";
        const juce::ScopedLock sl (writeLock);
        return socket->write (answer.toRawUTF8(), (int) answer.getNumBytesAsUTF8()) > 0;
    }

    void run() override
    {
        if (! handshake())
        {
            open.store (false);
            return;
        }
        handshakeDone.store (true);
        fresh.store (true);
        while (! threadShouldExit())
        {
            juce::uint8 head[2];
            if (! readExactly (head, 2))
                break;
            const int opcode = head[0] & 0x0f;
            const bool masked = (head[1] & 0x80) != 0;
            juce::uint64 length = head[1] & 0x7f;
            if (length == 126)
            {
                juce::uint8 ext[2];
                if (! readExactly (ext, 2)) break;
                length = (juce::uint64) ext[0] << 8 | ext[1];
            }
            else if (length == 127)
            {
                juce::uint8 ext[8];
                if (! readExactly (ext, 8)) break;
                length = 0;
                for (auto b : ext)
                    length = length << 8 | b;
            }
            if (length > (1u << 20))
                break;   // nothing the page sends is that big
            juce::uint8 mask[4] = {};
            if (masked && ! readExactly (mask, 4))
                break;
            juce::MemoryBlock payload ((size_t) length);
            if (length > 0 && ! readExactly (payload.getData(), (int) length))
                break;
            auto* p = static_cast<juce::uint8*> (payload.getData());
            for (size_t i = 0; i < (size_t) length; ++i)
                p[i] ^= mask[i % 4];

            if (opcode == 0x8)          // close
                break;
            if (opcode == 0x9)          // ping -> pong
            {
                juce::uint8 pong[2] = { 0x8a, 0 };
                const juce::ScopedLock sl (writeLock);
                socket->write (pong, 2);
            }
            else if (opcode == 0x1)
                received (payload.toString());
        }
        open.store (false);
    }

    std::unique_ptr<juce::StreamingSocket> socket;
    std::function<void (const juce::String&)> received;
    const Options options;
    juce::CriticalSection writeLock;
    std::atomic<bool> open { true }, handshakeDone { false }, fresh { false };
};

// ---------------------------------------------------------------------------------------------

RemoteServer::RemoteServer (int listenPort, Options o)
    : juce::Thread ("Remote server"), port (listenPort), options (std::move (o))
{
    startThread();
    startTimerHz (ticksPerSecond);
}

RemoteServer::~RemoteServer()
{
    stopTimer();
    cancelPendingUpdate();
    signalThreadShouldExit();
    server.close();
    stopThread (3000);
    const juce::ScopedLock sl (clientLock);
    clients.clear();
}

int RemoteServer::numClients() const
{
    const juce::ScopedLock sl (clientLock);
    int n = 0;
    for (auto* c : clients)
        n += c->isOpen() ? 1 : 0;
    return n;
}

void RemoteServer::run()
{
    if (! server.createListener (port, options.address))
    {
        listenFailed.store (true);
        return;   // port taken (another Oxide?): no remote
    }
    listening.store (true);
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> socket (server.waitForNextConnection());
        if (socket == nullptr)
            continue;
        auto onText = [this] (const juce::String& text)
        {
            {
                const juce::ScopedLock sl (incomingLock);
                if (incoming.size() < 256)
                    incoming.add (text);
            }
            triggerAsyncUpdate();
        };
        const juce::ScopedLock sl (clientLock);
        clients.add (new Client (std::move (socket), onText, options));
    }
    listening.store (false);
}

void RemoteServer::processIncoming()
{
    juce::StringArray messages;
    {
        const juce::ScopedLock sl (incomingLock);
        messages.swapWith (incoming);
    }
    for (auto& text : messages)
    {
        const auto v = juce::JSON::parse (text);
        if (v.isObject() && onMessage)
            onMessage (v);
    }
}

void RemoteServer::tick()
{
    // drop phones that went away
    {
        const juce::ScopedLock sl (clientLock);
        for (int i = clients.size(); --i >= 0;)
            if (! clients[i]->isOpen())
                clients.remove (i);
    }
    if (numClients() == 0 || makeState == nullptr)
        return;

    const auto state = makeState();
    const bool changed = state != lastState || ++ticksSinceSent >= 2 * ticksPerSecond;   // and every 2 s anyway
    const juce::ScopedLock sl (clientLock);
    std::vector<bool> fresh;
    for (auto* c : clients)
    {
        fresh.push_back (c->isNew());
        if (fresh.back() || changed)
            c->send (state);
    }
    if (changed)
    {
        lastState = state;
        ticksSinceSent = 0;
    }

    for (auto& feed : feeds)
    {
        const auto text = feed.make();
        if (text.isEmpty())
            continue;
        const bool feedChanged = feed.everyTick || text != feed.last;
        for (int i = 0; i < clients.size(); ++i)
            if (fresh[(size_t) i] || feedChanged)
                clients[i]->send (text);
        feed.last = text;
    }
}

void RemoteServer::addFeed (std::function<juce::String()> make, bool everyTick)
{
    feeds.push_back ({ std::move (make), everyTick, {} });
}
