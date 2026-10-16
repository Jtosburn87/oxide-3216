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

    // The snap grid. It follows the tempo (map): bars and beats wherever they fall, seconds in Min:Sec.
    /** The grid line at or before a tick, and the one after a grid line. */
    double gridFloor (double ticks) const;
    double gridNext (double line) const;
    juce::int64 snap (double ticks) const;       // to the nearest grid line (or as it is, with snap off)
    juce::int64 snapDown (double ticks) const;   // to the grid line at or before it (or as it is, with snap off)
    juce::int64 gridStep (double ticks) const;   // the length of the grid step a tick is in
    enum class Line { bar, beat, fine };
    /** The bars and beats grid from `from` to `to` (ticks), with the snap's finer lines in Bars: beats closer than
        6 px and finer lines closer than 8 px at pxPerTick are left out. */
    void forEachGridLine (double from, double to, double pxPerTick, const std::function<void (double tick, Line, int bar)>&) const;

    /** Session > Import tempo map and the page's Tempo map button: a MIDI file's tempo map, from bar 1. */
    static void importTempoMap (AudioEngine&);
    static void clearTempoMap (AudioEngine&);
    /** "12 changes from 120 BPM 4/4" */
    static juce::String describeMap (const std::vector<Tempo::Change>&);
    static const std::vector<Grid>& gridsFor (bool bars);

    void select (int track, int clip);
    /** A clip of the selected track to edit (copy, change, setClips). */
    void changeClips (int track, const std::function<void (std::vector<Midi::Clip>&)>&);
    void refreshAll();

    static constexpr int headerW = 300, rulerH = 28, toolbarH = 50;
    int laneH = 82;   // track height: the vertical zoom (Timeline zoom buttons)
    /** One step taller (+1) or shorter (-1) for every track. */
    void zoomLanes (int direction);

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
    std::uint32_t shownTempo = ~0u;
    std::uint32_t tempoPrint() const { return engine.tempo.getMapRevision() * 1000u + (std::uint32_t) (engine.tempo.getBeatsPerBar() * 20 + engine.tempo.getBeatUnit()); }
    juce::Array<int> shownSlots;

    void rebuildHeaders();
    void layoutHeaders();
    void timerCallback() override;

    friend class Timeline;
};
