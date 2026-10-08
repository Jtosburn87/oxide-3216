#include "Params.h"
#include <juce_events/juce_events.h>

Param::Param (juce::String i, juce::String n, float mn, float mx, float d, Scale s)
    : id (std::move (i)), name (std::move (n)), min (mn), max (mx), def (d), scale (s), value (d)
{
    jassert (min < max && def >= min && def <= max);
    jassert (scale != Scale::log || min > 0.0f);
}

void Param::set (float newValue)
{
    JUCE_ASSERT_MESSAGE_THREAD
    newValue = juce::jlimit (min, max, newValue);
    if (scale == Scale::toggle)
        newValue = newValue >= 0.5f ? 1.0f : 0.0f;

    if (value.exchange (newValue) != newValue)
        listeners.call ([this] (Listener& l) { l.paramChanged (*this); });
}

float Param::toNormalised (float v) const noexcept
{
    v = juce::jlimit (min, max, v);
    return scale == Scale::log ? std::log (v / min) / std::log (max / min)
                               : (v - min) / (max - min);
}

float Param::fromNormalised (float n) const noexcept
{
    n = juce::jlimit (0.0f, 1.0f, n);
    return scale == Scale::log ? min * std::pow (max / min, n)
                               : min + (max - min) * n;
}

Param& ParamSet::add (const juce::String& id, const juce::String& name, float min, float max, float def, Param::Scale scale)
{
    jassert (byId.find (id) == byId.end());   // ids must be unique
    auto* p = params.add (new Param (id, name, min, max, def, scale));
    byId[id] = p;
    return *p;
}

Param& ParamSet::addToggle (const juce::String& id, const juce::String& name, bool def)
{
    return add (id, name, 0.0f, 1.0f, def ? 1.0f : 0.0f, Param::Scale::toggle);
}

Param* ParamSet::find (const juce::String& id) const
{
    auto it = byId.find (id);
    return it != byId.end() ? it->second : nullptr;
}
