#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "InsertsUI.h"
#include "TempoPanel.h"

/** The MIDI page (the MIDI button in the top bar shows it in place of the console): a DAW-style arrange window
    on the same tape. Along the top the transport, where the tape is (bars and minutes), the grid (Bars or Min:Sec)
    and its snap, the tools and + MIDI track. Down the left the tracks: name, R / M / S, the instrument, the MIDI
    input and the routing button (Input Section or Tape). To the right the timeline with the clips; under it the
    piano roll of the selected clip, with velocities. */
class MidiPage : public juce::Component,
                 private juce::Timer
{
public:
    MidiPage (AudioEngine&, PluginContext&);
    ~MidiPage() override;

    /** "Console" was clicked. */
    std::function<void()> onShowConsole;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    // Shared by the page's parts ---------------------------------------------------------
    enum class Tool { pointer, pencil };
    struct Grid { const char* name; double value; };   // bars: a note value (1/value of a whole note, 0 = a bar); min:sec: seconds

    AudioEngine& engine;
    MidiEngine& midi;
    PluginContext& context;

    bool barsGrid = true;
    int snapIndex = 4;          // into the current grid's list
    bool snapOn = true;
    Tool tool = Tool::pointer;
    int selectedTrack = -1, selectedClip = -1;

    /** The snap in ticks (a grid step; fractional in Min:Sec). */
    double snapTicks() const;
    juce::int64 snap (double ticks) const;   // to the nearest grid step (or as it is, with snap off)
    double ticksPerSecond() const;
    juce::int64 barTicks() const { return (juce::int64) engine.tempo.getBeatsPerBar() * Tempo::ticksPerBeat; }
    static const std::vector<Grid>& gridsFor (bool bars);

    void select (int track, int clip);
    /** A clip of the selected track to edit (copy, change, setClips). */
    void changeClips (int track, const std::function<void (std::vector<Midi::Clip>&)>&);
    void refreshAll();

    static constexpr int headerW = 300, laneH = 82, rulerH = 28, toolbarH = 50;

private:
    class Toolbar;
    class TrackHeader;
    class Timeline;
    class PianoRoll;
    class EditorPanel;

    std::unique_ptr<Toolbar> toolbar;
    juce::Component headerHolder;            // the track headers, scrolled with the timeline
    juce::OwnedArray<TrackHeader> headers;
    std::unique_ptr<Timeline> timeline;
    std::unique_ptr<PianoRoll> roll;
    std::unique_ptr<EditorPanel> editorPanel;
    juce::ScrollBar vScroll { true };
    struct VWatch : juce::ScrollBar::Listener
    {
        std::function<void (double)> moved;
        void scrollBarMoved (juce::ScrollBar*, double start) override { if (moved) moved (start); }
    } vWatch;
    int editorH = 330;
    std::uint32_t shownRevision = ~0u;
    juce::Array<int> shownSlots;

    void rebuildHeaders();
    void layoutHeaders();
    void timerCallback() override;

    friend class Timeline;
};
