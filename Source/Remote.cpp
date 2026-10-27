#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <iphlpapi.h>
 #pragma comment (lib, "iphlpapi.lib")
#else
 #include <ifaddrs.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <net/if.h>
#endif

#include "Remote.h"
#include "StripStyle.h"
#include "ConsoleLookAndFeel.h"
#include "BinaryData.h"
#include "../third_party/qrcodegen/qrcodegen.hpp"

using namespace ConsoleColours;

namespace
{
    juce::String makeKey()
    {
        return juce::Uuid().toDashedString().removeCharacters ("-").substring (0, 24);
    }

    const juce::Colour statusGreen { 0xff5cc46a };
}

Remote::Remote (AudioEngine& e, juce::PropertiesFile& s, std::function<Desk&()> d, std::function<void()> save)
    : engine (e), settings (s), desk (std::move (d)), saveSettings (std::move (save))
{
    if (settings.getValue ("remoteKey").isEmpty())
    {
        settings.setValue ("remoteKey", makeKey());
        if (saveSettings) saveSettings();
    }
}

Remote::~Remote()
{
    stopTimer();
    server = nullptr;
    readBanks (false);
}

void Remote::restore()
{
    if (settings.getBoolValue ("remoteOn", false))
        start();
}

void Remote::setOn (bool shouldBeOn)
{
    settings.setValue ("remoteOn", shouldBeOn);
    if (saveSettings) saveSettings();
    if (shouldBeOn && server == nullptr)
        start();
    else if (! shouldBeOn)
    {
        server = nullptr;
        stopTimer();
        readBanks (false);
        if (heldShuttleStep != 0)
            shuttleBox().setShuttleStep (0);
        heldShuttleStep = 0;
    }
    sendChangeMessage();
}

void Remote::start()
{
    RemoteServer::Options options;
    options.address = settings.getValue ("remoteListenAddress");   // normally "": every adapter (Wi-Fi, hotspot, Ethernet); tests use 127.0.0.1
    options.key = settings.getValue ("remoteKey");
    options.page = juce::String::fromUTF8 (BinaryData::remote_html, BinaryData::remote_htmlSize);
    server = nullptr;   // a new key: the old server (and its phones) go first
    server = std::make_unique<RemoteServer> (port, options);
    server->makeState = [this] { return stateJson(); };
    server->onMessage = [this] (const juce::var& m) { handle (m); };
    server->addFeed ([this] { return schemaJson(); }, false);
    server->addFeed ([this] { return stripsJson(); }, false);
    server->addFeed ([this] { return metersJson(); }, true);
    readBanks (true);
    stripsText = {};
    startTimerHz (5);
}

void Remote::newKey()
{
    settings.setValue ("remoteKey", makeKey());
    if (saveSettings) saveSettings();
    if (server != nullptr)
        start();
    sendChangeMessage();
}

juce::String Remote::formatTime (juce::int64 s) const
{
    // the same as the Jog / shuttle box's counter
    const auto sr = engine.tape.getSampleRate();
    const auto tenths = (juce::int64) ((double) juce::jmax ((juce::int64) 0, s) / (sr > 0 ? sr : 48000.0) * 10.0);
    return juce::String::formatted ("%02d:%02d.%d", (int) (tenths / 600), (int) ((tenths / 10) % 60), (int) (tenths % 10));
}

juce::String Remote::stateJson() const
{
    auto& tape = engine.tape;
    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "state");
    root->setProperty ("version", JUCE_APPLICATION_VERSION_STRING);
    root->setProperty ("time", formatTime (tape.getPosition()));
    root->setProperty ("playing", tape.isPlaying());
    root->setProperty ("record", tape.isRecordEnabled());

    // Play / Stop / the wind speed, as the Jog / shuttle box shows it
    const float speed = tape.getWindSpeed();
    juce::String speedText ("Stop");
    if (tape.isPlaying())
        speedText = "Play";
    else if (std::abs (speed) >= 0.05f)
        speedText = juce::String (juce::CharPointer_UTF8 (speed < 0.0f ? "\xe2\x97\x80\xe2\x97\x80 " : "\xe2\x96\xb6\xe2\x96\xb6 "))
                  + (std::abs (speed) < 9.95f ? juce::String (std::abs (speed), 1) : juce::String (juce::roundToInt (std::abs (speed)))) + "x";   // (0 decimals would print them all)
    root->setProperty ("speed", speedText);
    root->setProperty ("wind", tape.getWindDirection());

    root->setProperty ("inPoint", formatTime (tape.getInPoint()));
    root->setProperty ("outPoint", formatTime (tape.getOutPoint()));
    root->setProperty ("loop", tape.loopOn.load());
    root->setProperty ("punch", tape.punchOn.load());
    root->setProperty ("scrub", tape.scrubOn.load());
    root->setProperty ("storing", ShuttlePanel::isStoring());
    // (the console box asks for 1-6 while storing; the remote has all 99 memories)
    root->setProperty ("message", ShuttlePanel::isStoring() ? juce::String ("Now tap a memory number") : ShuttlePanel::getMessage());

    // record arm: 0 off, 1 tape playback until Record (yellow), 2 armed (red); and which tracks record right now
    juce::Array<juce::var> arm, recording;
    for (int t = 0; t < TapeMachine::numTracks; ++t)
    {
        arm.add ((int) tape.getArmState (t));
        recording.add (tape.isRecording (t));
    }
    root->setProperty ("arm", arm);
    root->setProperty ("recording", recording);

    // the locate memories: their times, "" = empty
    juce::Array<juce::var> memories;
    for (int m = 0; m < TapeMachine::numMemories; ++m)
        memories.add (tape.getMemory (m) >= 0 ? formatTime (tape.getMemory (m)) : juce::String());
    root->setProperty ("memories", memories);
    return juce::JSON::toString (juce::var (root), true);
}

void Remote::handle (const juce::var& m)
{
    auto& tape = engine.tape;
    auto& box = shuttleBox();
    using K = ShuttlePanel::Key;
    const auto type = m.getProperty ("type", {}).toString();

    if (type == "press")
    {
        const auto a = m.getProperty ("action", {}).toString();
        if      (a == "play")      tape.play();
        else if (a == "stop")      tape.stop();
        else if (a == "record")    tape.setRecordEnabled (! tape.isRecordEnabled());
        else if (a == "rtz")       box.press (K::rtz);
        else if (a == "store")     box.press (K::store);
        else if (a == "setIn")     box.press (K::setIn);
        else if (a == "setOut")    box.press (K::setOut);
        else if (a == "goIn")      box.press (K::goIn);
        else if (a == "goOut")     box.press (K::goOut);
        else if (a == "loop")      box.press (K::loop);
        else if (a == "punch")     box.press (K::punch);
        else if (a == "jog")       box.press (K::jog);
        else if (a == "scrub")     box.press (K::scrub);
        else if (a == "disarmAll")
            for (int t = 0; t < TapeMachine::numTracks; ++t)
                tape.setArmState (t, TapeMachine::ArmState::off);
    }
    else if (type == "wind")
    {
        // Rew / F Fwd pressed (dir -1 / 1): wind, 40x while held, as the console's keys. Let go: held false.
        const int dir = (int) m.getProperty ("dir", 0);
        if (dir < 0)      tape.rewind();
        else if (dir > 0) tape.fastForward();
        tape.setWindHeld (dir != 0 && (bool) m.getProperty ("held", false));
    }
    else if (type == "arm")
    {
        const int t = (int) m.getProperty ("track", -1);
        if (juce::isPositiveAndBelow (t, TapeMachine::numTracks))
            tape.stepArmState (t);   // grey -> yellow -> red, as the buttons
    }
    else if (type == "memory")
    {
        box.pressMemory ((int) m.getProperty ("m", -1));
    }
    else if (type == "jog")
    {
        // the page's jog wheel: turns of the wheel, one turn = one second of tape (as the console's)
        const double turns = juce::jlimit (-4.0, 4.0, (double) m.getProperty ("turns", 0.0));
        if (turns != 0.0)
            tape.jog (turns * tape.getSampleRate());
    }
    else if (type == "shuttle")
    {
        // the page's shuttle ring: a step -7..7, held until the page sends 0 (it springs back when let go)
        heldShuttleStep = juce::jlimit (-ShuttleWheel::steps, ShuttleWheel::steps, (int) m.getProperty ("step", 0));
        heldShuttleSince = juce::Time::getMillisecondCounter();
        box.setShuttleStep (heldShuttleStep);
    }
    else if (type == "param")
    {
        setStripParam (m.getProperty ("id", {}).toString(), (float) (double) m.getProperty ("v", 0.0));
    }
    else if (type == "shuttleHeld")
    {
        heldShuttleSince = juce::Time::getMillisecondCounter();   // still holding the ring
    }

    if (server != nullptr && type != "param")
        server->tick();   // the change shows on the phone at once (knob moves go with the next tick, 15 a second)
}

//==============================================================================
// The channel strips on the remote: In = the input bank, Out = the tape returns.

namespace
{
    /** Every parameter of a strip ("ch3.eq.hf.gain" ...), in the order the ParamSet made them. */
    std::vector<Param*> stripParams (const ParamSet& params, const ChannelStrip& strip)
    {
        std::vector<Param*> list;
        for (auto* p : params.all())
            if (p->id.startsWith (strip.idPrefix + "."))
                list.push_back (p);
        return list;
    }

    /** A number for the page: whole numbers bare, others to three places. */
    juce::var number (float v)
    {
        if (v == std::round (v))
            return (int) v;
        return std::round ((double) v * 1000.0) / 1000.0;
    }

    juce::String hex (juce::Colour c) { return "#" + c.toDisplayString (false).toLowerCase(); }

    /** Meter value for the page: dB, whole numbers, -99 = silence. */
    int meterDb (float linear)
    {
        return linear > 1.0e-5f ? juce::jlimit (-99, 12, juce::roundToInt (20.0f * std::log10 (linear))) : -99;
    }
}

juce::String Remote::schemaJson() const
{
    // Ids (after the strip's prefix), ranges and defaults, for each bank (the returns also have the de-esser).
    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "schema");
    for (auto [name, strips] : { std::pair { "ch", &engine.inputStrips }, std::pair { "rt", &engine.returnStrips } })
    {
        const auto& strip = *strips->getFirst();
        juce::Array<juce::var> keys, mins, maxs, scales, defs;
        const int skip = strip.idPrefix.length() + 1;
        for (auto* p : stripParams (engine.params, strip))
        {
            keys.add (p->id.substring (skip));
            mins.add (number (p->min));
            maxs.add (number (p->max));
            scales.add ((int) p->scale);   // 0 linear, 1 log, 2 toggle, 3 stepped
            defs.add (number (p->def));
        }
        auto* bank = new juce::DynamicObject();
        bank->setProperty ("keys", keys);
        bank->setProperty ("min", mins);
        bank->setProperty ("max", maxs);
        bank->setProperty ("scale", scales);
        bank->setProperty ("def", defs);
        root->setProperty (name, juce::var (bank));
    }
    return juce::JSON::toString (juce::var (root), true);
}

juce::String Remote::stripsJson()
{
    // Rebuilt only when a control, a name or a colour changed (it is the biggest message).
    juce::String names;
    for (auto* strips : { &engine.inputStrips, &engine.returnStrips })
        for (auto* s : *strips)
            names << s->name << "\x01" << s->scribbleColour << "\x01";
    for (int pair = 0; pair < Config::numAux / 2; ++pair)
        names << (int) engine.inputStrips.getFirst()->isAuxLinked (pair);
    const auto count = engine.params.changeCount();
    if (stripsText.isNotEmpty() && count == stripsChangeCount && names == stripsNames)
        return stripsText;
    stripsChangeCount = count;
    stripsNames = names;

    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "strips");
    for (auto [name, strips] : { std::pair { "ch", &engine.inputStrips }, std::pair { "rt", &engine.returnStrips } })
    {
        juce::Array<juce::var> list;
        for (auto* s : *strips)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("n", s->name);
            o->setProperty ("c", hex (StripStyle::stripColour (s->scribbleColour)));
            o->setProperty ("t", hex (StripStyle::stripTextColour (s->scribbleColour)));
            juce::Array<juce::var> values;
            for (auto* p : stripParams (engine.params, *s))
                values.add (number (p->get()));
            o->setProperty ("v", values);
            list.add (juce::var (o));
        }
        root->setProperty (name, list);
    }
    juce::Array<juce::var> auxLink;   // linked aux pairs: the odd send is the level, the even one its pan
    for (int pair = 0; pair < Config::numAux / 2; ++pair)
        auxLink.add (engine.inputStrips.getFirst()->isAuxLinked (pair));
    root->setProperty ("auxLink", auxLink);
    stripsText = juce::JSON::toString (juce::var (root), true);
    return stripsText;
}

juce::String Remote::metersJson()
{
    // Each strip's meter as the console shows it (its bank's Meters In / Out switch), compressor and gate (or
    // de-esser) reduction in dB, and the main L / R.
    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "meters");
    for (auto [name, section] : { std::pair { "ch", Desk::inputSection }, std::pair { "rt", Desk::returnSection } })
    {
        auto& bank = desk().getBank (section);
        juce::Array<juce::var> level, comp, gate;
        for (int i = 0; i < Config::numInputStrips; ++i)   // (as many returns)
        {
            auto& strip = bank.getStrip (i);
            const auto r = strip.takeRemoteReadings();
            level.add (meterDb (strip.isMeterInput() ? r.in : r.out));
            comp.add (juce::roundToInt (r.gr));
            gate.add (juce::roundToInt (juce::jmax (r.gate, r.deEss)));
        }
        root->setProperty (name, level);
        root->setProperty (juce::String (name) + "Comp", comp);
        root->setProperty (juce::String (name) + "Gate", gate);
    }
    juce::Array<juce::var> main;
    for (int side = 0; side < 2; ++side)
        main.add (meterDb (desk().takeRemoteMain (side)));
    root->setProperty ("main", main);
    return juce::JSON::toString (juce::var (root), true);
}

void Remote::readBanks (bool on)
{
    if (on == readingBanks)
        return;
    readingBanks = on;
    for (auto section : { Desk::inputSection, Desk::returnSection })
        desk().getBank (section).readers += on ? 1 : -1;
}

void Remote::setStripParam (const juce::String& id, float value)
{
    // Only a channel strip's own parameters: "ch1." .. "ch16.", "rt1." .. "rt16.".
    const auto prefix = id.upToFirstOccurrenceOf (".", false, false);
    for (auto [strips, section] : { std::pair { &engine.inputStrips, Desk::inputSection }, std::pair { &engine.returnStrips, Desk::returnSection } })
        for (int i = 0; i < strips->size(); ++i)
        {
            auto& strip = *(*strips)[i];
            if (strip.idPrefix != prefix)
                continue;
            auto* p = engine.params.find (id);
            if (p == nullptr || ! std::isfinite (value))
                return;
            value = juce::jlimit (p->min, p->max, value);
            auto& c = strip.controls;
            auto& onDesk = desk().getBank (section).getStrip (i);
            if (p == c.gain)
                strip.turnGain (value);   // as the knob: the link turns the trim the other way
            else if (p == c.fader)
                onDesk.remoteFader (value);
            else if (p == c.mute || p == c.solo || p == c.main || std::find (std::begin (c.sub), std::end (c.sub), p) != std::end (c.sub))
                onDesk.remoteSwitch (p, value >= 0.5f);
            else
                p->set (p->scale == Param::Scale::stepped || p->scale == Param::Scale::toggle ? std::round (value) : value);
            return;
        }
}

void Remote::timerCallback()
{
    // A phone holding the shuttle ring sends shuttleHeld a few times a second. If it goes quiet (Wi-Fi gone, the
    // phone locked) the ring is let go, so the tape doesn't wind on by itself.
    if (heldShuttleStep != 0 && (numClients() == 0 || juce::Time::getMillisecondCounter() - heldShuttleSince > 1500))
    {
        heldShuttleStep = 0;
        shuttleBox().setShuttleStep (0);
    }
}

std::vector<Remote::Address> Remote::addresses()
{
    std::vector<Address> found;
#ifdef _WIN32
    ULONG size = 16384;
    juce::HeapBlock<char> buffer;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        buffer.calloc (size);
        const auto r = GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                             nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.get()), &size);
        if (r == ERROR_BUFFER_OVERFLOW)
            continue;
        if (r != NO_ERROR)
            break;
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.get()); a != nullptr; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                char text[INET_ADDRSTRLEN] = {};
                auto* in = reinterpret_cast<sockaddr_in*> (u->Address.lpSockaddr);
                if (inet_ntop (AF_INET, &in->sin_addr, text, sizeof (text)) != nullptr)
                    found.push_back ({ juce::String (text), juce::String (a->FriendlyName) });
            }
        }
        break;
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs (&list) == 0)
    {
        for (auto* a = list; a != nullptr; a = a->ifa_next)
        {
            if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) != 0 || (a->ifa_flags & IFF_UP) == 0)
                continue;
            char text[INET_ADDRSTRLEN] = {};
            if (inet_ntop (AF_INET, &reinterpret_cast<sockaddr_in*> (a->ifa_addr)->sin_addr, text, sizeof (text)) != nullptr)
                found.push_back ({ juce::String (text), juce::String (a->ifa_name) });
        }
        freeifaddrs (list);
    }
#endif
    // self-assigned addresses (no network) are no use to a phone
    found.erase (std::remove_if (found.begin(), found.end(), [] (const Address& a) { return a.ip.startsWith ("169.254.") || a.ip.startsWith ("127."); }),
                 found.end());
    auto rank = [] (const Address& a)
    {
        if (a.ip.startsWith ("192.168.137."))   // Windows Mobile Hotspot
            return 0;
        const auto n = a.adapter.toLowerCase();
        if (n.contains ("wi-fi") || n.contains ("wifi") || n.contains ("wireless") || n.contains ("wlan") || n == "en0")
            return 1;
        if (n.contains ("local area connection*") || n.contains ("bridge"))
            return 2;
        if (n.contains ("dante") || n.contains ("soundgrid") || n.contains ("vethernet") || n.contains ("virtual") || n.contains ("vpn"))
            return 4;
        return 3;
    };
    std::stable_sort (found.begin(), found.end(), [&] (const Address& a, const Address& b) { return rank (a) < rank (b); });
    return found;
}

juce::String Remote::getAddress() const
{
    const auto all = addresses();
    const auto picked = settings.getValue ("remoteAddress");
    for (auto& a : all)
        if (a.ip == picked)
            return picked;
    return all.empty() ? juce::String ("127.0.0.1") : all.front().ip;
}

void Remote::setAddress (const juce::String& ip)
{
    settings.setValue ("remoteAddress", ip);
    if (saveSettings) saveSettings();
    sendChangeMessage();
}

juce::String Remote::url() const
{
    return "http://" + getAddress() + ":" + juce::String (port) + "/?k=" + settings.getValue ("remoteKey");
}

// ---------------------------------------------------------------------------------------------

juce::Component::SafePointer<juce::DocumentWindow> RemotePanel::window;

namespace
{
    /** A normal window (the console stays usable while it is open); closing it deletes it. */
    class RemoteWindow : public juce::DocumentWindow
    {
    public:
        explicit RemoteWindow (Remote& remote)
            : DocumentWindow ("Wi-Fi Remote", room, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new RemotePanel (remote), true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
        }
        void closeButtonPressed() override { delete this; }
    };
}

void RemotePanel::show (Remote& remote)
{
    if (window == nullptr)
        window = new RemoteWindow (remote);
    window->setVisible (true);
    window->toFront (true);
}

RemotePanel::RemotePanel (Remote& r) : remote (r)
{
    on.setColour (juce::ToggleButton::textColourId, bridgeText);
    on.setColour (juce::ToggleButton::tickColourId, amber);
    on.onClick = [this] { remote.setOn (on.getToggleState()); };
    addAndMakeVisible (on);

    networkLabel.setColour (juce::Label::textColourId, bridgeText.withAlpha (0.7f));
    addAndMakeVisible (networkLabel);
    network.setTooltip ("The network the phones are on (the address in the QR code)");
    network.onChange = [this]
    {
        const int i = network.getSelectedItemIndex();
        if (i >= 0 && i < (int) shownAddresses.size())
            remote.setAddress (shownAddresses[(size_t) i].ip);
    };
    addAndMakeVisible (network);
    rescan.setTooltip ("Look for networks again (after joining a Wi-Fi or turning the hotspot on)");
    rescan.onClick = [this] { fillAddresses(); refresh(); };
    addAndMakeVisible (rescan);

    link.setReadOnly (true);
    link.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (link);
    copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (remote.url()); };
    addAndMakeVisible (copy);
    renew.setTooltip ("Makes a new link and QR code: phones with the old one are disconnected and must scan again");
    renew.onClick = [this]
    {
        juce::Component::SafePointer<RemotePanel> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New link",
                                            "Phones and tablets with the old link are disconnected and must scan the new QR code.",
                                            "New link", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->remote.newKey();
        }));
    };
    addAndMakeVisible (renew);

    status.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
    addAndMakeVisible (status);

    help.setColour (juce::Label::textColourId, bridgeText.withAlpha (0.7f));
    help.setJustificationType (juce::Justification::topLeft);
    help.setText (juce::String::fromUTF8 (
        "1. Turn the remote on and scan the QR code with the phone's or iPad's camera (or type the link into its browser).\n"
        "2. It must be on the same network as this computer. No internet is needed.\n"
        "3. The page has the counter, the transport, record arm for tracks 1-16, the jog / shuttle wheel, "
        "the In / Out keys and the 99 locate memories (Store, then a number). Older iPads (iOS 12) work too.\n\n"
        "Venue or studio Wi-Fi often blocks phones from reaching other devices. Then turn on the computer's Mobile Hotspot, "
        "join it from the phone, pick the hotspot network above and scan again.\n"
        "The first time, Windows asks whether Oxide-3216 may use the network: allow it (Private, and Public if the Wi-Fi is Public).\n"
        "Set the iPad's Auto-Lock longer for a session; the page reconnects by itself after it sleeps."),
        juce::dontSendNotification);
    addAndMakeVisible (help);

    hotspot.setTooltip ("Windows settings for the Mobile Hotspot (your own Wi-Fi from this computer)");
    hotspot.onClick = [] { juce::URL ("ms-settings:network-mobilehotspot").launchInDefaultBrowser(); };
#ifdef _WIN32
    addAndMakeVisible (hotspot);
#endif

    remote.addChangeListener (this);
    fillAddresses();
    refresh();
    startTimerHz (2);
    setSize (640, 560);
}

RemotePanel::~RemotePanel()
{
    remote.removeChangeListener (this);
}

void RemotePanel::fillAddresses()
{
    shownAddresses = Remote::addresses();
    network.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) shownAddresses.size(); ++i)
        network.addItem (shownAddresses[(size_t) i].adapter + juce::String::fromUTF8 ("  \xe2\x80\x94  ") + shownAddresses[(size_t) i].ip, i + 1);
    if (shownAddresses.empty())
        network.setTextWhenNothingSelected ("No network: join a Wi-Fi or turn on the hotspot");
}

void RemotePanel::refresh()
{
    on.setToggleState (remote.isOn(), juce::dontSendNotification);
    const auto address = remote.getAddress();
    for (int i = 0; i < (int) shownAddresses.size(); ++i)
        if (shownAddresses[(size_t) i].ip == address)
            network.setSelectedItemIndex (i, juce::dontSendNotification);

    const auto url = remote.url();
    link.setText (url, false);
    link.setCaretPosition (0);
    qr = {};
    try
    {
        const auto code = qrcodegen::QrCode::encodeText (url.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = code.getSize(), border = 3, scale = 8;
        qr = juce::Image (juce::Image::RGB, (n + border * 2) * scale, (n + border * 2) * scale, true);
        juce::Graphics g (qr);
        g.fillAll (juce::Colours::white);
        g.setColour (juce::Colours::black);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (code.getModule (x, y))
                    g.fillRect ((x + border) * scale, (y + border) * scale, scale, scale);
    }
    catch (...) {}
    timerCallback();
    repaint();
}

void RemotePanel::timerCallback()
{
    juce::String text;
    juce::Colour colour = bridgeText.withAlpha (0.7f);
    if (! remote.isOn())
        text = "Remote is off";
    else if (remote.failed())
    {
        text = "Can't start: port " + juce::String (Remote::port) + " is in use (another Oxide-3216?)";
        colour = meterRed.brighter (0.4f);
    }
    else if (const int n = remote.numClients(); n > 0)
    {
        text = juce::String (n) + (n == 1 ? " phone / tablet connected" : " phones / tablets connected");
        colour = statusGreen;
    }
    else
    {
        text = "Waiting for a phone: scan the QR code";
        colour = amber;
    }
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, colour);
}

void RemotePanel::paint (juce::Graphics& g)
{
    g.fillAll (room);
    if (qr.isValid())
    {
        g.setOpacity (remote.isOn() ? 1.0f : 0.15f);
        g.drawImage (qr, qrArea.toFloat(), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }
    if (! remote.isOn())
    {
        g.setColour (bridgeText);
        g.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
        g.drawFittedText ("Turn the remote on\nto use the QR code", qrArea, juce::Justification::centred, 2);
    }
}

void RemotePanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    on.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    networkLabel.setBounds (row.removeFromLeft (70));
    rescan.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    network.setBounds (row);
    r.removeFromTop (12);

    auto top = r.removeFromTop (240);
    qrArea = top.removeFromLeft (240);
    top.removeFromLeft (16);
    status.setBounds (top.removeFromTop (28));
    top.removeFromTop (8);
    link.setBounds (top.removeFromTop (28));
    top.removeFromTop (8);
    auto buttons = top.removeFromTop (28);
    copy.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    renew.setBounds (buttons.removeFromLeft (110));
    top.removeFromTop (8);
    hotspot.setBounds (top.removeFromTop (28).removeFromLeft (228));

    r.removeFromTop (12);
    help.setBounds (r);
}
