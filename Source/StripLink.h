#pragma once

#include "ChannelStrip.h"
#include <map>
#include <set>
#include <vector>

/** Links two neighbouring channel strips (1+2, 3+4, ...): while the link is on, moving any control on
    one strip moves the same control on the other. Pan is the exception: it mirrors (one turns left as
    the other turns right), so the pair keeps a balanced stereo image.

    Switching the link on copies the odd strip's settings onto the even one (its pan mirrored).
    The same thing gangs any other list of control pairs (the aux masters of a linked aux pair).
    Message thread only, like Param::set. */
class StripLink : private Param::Listener
{
public:
    StripLink (ParamSet&, ChannelStrip& first, ChannelStrip& second, Param& link);

    /** Gangs each pair (first -> second when the link goes on); controls in `mirrored` move the opposite way. */
    StripLink (Param& link, const std::vector<std::pair<Param*, Param*>>& pairs, std::set<const Param*> mirrored = {});
    ~StripLink() override;

    Param& link;

private:
    std::map<Param*, Param*> partner;   // every control of either side -> the same control on the other
    std::set<const Param*> firsts, mirrored;
    bool mirroring = false;

    void addPair (Param* a, Param* b);
    void listen();
    void paramChanged (Param&) override;
    void copy (Param& from, Param& to);

    JUCE_DECLARE_NON_COPYABLE (StripLink)
};
