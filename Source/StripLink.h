#pragma once

#include "ChannelStrip.h"
#include <map>

/** Links two neighbouring channel strips (1+2, 3+4, ...): while the link is on, moving any control on
    one strip moves the same control on the other. Pan is the exception: it mirrors (one turns left as
    the other turns right), so the pair keeps a balanced stereo image.

    Switching the link on copies the odd strip's settings onto the even one (its pan mirrored).
    Message thread only, like Param::set. */
class StripLink : private Param::Listener
{
public:
    StripLink (ParamSet&, ChannelStrip& first, ChannelStrip& second, Param& link);
    ~StripLink() override;

    Param& link;

private:
    ChannelStrip &first, &second;
    std::map<Param*, Param*> partner;   // every control of either strip -> the same control on the other
    bool mirroring = false;

    void paramChanged (Param&) override;
    void copy (Param& from, Param& to);
    bool isPan (const Param& p) const noexcept { return &p == first.controls.pan || &p == second.controls.pan; }

    JUCE_DECLARE_NON_COPYABLE (StripLink)
};
