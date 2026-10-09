#include "StripLink.h"

StripLink::StripLink (ParamSet& params, ChannelStrip& a, ChannelStrip& b, Param& l)
    : link (l), first (a), second (b)
{
    // A strip's controls all have ids "<prefix>.<control>", so "ch1.eq.hf.gain" pairs with "ch2.eq.hf.gain".
    const auto prefixA = a.idPrefix + ".";
    for (auto* p : params.all())
    {
        if (! p->id.startsWith (prefixA))
            continue;
        if (auto* q = params.find (b.idPrefix + "." + p->id.substring (prefixA.length())))
        {
            partner[p] = q;
            partner[q] = p;
        }
    }

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
    to.set (isPan (from) ? -from.get() : from.get());
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
                if (a->id.startsWith (first.idPrefix + "."))
                    copy (*a, *b);
        return;
    }

    if (! link.on())
        return;
    if (auto it = partner.find (&p); it != partner.end())
        copy (p, *it->second);
}
