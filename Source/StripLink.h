#pragma once

#include "ChannelStrip.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

/** Links two channel strips of one bank (any two: see BankLinks): while the link is on, moving any control on
    one strip moves the same control on the other. Pans are the exception: they mirror (one turns left as
    the other turns right), so the pair keeps a balanced stereo image. That is the main pan, and the pan of
    each linked aux pair's stereo send (the even send knob, while that aux pair is linked).

    Switching the link on copies the first strip's settings onto the second (its pans mirrored).
    The same thing gangs any other list of control pairs (the aux masters of a linked aux pair).
    Message thread only, like Param::set. */
class StripLink : private Param::Listener
{
public:
    /** Links two strips for as long as it exists (no link switch): copies first -> second now if asked. */
    StripLink (ParamSet&, ChannelStrip& first, ChannelStrip& second, bool copyNow);

    /** Gangs each pair (first -> second when the link goes on). */
    StripLink (Param& link, const std::vector<std::pair<Param*, Param*>>& pairs);
    ~StripLink() override;

    Param* const link;   // nullptr = always on

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

/** The stereo links of one bank of strips (inputs, or returns): any two strips can be linked, each to one
    partner. Linking copies the lower-numbered strip's settings onto the higher one, and the lower one is the
    left side for the mirrored pans. Message thread only. */
class BankLinks
{
public:
    BankLinks (ParamSet&, const juce::OwnedArray<ChannelStrip>&);

    /** The strip linked with this one, or -1. */
    int partner (int strip) const noexcept;

    /** Links two strips (each leaves the partner it had). copySettings false: they already match (a loaded session). */
    void link (int a, int b, bool copySettings = true);
    void unlink (int strip);
    void unlinkAll();

    /** Changes whenever a link is made or broken (for the UI and the session fingerprint). */
    int getRevision() const noexcept { return revision; }

    /** [[a, b], ...] with 1-based strip numbers, lower first. */
    juce::var toVar() const;
    void fromVar (const juce::var&);

private:
    ParamSet& params;
    const juce::OwnedArray<ChannelStrip>& strips;
    std::vector<int> partners;
    std::map<int, std::unique_ptr<StripLink>> links;   // by the lower strip
    int revision = 0;

    JUCE_DECLARE_NON_COPYABLE (BankLinks)
};
