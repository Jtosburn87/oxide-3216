#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <map>

/** One console control value (knob, fader or button).

    The UI and session code set it on the message thread; the audio thread only reads it.
    Every control has a stable id so sessions can save and restore it. */
class Param
{
public:
    enum class Scale { linear, log, toggle, stepped };   // stepped: whole numbers min..max (a switch position)

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void paramChanged (Param&) = 0;
    };

    Param (juce::String id, juce::String name, float min, float max, float def, Scale scale = Scale::linear);

    const juce::String id, name;
    const float min, max, def;
    const Scale scale;

    float get() const noexcept { return value.load (std::memory_order_relaxed); }
    bool  on() const noexcept  { return get() >= 0.5f; }

    /** Message thread only. Clamps, stores and tells listeners. */
    void set (float newValue);
    void reset() { set (def); }
    void toggle() { set (on() ? 0.0f : 1.0f); }

    float toNormalised (float v) const noexcept;
    float fromNormalised (float n) const noexcept;

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    std::atomic<float> value;
    juce::ListenerList<Listener> listeners;
    std::atomic<std::uint32_t>* changeCounter = nullptr;   // shared by the whole ParamSet (unsaved-changes detection)
    friend class ParamSet;

    JUCE_DECLARE_NON_COPYABLE (Param)
};

/** All the console's parameters. Built once at start-up; never resized while audio runs. */
class ParamSet
{
public:
    Param& add (const juce::String& id, const juce::String& name, float min, float max, float def,
                Param::Scale scale = Param::Scale::linear);
    Param& addToggle (const juce::String& id, const juce::String& name, bool def);
    Param& addStepped (const juce::String& id, const juce::String& name, int numPositions, int def);

    Param* find (const juce::String& id) const;
    const juce::OwnedArray<Param>& all() const noexcept { return params; }

    /** Goes up whenever any parameter changes. */
    std::uint32_t changeCount() const noexcept { return changes.load(); }

private:
    juce::OwnedArray<Param> params;
    std::map<juce::String, Param*> byId;
    std::atomic<std::uint32_t> changes { 0 };
};
