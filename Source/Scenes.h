#pragma once

#include "Params.h"
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
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

    /** The fade knob, 0..5 s (saved with the session; not a scene control). Store keeps it with the scene as that
        scene's fade out; recall sets it to the recalled scene's fade, and uses it when the current scene has none. */
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
    /** The fade stored with a scene (the fade knob when it was stored), in seconds; -1 if it has none (empty,
        or stored before fades were kept). It is the scene's fade out: recalling any scene from this one glides
        over it, so a scene's fade in is the stored fade of the scene before it. */
    float getStoredFade (int scene) const;
    void setTitle (int scene, const juce::String&);

    /** Keeps the console's faders and knobs in a scene (an empty title becomes "Scene NN"). It becomes the
        current and the selected scene. */
    void store (int scene, const juce::String& title);
    /** Sets the faders and knobs to the scene, over the stored fade of the current scene (the one being left; the
        fade knob if it has none). The fade knob then shows the recalled scene's own fade. False (and nothing
        changes) if it is empty. */
    bool recall (int scene);
    /** The fade a recall would use now. */
    float fadeIntoNext() const;

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

    // MIDI control --------------------------------------------------------------------------------
    /** Scene control over MIDI: a controller per scene selects it (any value above 0; scene NN defaults to CC NN),
        a program change (one number) is Go. It listens to a hardware input (allInputs, a device identifier, or
        empty: none) and/or to MIDI page tracks set to send to scene control, on one channel or all (0). */
    static constexpr const char* allInputs = "*";
    struct MidiSettings
    {
        juce::String input;          // hardware: empty = none, allInputs, or a device identifier
        bool fromMidiPage = true;    // MIDI page tracks with "Send to scene control"
        int channel = 0;             // 0 = any, 1-16
        int goProgram = 0;           // 0-127 (shown as 1-128)
        std::array<int, numScenes> cc {};   // per scene, 0-127 or -1 (none)
    };
    const MidiSettings& getMidiSettings() const noexcept { return midi; }
    void setMidiInput (const juce::String& deviceIdentifier);
    void setMidiFromPage (bool);
    void setMidiChannel (int channel);
    void setGoProgram (int program);
    /** A scene's controller; another scene on that number gives it up. -1 = none. */
    void setSceneCc (int scene, int cc);

    /** Learn: the next controller (for a scene) or program change (for Go) that comes in, from a source scene
        control listens to, is taken. */
    enum class Learn { none, sceneCc, goProgram };
    void learn (Learn what, int scene = -1);
    Learn getLearning() const noexcept { return learning; }
    int getLearningScene() const noexcept { return learningScene; }

    /** A MIDI message for scene control (message thread). internal: from a MIDI page track; else from the hardware
        input deviceIdentifier. True if it did something (selected, Go, or learnt). */
    bool handleMidi (const juce::MidiMessage&, bool internal, const juce::String& deviceIdentifier);

    void clear();
    juce::var toVar() const;
    void fromVar (const juce::var&);

    static juce::String number (int scene) { return juce::String (scene + 1).paddedLeft ('0', 2); }

private:
    MidiSettings midi = defaultMidi();
    static MidiSettings defaultMidi();
    Learn learning = Learn::none;
    int learningScene = -1;

    struct Scene
    {
        bool stored = false;
        juce::String title;
        juce::Time when;
        float fade = -1.0f;   // seconds, -1 = none stored
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
