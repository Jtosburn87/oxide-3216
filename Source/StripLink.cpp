#include "StripLink.h"

StripLink::StripLink (ParamSet& params, ChannelStrip& a, ChannelStrip& b, Param& l)
    : link (l), mirrored { a.controls.pan, b.controls.pan }
{
    // A strip's controls all have ids "<prefix>.<control>", so "ch1.eq.hf.gain" pairs with "ch2.eq.hf.gain".
    const auto prefixA = a.idPrefix + ".";
    for (auto* p : params.all())
        if (p->id.startsWith (prefixA))
            if (auto* q = params.find (b.idPrefix + "." + p->id.substring (prefixA.length())))
                addPair (p, q);
    listen();
}

StripLink::StripLink (Param& l, const std::vector<std::pair<Param*, Param*>>& pairs, std::set<const Param*> m)
    : link (l), mirrored (std::move (m))
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
    link.addListener (this);
}

StripLink::~StripLink()
{
    link.removeListener (this);
    for (auto& [p, q] : partner)
        p->removeListener (this);
}

void StripLink::copy (Param& from, Param& to)
{
    mirroring = true;
    to.set (mirrored.count (&from) != 0 ? -from.get() : from.get());
    mirroring = false;
}

void StripLink::paramChanged (Param& p)
{
    if (mirroring)
        return;

    if (&p == &link)
    {
        if (link.on())
            for (auto& [a, b] : partner)
                if (firsts.count (a) != 0)
                    copy (*a, *b);
        return;
    }

    if (! link.on())
        return;
    if (auto it = partner.find (&p); it != partner.end())
        copy (p, *it->second);
}
