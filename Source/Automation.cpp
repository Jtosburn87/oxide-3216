#include "Automation.h"
#include <algorithm>

namespace
{
    constexpr float thinTolerance = 0.002f;   // of the control's travel

    /** Drops points that lie on the line between their neighbours (a recorded move is mostly straight runs). */
    Automation::Curve thin (const Automation::Curve& in)
    {
        if (in.size() < 3)
            return in;
        Automation::Curve out;
        out.push_back (in.front());
        for (size_t i = 1; i + 1 < in.size(); ++i)
        {
            const auto& a = out.back();
            const auto& b = in[i];
            const auto& c = in[i + 1];
            const double span = (double) (c.pos - a.pos);
            const double line = a.value + (c.value - a.value) * (span > 0.0 ? (double) (b.pos - a.pos) / span : 0.0);
            if (std::abs (line - (double) b.value) > thinTolerance)
                out.push_back (b);
        }
        out.push_back (in.back());
        return out;
    }

    const char* targetName (int t) { return t == Automation::fader ? "fader" : t == Automation::pan ? "pan" : "mute"; }
    const char* modeName (Automation::Mode m)
    {
        return m == Automation::Mode::off ? "off" : m == Automation::Mode::touch ? "touch" : m == Automation::Mode::latch ? "latch" : "read";
    }
}

Automation::Automation (TapeMachine& t) : tape (t)
{
    for (auto& m : modes)
        m.store (Mode::read);
    for (auto& s : writing)
        for (auto& w : s)
            w.store (false);
}

Automation::~Automation()
{
    stopTimer();
    for (auto& row : params)
        for (auto* p : row)
            if (p != nullptr)
                p->removeListener (this);
}

void Automation::bind (const juce::OwnedArray<ChannelStrip>& inputs, const juce::OwnedArray<ChannelStrip>& returns)
{
    const auto add = [this] (ChannelStrip& s, int index)
    {
        auto& row = params[(size_t) index];
        row[fader] = s.controls.fader;
        row[pan] = s.controls.pan;
        row[mute] = s.controls.mute;
        for (int t = 0; t < numTargets; ++t)
        {
            byParam[row[(size_t) t]] = { index, t };
            row[(size_t) t]->addListener (this);
            shadow[(size_t) index][(size_t) t] = row[(size_t) t]->toNormalised (row[(size_t) t]->get());
        }
    };
    for (int i = 0; i < inputs.size(); ++i)  add (*inputs[i], stripIndex (false, i));
    for (int i = 0; i < returns.size(); ++i) add (*returns[i], stripIndex (true, i));
    startTimerHz (30);
}

float Automation::evaluate (const Curve& c, Target target, juce::int64 pos) noexcept
{
    if (c.empty())
        return 0.0f;
    if (pos < c.front().pos)
        return c.front().value;
    if (pos >= c.back().pos)
        return c.back().value;
    const auto after = std::upper_bound (c.begin(), c.end(), pos, [] (juce::int64 p, const Point& a) { return p < a.pos; });
    const auto& b = *after;
    const auto& a = *(after - 1);
    if (target == mute)
        return a.value;
    return a.value + (b.value - a.value) * (float) ((double) (pos - a.pos) / (double) (b.pos - a.pos));
}

float Automation::valueAt (int strip, Target t, juce::int64 pos, float fallback) const
{
    const auto& c = getCurve (strip, t);
    return c.empty() ? fallback : evaluate (c, t, pos);
}

void Automation::swapIn (int strip, Target t)
{
    Curve copy = curves[(size_t) strip][(size_t) t];
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (live[(size_t) strip][(size_t) t], copy);
    }
    updateAnyCurves();
    ++revision;
}

void Automation::updateAnyCurves()
{
    bool any = false;
    for (auto& s : curves)
        for (auto& c : s)
            any = any || ! c.empty();
    anyCurves.store (any);
}

void Automation::setCurve (int strip, Target t, Curve c)
{
    std::stable_sort (c.begin(), c.end(), [] (const Point& a, const Point& b) { return a.pos < b.pos; });
    Curve clean;
    for (auto p : c)
    {
        p.pos = juce::jmax ((juce::int64) 0, p.pos);
        p.value = juce::jlimit (0.0f, 1.0f, p.value);
        if (t == mute)
            p.value = p.value >= 0.5f ? 1.0f : 0.0f;
        // Two points may share a position (a step); more than two: the last one wins.
        if (clean.size() >= 2 && clean.back().pos == p.pos && clean[clean.size() - 2].pos == p.pos)
            clean.back() = p;
        else
            clean.push_back (p);
    }
    curves[(size_t) strip][(size_t) t] = std::move (clean);
    swapIn (strip, t);
}

void Automation::setMuteRange (int strip, juce::int64 from, juce::int64 to, bool muted)
{
    if (to <= from)
        return;
    const auto& old = getCurve (strip, mute);
    const float now = params[(size_t) strip][mute] != nullptr && params[(size_t) strip][mute]->on() ? 1.0f : 0.0f;
    const float after = valueAt (strip, mute, to, now);
    const float before = valueAt (strip, mute, from - 1, now);
    Curve c;
    for (const auto& p : old)
        if (p.pos < from)
            c.push_back (p);
    if (c.empty() && from > 0)
        c.push_back ({ 0, before });
    c.push_back ({ from, muted ? 1.0f : 0.0f });
    c.push_back ({ to, after });
    for (const auto& p : old)
        if (p.pos > to)
            c.push_back (p);
    setCurve (strip, mute, std::move (c));
}

void Automation::setMode (int strip, Mode m)
{
    if (modes[(size_t) strip].exchange (m) == m)
        return;
    if (m == Mode::off || m == Mode::read)
        for (int t = 0; t < numTargets; ++t)
            if (writes[(size_t) strip][(size_t) t].active)
                endWrite (strip, (Target) t, tape.getPosition(), false);
    ++revision;
}

bool Automation::hasCurves (int strip) const
{
    for (auto& c : curves[(size_t) strip])
        if (! c.empty())
            return true;
    return false;
}

void Automation::clearStrip (int strip)
{
    for (int t = 0; t < numTargets; ++t)
        clearCurve (strip, (Target) t);
}

void Automation::clearAll()
{
    for (int s = 0; s < numStrips; ++s)
    {
        for (int t = 0; t < numTargets; ++t)
        {
            writes[(size_t) s][(size_t) t] = {};
            writing[(size_t) s][(size_t) t].store (false);
        }
        clearStrip (s);
        modes[(size_t) s].store (Mode::read);
    }
    ++revision;
}

void Automation::process() noexcept
{
    if (! anyCurves.load (std::memory_order_relaxed))
        return;
    const auto locate = tape.getPendingLocate();
    const auto pos = locate >= 0 ? locate : tape.getPosition();
    if (! tape.isPlaying() && pos == lastPos)
        return;   // stopped: the controls are free

    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked())
    {
        lastPos = -1;   // next block
        return;
    }
    lastPos = pos;
    for (size_t s = 0; s < numStrips; ++s)
    {
        if (modes[s].load (std::memory_order_relaxed) == Mode::off)
            continue;
        for (size_t t = 0; t < numTargets; ++t)
        {
            const auto& c = live[s][t];
            auto* p = params[s][t];
            if (c.empty() || p == nullptr || writing[s][t].load (std::memory_order_relaxed))
                continue;
            p->setFromAudio (p->fromNormalised (evaluate (c, (Target) t, pos)));
        }
    }
}

void Automation::paramChanged (Param& p)
{
    if (flushing)
        return;   // the curve moved it, or a stereo link followed that move
    const auto it = byParam.find (&p);
    if (it == byParam.end())
        return;
    const auto [strip, target] = it->second;
    const auto m = getMode (strip);
    const float value = p.toNormalised (p.get());
    if ((m == Mode::touch || m == Mode::latch) && tape.isPlaying() && ! tape.exporting.load())
        beginOrContinue (strip, (Target) target, value);
    else
        shadow[(size_t) strip][(size_t) target] = value;
}

void Automation::beginOrContinue (int strip, Target t, float value)
{
    auto& w = writes[(size_t) strip][(size_t) t];
    const auto pos = tape.getPosition();
    if (w.active && pos < w.last)   // the tape looped back: what has been written is finished
        endWrite (strip, t, w.last, false);
    if (! w.active)
    {
        w.active = true;
        writing[(size_t) strip][(size_t) t].store (true);
        w.start = pos;
        w.points.clear();
        w.before = valueAt (strip, t, pos, shadow[(size_t) strip][(size_t) t]);
    }
    w.last = pos;
    w.lastMove = juce::Time::getMillisecondCounterHiRes();
    if (! w.points.empty() && w.points.back().pos == pos)
        w.points.back().value = value;
    else
        w.points.push_back ({ pos, value });
    ++revision;
}

void Automation::endWrite (int strip, Target t, juce::int64 endPos, bool glideBack)
{
    auto& w = writes[(size_t) strip][(size_t) t];
    if (! w.active)
        return;
    const Curve old = getCurve (strip, t);
    const bool blank = old.empty();
    const float heldValue = w.points.empty() ? w.before : w.points.back().value;
    endPos = juce::jmax (endPos, w.start + 1);

    // Back to the curve after the write: a glide in Touch, at once when the tape stopped. A blank curve goes back to
    // where the control was in Touch, and stays where it was left when the tape stopped.
    const auto glide = t == mute ? (juce::int64) 1 : juce::jmax ((juce::int64) 1, (juce::int64) (glideSeconds * tape.getSampleRate()));
    const auto rejoin = endPos + (glideBack ? glide : 1);

    Curve c;
    for (const auto& p : old)
        if (p.pos < w.start)
            c.push_back (p);
    if (c.empty() && w.start > 0)
        c.push_back ({ 0, w.before });
    else if (! c.empty() && w.start > 0)
        c.push_back ({ w.start - 1, w.before });

    const auto recorded = t == mute ? w.points : thin (w.points);
    c.insert (c.end(), recorded.begin(), recorded.end());
    c.push_back ({ endPos, heldValue });
    if (! blank || glideBack)
        c.push_back ({ rejoin, blank ? w.before : evaluate (old, t, rejoin) });
    for (const auto& p : old)
        if (p.pos > rejoin)
            c.push_back (p);

    w.active = false;
    w.points.clear();
    shadow[(size_t) strip][(size_t) t] = heldValue;
    setCurve (strip, t, std::move (c));
    writing[(size_t) strip][(size_t) t].store (false);
}

void Automation::tick()
{
    flushing = true;
    for (auto& row : params)
        for (auto* p : row)
            if (p != nullptr)
                p->flushAudioChange();
    flushing = false;

    const bool rolling = tape.isPlaying();
    const auto pos = tape.getPosition();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const bool held = isMouseHeld != nullptr && isMouseHeld();
    for (int s = 0; s < numStrips; ++s)
        for (int t = 0; t < numTargets; ++t)
        {
            auto& w = writes[(size_t) s][(size_t) t];
            if (! w.active)
                continue;
            if (! rolling)
                endWrite (s, (Target) t, juce::jmax (pos, w.last), false);
            else if (pos < w.last)
                endWrite (s, (Target) t, w.last, false);
            else if (t != mute && getMode (s) == Mode::touch && ! held && now - w.lastMove > touchReleaseMs)
                endWrite (s, (Target) t, pos, true);
            else
                w.last = pos;
        }
}

juce::var Automation::toVar() const
{
    auto* root = new juce::DynamicObject();
    for (int s = 0; s < numStrips; ++s)
    {
        if (params[(size_t) s][fader] == nullptr)
            continue;
        const auto mode = getMode (s);
        if (mode == Mode::read && ! hasCurves (s))
            continue;
        auto* strip = new juce::DynamicObject();
        strip->setProperty ("mode", modeName (mode));
        for (int t = 0; t < numTargets; ++t)
        {
            juce::Array<juce::var> points;
            for (const auto& p : getCurve (s, (Target) t))
                points.add (juce::Array<juce::var> { p.pos, (double) p.value });
            if (! points.isEmpty())
                strip->setProperty (targetName (t), points);
        }
        root->setProperty (params[(size_t) s][fader]->id.upToFirstOccurrenceOf (".", false, false), juce::var (strip));
    }
    return juce::var (root);
}

void Automation::fromVar (const juce::var& v)
{
    clearAll();
    for (int s = 0; s < numStrips; ++s)
    {
        if (params[(size_t) s][fader] == nullptr)
            continue;
        const auto entry = v[juce::Identifier (params[(size_t) s][fader]->id.upToFirstOccurrenceOf (".", false, false))];
        if (! entry.isObject())
            continue;
        for (int t = 0; t < numTargets; ++t)
            if (auto* points = entry[targetName (t)].getArray())
            {
                Curve c;
                for (auto& p : *points)
                    if (p.isArray() && p.size() == 2)
                        c.push_back ({ (juce::int64) p[0], (float) (double) p[1] });
                setCurve (s, (Target) t, std::move (c));
            }
        const auto mode = entry["mode"].toString();
        modes[(size_t) s].store (mode == "off" ? Mode::off : mode == "touch" ? Mode::touch : mode == "latch" ? Mode::latch : Mode::read);
    }
}
