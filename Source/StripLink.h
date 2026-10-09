#pragma once

#include "ChannelStrip.h"
#include <functional>
#include <map>
#include <set>
#include <vector>

/** Links two neighbouring channel strips (1+2, 3+4, ...): while the link is on, moving any control on
    one strip moves the same control on the other. Pans are the exception: they mirror (one turns left as
    the other turns right), so the pair keeps a balanced stereo image. That is the main pan, and the pan of
    each linked aux pair's stereo send (the even send knob, while that aux pair is linked).

    Switching the link on copies the odd strip's settings onto the even one (its pans mirrored).
    The same thing gangs any other list of control pairs (the aux masters of a linked aux pair).
    Message thread only, like Param::set. */
class StripLink : private Param::Listener
{
public:
    StripLink (ParamSet&, ChannelStrip& first, ChannelStrip& second, Param& link);

    /** Gangs each pair (first -> second when the link goes on). */
    StripLink (Param& link, const std::vector<std::pair<Param*, Param*>>& pairs);
    ~StripLink() override;

    Param& link;

private:
    std::map<Param*, Param*> partner;   // every control of either side -> the same control on the other
    std::set<const Param*> firsts;
    std::map<const Param*, std::function<bool()>> mirrored;   // controls that mirror, while their condition holds
    bool mirroring = false;

    void addPair (Param* a, Param* b);
    void listen();
    void paramChanged (Param&) override;
    void copy (Param& from, Param& to);

    JUCE_DECLARE_NON_COPYABLE (StripLink)
};
