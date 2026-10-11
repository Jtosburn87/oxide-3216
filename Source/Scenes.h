#pragma once

#include "Params.h"
#include <juce_events/juce_events.h>
#include <array>
#include <map>
#include <vector>

/** Scene memory, as on a Yamaha digital console: 99 scenes, each a snapshot of the console's faders and knobs
    (every continuous control). Switches (mutes, solos, assigns, In buttons, type selectors), names, plugins, the
    patch bay and the tape machine are not part of a scene and stay as they are on recall.

    The readout shows the selected scene; the arrows and the Scene Memory window move the selection, and Go
    recalls it, gliding there over the fade time (0 = at once, up to 5 s). A control you move during a fade is
    yours: the fade lets go of it. Message thread only. */
class SceneMemory : private juce::Timer
{
public:
    static constexpr int numScenes = 99;
    static constexpr float maxFadeSeconds = 5.0f;

    explicit SceneMemory (ParamSet&);

    /** The recall fade time in seconds, 0..5 (saved with the session; not part of any scene). */
    Param* fadeTime;

    /** Faders and knobs: continuous controls outside the tape machine, the patch bay's test oscillator and the
        scene fade itself. */
    static bool isSceneControl (const Param&);

    bool isFading() const noexcept { return ! fading.empty(); }
    /** Moves a fade on to this time (juce::Time::getMillisecondCounterHiRes). The timer calls it; tests too. */
    void advanceFade (double nowMs);

    bool isStored (int scene) const;
    juce::String getTitle (int scene) const;
    juce::Time getStoredTime (int scene) const;
    void setTitle (int scene, const juce::String&);

    /** Keeps the console's faders and knobs in a scene (an empty title becomes "Scene NN"). It becomes the
        current and the selected scene. */
    void store (int scene, const juce::String& title);
    /** Sets the faders and knobs to the scene. False (and nothing changes) if it is empty. */
    bool recall (int scene);

    /** The scene the console was last recalled to or stored as (-1: none). */
    int getCurrent() const noexcept { return current; }
    /** The scene on the readout, waiting for Go. */
    int getSelected() const noexcept { return selected; }
    void select (int scene);
    /** The next (+1) or previous (-1) stored scene from the selected one, round the ends; -1 if none is stored. */
    int nextStored (int direction) const;

    /** Has a fader or knob moved since the current scene was recalled or stored? */
    bool isEdited() const;

    /** Goes up on every store, title change and selection, so the session saves itself. */
    std::uint32_t getRevision() const noexcept { return revision; }

    void clear();
    juce::var toVar() const;
    void fromVar (const juce::var&);

    static juce::String number (int scene) { return juce::String (scene + 1).paddedLeft ('0', 2); }

private:
    struct Scene
    {
        bool stored = false;
        juce::String title;
        juce::Time when;
        std::map<juce::String, float> values;
    };

    ParamSet& params;
    std::array<Scene, numScenes> scenes;
    int current = -1, selected = 0;
    std::uint32_t revision = 0;

    // A fade in progress: each control glides from where it was to the scene, in its knob's own travel.
    struct Glide { Param* param; float from, to, target, lastSet; };   // from, to: normalised; target: the scene value
    std::vector<Glide> fading;
    double fadeStart = 0.0, fadeLength = 0.0;
    void timerCallback() override;
    void stopFade();
};
