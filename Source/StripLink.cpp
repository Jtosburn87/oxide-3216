#include "StripLink.h"

StripLink::StripLink (ParamSet& params, ChannelStrip& a, ChannelStrip& b, bool copyNow)
    : link (nullptr)
{
    const auto always = [] { return true; };
    mirrored[a.controls.pan] = always;
    mirrored[b.controls.pan] = always;
    for (int j = 0; j < Config::numAux / 2; ++j)
    {
        const auto auxLinked = [&a, j] { return a.isAuxLinked (j); };
        mirrored[a.controls.auxLevel[2 * j + 1]] = auxLinked;
        mirrored[b.controls.auxLevel[2 * j + 1]] = auxLinked;
    }

    // A strip's controls all have ids "<prefix>.<control>", so "ch1.eq.hf.gain" pairs with "ch2.eq.hf.gain".
    const auto prefixA = a.idPrefix + ".";
    for (auto* p : params.all())
        if (p->id.startsWith (prefixA))
            if (auto* q = params.find (b.idPrefix + "." + p->id.substring (prefixA.length())))
                addPair (p, q);
    listen();
    if (copyNow)
        for (auto& [p, q] : partner)
            if (firsts.count (p) != 0)
                copy (*p, *q);
}

StripLink::StripLink (Param& l, const std::vector<std::pair<Param*, Param*>>& pairs)
    : link (&l)
{
    for (auto& [a, b] : pairs)
        addPair (a, b);
    listen();
}

void StripLink::addPair (Param* a, Param* b)
{
    partner[a] = b;
    partner[b] = a;
    firsts.insert (a);
}

void StripLink::listen()
{
    for (auto& [p, q] : partner)
        p->addListener (this);
    if (link != nullptr)
        link->addListener (this);
}

StripLink::~StripLink()
{
    if (link != nullptr)
        link->removeListener (this);
    for (auto& [p, q] : partner)
        p->removeListener (this);
}

void StripLink::copy (Param& from, Param& to)
{
    mirroring = true;
    // Mirroring reflects about the middle of the range: pan -1..1 about 0, an aux send pan 0..1 about 0.5.
    const auto m = mirrored.find (&from);
    const bool mirror = m != mirrored.end() && m->second();
    to.set (mirror ? from.min + from.max - from.get() : from.get());
    mirroring = false;
}

void StripLink::paramChanged (Param& p)
{
    if (mirroring)
        return;

    if (&p == link)
    {
        if (link->on())
            for (auto& [a, b] : partner)
                if (firsts.count (a) != 0)
                    copy (*a, *b);
        return;
    }

    if (link != nullptr && ! link->on())
        return;
    if (auto it = partner.find (&p); it != partner.end())
        copy (p, *it->second);
}

//==============================================================================
BankLinks::BankLinks (ParamSet& p, const juce::OwnedArray<ChannelStrip>& s)
    : params (p), strips (s)
{
}

int BankLinks::partner (int strip) const noexcept
{
    return juce::isPositiveAndBelow (strip, (int) partners.size()) ? partners[(size_t) strip] : -1;
}

void BankLinks::link (int a, int b, bool copySettings)
{
    if (a == b || ! juce::isPositiveAndBelow (a, strips.size()) || ! juce::isPositiveAndBelow (b, strips.size()) || partner (a) == b)
        return;
    unlink (a);
    unlink (b);
    partners.resize ((size_t) strips.size(), -1);
    const int lo = juce::jmin (a, b), hi = juce::jmax (a, b);
    partners[(size_t) lo] = hi;
    partners[(size_t) hi] = lo;
    links[lo] = std::make_unique<StripLink> (params, *strips[lo], *strips[hi], copySettings);
    ++revision;
}

void BankLinks::unlink (int strip)
{
    const int other = partner (strip);
    if (other < 0)
        return;
    links.erase (juce::jmin (strip, other));
    partners[(size_t) strip] = -1;
    partners[(size_t) other] = -1;
    ++revision;
}

void BankLinks::unlinkAll()
{
    for (int k = 0; k < (int) partners.size(); ++k)
        unlink (k);
}

juce::var BankLinks::toVar() const
{
    juce::Array<juce::var> list;
    for (auto& [lo, l] : links)
        list.add (juce::Array<juce::var> { lo + 1, partners[(size_t) lo] + 1 });
    return list;
}

void BankLinks::fromVar (const juce::var& v)
{
    if (auto* list = v.getArray())
        for (auto& pair : *list)
            if (pair.isArray() && pair.size() == 2)
                link ((int) pair[0] - 1, (int) pair[1] - 1, false);
}
