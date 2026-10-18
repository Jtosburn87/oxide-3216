#include "MidiOut.h"
#include <algorithm>

MidiOuts::MidiOuts()
{
    pending.reserve (8192);
}

MidiOuts::~MidiOuts()
{
    stopTimer();
    const juce::ScopedLock sl (portLock);
    for (int p = 0; p < numPorts.load(); ++p)
        ports[(size_t) p].device.reset();
}

void MidiOuts::enable()
{
    if (enabled)
        return;
    enabled = true;
    for (int p = 0; p < numPorts.load(); ++p)
        open (ports[(size_t) p]);
    if (active.load())
        startTimer (1);
}

void MidiOuts::setActive (bool on)
{
    active = on;
    if (on && enabled && ! isTimerRunning())
        startTimer (1);   // stopping waits for the queue to empty (hiResTimerCallback)
}

int MidiOuts::portFor (const juce::String& id)
{
    if (id.isEmpty())
        return -1;
    const int n = numPorts.load();
    for (int p = 0; p < n; ++p)
        if (ports[(size_t) p].identifier == id)
            return p;
    if (n >= maxPorts)
        return -1;
    auto& port = ports[(size_t) n];
    port.identifier = id;
    if (enabled)
        open (port);
    numPorts.store (n + 1);
    return n;
}

juce::String MidiOuts::identifier (int p) const
{
    return juce::isPositiveAndBelow (p, numPorts.load()) ? ports[(size_t) p].identifier : juce::String();
}

bool MidiOuts::isOpen (int p) const noexcept
{
    return juce::isPositiveAndBelow (p, numPorts.load()) && ports[(size_t) p].open.load();
}

void MidiOuts::open (Port& port)
{
    if (port.open.load())
        return;
    auto device = juce::MidiOutput::openDevice (port.identifier);
    if (device == nullptr)
        return;
    const juce::ScopedLock sl (portLock);
    port.device = std::move (device);
    port.open = true;
}

void MidiOuts::reopenMissing()
{
    if (! enabled)
        return;
    const auto devices = availableDevices();
    for (int p = 0; p < numPorts.load(); ++p)
    {
        auto& port = ports[(size_t) p];
        const bool present = std::any_of (devices.begin(), devices.end(), [&] (const juce::MidiDeviceInfo& d) { return d.identifier == port.identifier; });
        if (port.open.load() && ! present)
        {
            // Unplugged: let it go, so plugging it back in opens it afresh.
            const juce::ScopedLock sl (portLock);
            port.open = false;
            port.device.reset();
        }
        else if (! port.open.load() && present)
            open (port);
    }
}

juce::String MidiOuts::nameOf (const juce::String& id)
{
    for (auto& d : availableDevices())
        if (d.identifier == id)
            return d.name;
    return id;
}

bool MidiOuts::push (int port, double timeMs, const std::uint8_t* data, int size) noexcept
{
    if (! juce::isPositiveAndBelow (port, maxPorts) || size < 1 || size > 3)
        return false;
    const auto scope = fifo.write (1);
    if (scope.blockSize1 < 1)
        return false;
    auto& item = queue[(size_t) scope.startIndex1];
    item.time = timeMs;
    item.port = port;
    item.size = (std::uint8_t) size;
    std::copy (data, data + size, item.data);
    return true;
}

std::vector<MidiOuts::Item> MidiOuts::takeQueued()
{
    std::vector<Item> items;
    const auto scope = fifo.read (fifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) items.push_back (queue[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) items.push_back (queue[(size_t) (scope.startIndex2 + i)]);
    return items;
}

void MidiOuts::sendNow (int p, const juce::MidiMessage& m)
{
    const juce::ScopedLock sl (portLock);
    if (isOpen (p))
        ports[(size_t) p].device->sendMessageNow (m);
}

void MidiOuts::allNotesOff (int p)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        sendNow (p, juce::MidiMessage::controllerEvent (ch, 64, 0));   // sustain up
        sendNow (p, juce::MidiMessage::allNotesOff (ch));
    }
}

void MidiOuts::hiResTimerCallback()
{
    // New messages into the time-sorted list (after any at the same time, so their order holds).
    const auto scope = fifo.read (fifo.getNumReady());
    const auto take = [this] (int start, int count)
    {
        for (int i = start; i < start + count; ++i)
        {
            const auto& item = queue[(size_t) i];
            pending.insert (std::upper_bound (pending.begin(), pending.end(), item.time,
                                              [] (double t, const Item& o) { return t < o.time; }), item);
        }
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
    if (pending.empty())
    {
        if (! active.load() && fifo.getNumReady() == 0)
        {
            stopTimer();   // nothing sends any more and everything has gone out
            if (active.load())
                startTimer (1);   // switched back on meanwhile
        }
        return;
    }

    // Everything due (half a millisecond early is closer than a millisecond late).
    const double now = juce::Time::getMillisecondCounterHiRes() + 0.5;
    size_t sent = 0;
    {
        const juce::ScopedLock sl (portLock);
        for (; sent < pending.size() && pending[sent].time <= now; ++sent)
        {
            const auto& item = pending[sent];
            if (juce::isPositiveAndBelow (item.port, numPorts.load()) && ports[(size_t) item.port].open.load())
                ports[(size_t) item.port].device->sendMessageNow (juce::MidiMessage (item.data, item.size));
        }
    }
    pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) sent);
}
