#include "MidiControl.h"

//==============================================================================
bool MidiBinding::matches (const juce::MidiMessage& m) const
{
    if (channel > 0 && m.getChannel() != channel)
        return false;
    switch (kind)
    {
        case note: return m.isNoteOn() && m.getNoteNumber() == number;
        case cc:   return m.isController() && m.getControllerNumber() == number;
        case none: break;
    }
    return false;
}

bool MidiBinding::isPress (const juce::MidiMessage& m)
{
    return m.isNoteOn() || (m.isController() && m.getControllerValue() > 0);
}

MidiBinding MidiBinding::from (const juce::MidiMessage& m, bool keepChannel)
{
    MidiBinding b;
    if (m.isNoteOn())
        b = { note, 0, m.getNoteNumber() };
    else if (m.isController())
        b = { cc, 0, m.getControllerNumber() };
    if (b.isSet() && keepChannel)
        b.channel = m.getChannel();
    return b;
}

juce::String MidiBinding::text() const
{
    if (kind == none)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
    auto s = kind == note ? "Note " + juce::MidiMessage::getMidiNoteName (number, true, true, 3) : "CC " + juce::String (number);
    if (channel > 0)
        s << " (ch " << channel << ")";
    return s;
}

juce::String MidiBinding::toString() const
{
    return juce::String ((int) kind) + " " + juce::String (channel) + " " + juce::String (number);
}

MidiBinding MidiBinding::fromString (const juce::String& s)
{
    const auto f = juce::StringArray::fromTokens (s, " ", "");
    MidiBinding b;
    if (f.size() == 3)
    {
        const int k = f[0].getIntValue();
        b.kind = k == note ? note : k == cc ? cc : none;
        b.channel = juce::jlimit (0, 16, f[1].getIntValue());
        b.number = juce::jlimit (0, 127, f[2].getIntValue());
    }
    return b;
}

//==============================================================================
MidiBinding MidiShortcuts::get (int command) const
{
    const auto it = bindings.find (command);
    return it != bindings.end() ? it->second : MidiBinding();
}

void MidiShortcuts::set (int command, MidiBinding b)
{
    if (b.isSet())
        for (auto it = bindings.begin(); it != bindings.end();)
            it = it->first != command && it->second == b ? bindings.erase (it) : std::next (it);   // one press, one command
    if (b.isSet())
        bindings[command] = b;
    else
        bindings.erase (command);
    changed();
}

void MidiShortcuts::learn (int command)
{
    learning = command;
    ++revision;
}

bool MidiShortcuts::handle (const juce::MidiMessage& m)
{
    if (! MidiBinding::isPress (m))
        return false;
    if (learning >= 0)
    {
        const int command = learning;
        learning = -1;
        set (command, MidiBinding::from (m, true));
        return true;
    }
    bool did = false;
    for (const auto& [command, b] : bindings)
        if (b.matches (m) && invoke)
        {
            invoke (command);
            did = true;
        }
    return did;
}

juce::String MidiShortcuts::toString() const
{
    juce::StringArray items;
    for (const auto& [command, b] : bindings)
        items.add (juce::String (command) + ":" + b.toString());
    return items.joinIntoString (";");
}

void MidiShortcuts::fromString (const juce::String& s)
{
    bindings.clear();
    for (auto& item : juce::StringArray::fromTokens (s, ";", ""))
    {
        const auto b = MidiBinding::fromString (item.fromFirstOccurrenceOf (":", false, false));
        if (b.isSet())
            bindings[item.upToFirstOccurrenceOf (":", false, false).getIntValue()] = b;
    }
    ++revision;
}

void MidiShortcuts::changed()
{
    ++revision;
    if (onChange)
        onChange();
}

//==============================================================================
juce::String ShuttleMidi::name (Control c)
{
    static const char* const names[numControls] = { "Jog wheel", "Shuttle ring", "Jog", "Scrub", "Set In", "Set Out", "Loop",
                                                    "Go In", "Go Out", "Punch", "Store", "RTZ", "Memories up", "Memories down",
                                                    "Memory key 1", "Memory key 2", "Memory key 3", "Memory key 4", "Memory key 5", "Memory key 6" };
    return juce::isPositiveAndBelow ((int) c, (int) numControls) ? names[c] : "";
}

void ShuttleMidi::set (Control c, MidiBinding b)
{
    if (! juce::isPositiveAndBelow ((int) c, (int) numControls))
        return;
    if (isContinuous (c) && b.kind == MidiBinding::note)
        return;   // the wheel and the ring need a controller
    b.channel = 0;   // the window's channel decides
    if (b.isSet())
        for (int o = 0; o < numControls; ++o)
            if (o != c && bindings[(size_t) o] == b)
                bindings[(size_t) o] = {};
    bindings[(size_t) c] = b;
    lastAbsolute = -1;
    changed();
}

void ShuttleMidi::setInput (const juce::String& id) { input = id; changed(); }
void ShuttleMidi::setChannel (int c)                { channel = juce::jlimit (0, 16, c); changed(); }
void ShuttleMidi::setEncoder (Encoder e)            { encoder = e; lastAbsolute = -1; changed(); }

void ShuttleMidi::learn (int c)
{
    learning = juce::isPositiveAndBelow (c, (int) numControls) ? c : -1;
    ++revision;
}

bool ShuttleMidi::handle (const juce::MidiMessage& m, const juce::String& device)
{
    if (input.isEmpty() || (input != allInputs && input != device))
        return false;
    if (channel > 0 && m.getChannel() != channel)
        return false;

    if (learning >= 0)
    {
        const auto c = (Control) learning;
        const bool usable = isContinuous (c) ? m.isController() : MidiBinding::isPress (m);
        if (! usable)
            return false;
        learning = -1;
        set (c, MidiBinding::from (m, false));
        return true;
    }

    for (int i = 0; i < numControls; ++i)
    {
        const auto c = (Control) i;
        const auto& b = bindings[(size_t) i];
        if (! b.matches (m))
            continue;
        if (c == jogWheel)
        {
            const int v = m.getControllerValue();
            int notches = 0;
            switch (encoder)
            {
                case Encoder::twosComplement: notches = v < 64 ? v : v - 128; break;
                case Encoder::offset64:       notches = v - 64; break;
                case Encoder::absolute:       notches = lastAbsolute < 0 ? 0 : v - lastAbsolute; lastAbsolute = v; break;
            }
            if (notches != 0 && act)
                act (c, notches);
            return true;
        }
        if (c == shuttleRing)
        {
            if (act)
                act (c, juce::roundToInt ((float) (m.getControllerValue() - 64) / 63.0f * 7.0f));
            return true;
        }
        if (MidiBinding::isPress (m) && act)
        {
            act (c, 1);
            return true;
        }
    }
    return false;
}

juce::String ShuttleMidi::toString() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("input", input);
    o->setProperty ("channel", channel);
    o->setProperty ("encoder", (int) encoder);
    juce::Array<juce::var> list;
    for (auto& b : bindings)
        list.add (b.toString());
    o->setProperty ("bindings", list);
    return juce::JSON::toString (juce::var (o), true);
}

void ShuttleMidi::fromString (const juce::String& s)
{
    const auto v = juce::JSON::parse (s);
    input = v["input"].toString();
    channel = juce::jlimit (0, 16, (int) v["channel"]);
    encoder = (Encoder) juce::jlimit (0, 2, (int) v["encoder"]);
    bindings = {};
    if (auto* list = v["bindings"].getArray())
        for (int i = 0; i < juce::jmin (list->size(), (int) numControls); ++i)
            bindings[(size_t) i] = MidiBinding::fromString ((*list)[i].toString());
    lastAbsolute = -1;
    ++revision;
}

void ShuttleMidi::changed()
{
    ++revision;
    if (onChange)
        onChange();
}
