#include "MidiPage.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "ConsoleLookAndFeel.h"
#include <cmath>
#include <map>
#include <set>

using namespace ConsoleColours;

namespace
{
    const juce::Colour pageBg { 0xff2c2f33 }, headerBg { 0xff33363a }, headerSel { 0xff3d4146 }, laneBg { 0xff26292c },
                       laneAlt { 0xff292c30 }, rulerBg { 0xff303337 }, lineBar { 0xff4a4e53 }, lineBeat { 0xff33373b },
                       lineFine { 0xff2b2e32 }, playheadColour { 0xffff3b2a }, textDim { 0xffaeb3b5 };

    juce::String noteName (int pitch)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 2);   // C3 = 60
    }

    bool isBlackKey (int pitch) { const int k = pitch % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

    juce::String routeText (const AudioEngine& e, int slot)
    {
        const auto r = e.getInstrumentRoute (slot);
        if (r.kind == Midi::Route::none)
            return "Not routed";
        const juce::String n (r.first + 1);
        const auto pair = e.midi.isStereo (slot) ? n + "+" + juce::String (r.first + 2) : n;
        return (r.kind == Midi::Route::input ? "Input " : "Tape ") + pair;
    }

    /** Calls a function when a scroll bar moves. */
    struct ScrollWatch : juce::ScrollBar::Listener
    {
        std::function<void (double)> moved;
        void scrollBarMoved (juce::ScrollBar*, double start) override { if (moved) moved (start); }
    };

    /** H - + and V - +: horizontal and vertical zoom, for the bottom right corner of a view. */
    class ZoomButtons : public juce::Component
    {
    public:
        ZoomButtons()
        {
            const auto setUp = [this] (juce::TextButton& b, const char* text, const juce::String& tip, std::function<void()> fn)
            {
                b.setButtonText (juce::String (juce::CharPointer_UTF8 (text)));
                b.setTooltip (tip);
                b.onClick = std::move (fn);
                addAndMakeVisible (b);
            };
            setUp (hOut, "−", "Zoom out across (Ctrl or Alt + mouse wheel too)", [this] { if (onHorizontal) onHorizontal (-1); });
            setUp (hIn, "+", "Zoom in across (Ctrl or Alt + mouse wheel too)", [this] { if (onHorizontal) onHorizontal (1); });
            setUp (vOut, "−", "Zoom out down: shorter rows", [this] { if (onVertical) onVertical (-1); });
            setUp (vIn, "+", "Zoom in down: taller rows", [this] { if (onVertical) onVertical (1); });
        }

        std::function<void (int)> onHorizontal, onVertical;
        static constexpr int width = 152;

        void resized() override
        {
            auto r = getLocalBounds();
            hLabel = r.removeFromLeft (14);
            hOut.setBounds (r.removeFromLeft (30));
            hIn.setBounds (r.removeFromLeft (30));
            r.removeFromLeft (8);
            vLabel = r.removeFromLeft (14);
            vOut.setBounds (r.removeFromLeft (30));
            vIn.setBounds (r.removeFromLeft (30));
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xffaeb3b5));
            g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
            g.drawText ("H", hLabel, juce::Justification::centred, false);
            g.drawText ("V", vLabel, juce::Justification::centred, false);
        }

    private:
        juce::TextButton hOut, hIn, vOut, vIn;
        juce::Rectangle<int> hLabel, vLabel;
    };

    void styleSmall (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonOnColourId, amber);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1b1b1b));
    }
}

//==============================================================================
const std::vector<MidiPage::Grid>& MidiPage::gridsFor (bool bars)
{
    static const std::vector<Grid> barGrids { { "Bar", 0.0 }, { "1/2", 2.0 }, { "1/4", 4.0 }, { "1/8", 8.0 }, { "1/16", 16.0 }, { "1/32", 32.0 } };
    static const std::vector<Grid> secGrids { { "1 s", 1.0 }, { "1/2 s", 0.5 }, { "1/4 s", 0.25 }, { "1/10 s", 0.1 } };
    return bars ? barGrids : secGrids;
}

double MidiPage::ticksPerSecond() const
{
    const double sr = engine.tape.getSampleRate();
    return Tempo::ticksPerBeat * sr / engine.tempo.samplesPerBeat (sr);
}

double MidiPage::snapTicks() const
{
    const auto& grids = gridsFor (barsGrid);
    const auto& g = grids[(size_t) juce::jlimit (0, (int) grids.size() - 1, snapIndex)];
    if (! barsGrid)
        return g.value * ticksPerSecond();
    if (g.value <= 0.0)
        return (double) barTicks();
    return Tempo::ticksPerBeat * engine.tempo.getBeatUnit() / g.value;   // a 1/value note
}

juce::int64 MidiPage::snap (double ticks) const
{
    if (! snapOn)
        return (juce::int64) std::llround (ticks);
    const double g = juce::jmax (1.0, snapTicks());
    return (juce::int64) std::llround (std::round (ticks / g) * g);
}

void MidiPage::select (int track, int clip)
{
    selectedTrack = track;
    selectedClip = clip;
    if (midi.isUsed (track))
        midi.setSelected (track);
    refreshAll();
}

void MidiPage::changeClips (int track, const std::function<void (std::vector<Midi::Clip>&)>& fn)
{
    if (! midi.isUsed (track))
        return;
    auto clips = midi.getClips (track);
    fn (clips);
    midi.setClips (track, std::move (clips));
}

//==============================================================================
/** Transport, position readouts, grid and snap, tools, + MIDI track and the way back to the console. */
class MidiPage::Toolbar : public juce::Component
{
public:
    explicit Toolbar (MidiPage& p) : page (p)
    {
        auto& tape = page.engine.tape;
        const auto glyph = [] (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); };
        rew.setButtonText (glyph ("\xe2\x97\x80\xe2\x97\x80"));
        stop.setButtonText (glyph ("\xe2\x96\xa0"));
        play.setButtonText (glyph ("\xe2\x96\xb6"));
        rec.setButtonText (glyph ("\xe2\x97\x8f"));
        rew.setTitle ("Return to zero");
        stop.setTitle ("Stop");
        play.setTitle ("Play");
        rec.setTitle ("Record");
        rew.setTooltip ("Back to the start of the tape (bar 1)");
        rec.setTooltip ("Record enable: armed tape tracks and armed MIDI tracks record while the tape plays");
        rew.onClick = [&tape] { tape.windTo (0); };
        stop.onClick = [&tape] { tape.stop(); };
        play.onClick = [&tape] { tape.play(); };
        rec.onClick = [&tape] { tape.setRecordEnabled (! tape.isRecordEnabled()); };
        rec.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd6392d));
        rec.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        play.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3fa34d));

        bars.setTooltip ("Where the tape is: bar | beat | tick");
        clock.setTooltip ("Where the tape is: minutes and seconds");
        tempoText.setTooltip ("Tempo and time signature (set them in the Tempo box on the console)");

        for (auto* b : { &gridBars, &gridSecs })
        {
            b->setRadioGroupId (101);
            b->setClickingTogglesState (true);
            styleSmall (*b);
        }
        gridBars.setToggleState (true, juce::dontSendNotification);
        gridBars.setTooltip ("Grid in bars and beats");
        gridSecs.setTooltip ("Grid in minutes and seconds");
        gridBars.onClick = [this] { setGrid (true); };
        gridSecs.onClick = [this] { setGrid (false); };

        snapBox.setTooltip ("Snap value: clips and notes land on it");
        snapBox.onChange = [this] { page.snapIndex = snapBox.getSelectedItemIndex(); };
        snapOn.setClickingTogglesState (true);
        snapOn.setToggleState (true, juce::dontSendNotification);
        styleSmall (snapOn);
        snapOn.setTooltip ("Snap to the grid (off: anywhere)");
        snapOn.onClick = [this] { page.snapOn = snapOn.getToggleState(); };
        fillSnap();

        for (auto* b : { &pointer, &pencil })
        {
            b->setRadioGroupId (102);
            b->setClickingTogglesState (true);
            styleSmall (*b);
        }
        pointer.setToggleState (true, juce::dontSendNotification);
        pointer.setTooltip ("Pointer: select, move (drag) and trim (drag an edge) clips and notes. Double-click empty space to add one.");
        pencil.setTooltip ("Pencil: click or drag in empty space to draw a clip or a note");
        pointer.onClick = [this] { page.tool = Tool::pointer; };
        pencil.onClick = [this] { page.tool = Tool::pencil; };

        addTrack.setTooltip ("Add a MIDI track (up to " + juce::String (Config::numMidiTracks) + ")");
        addTrack.onClick = [this]
        {
            const int k = page.midi.addTrack();
            if (k < 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MIDI tracks",
                                                        "All " + juce::String (Config::numMidiTracks) + " MIDI tracks are in use.");
            else
                page.select (k, -1);
        };
        console.setTooltip ("Back to the console");
        console.onClick = [this] { if (page.onShowConsole) page.onShowConsole(); };

        for (juce::Component* c : { (juce::Component*) &rew, (juce::Component*) &stop, (juce::Component*) &play, (juce::Component*) &rec,
                                    (juce::Component*) &bars, (juce::Component*) &clock, (juce::Component*) &tempoText,
                                    (juce::Component*) &gridBars, (juce::Component*) &gridSecs, (juce::Component*) &snapBox,
                                    (juce::Component*) &snapOn, (juce::Component*) &pointer, (juce::Component*) &pencil,
                                    (juce::Component*) &addTrack, (juce::Component*) &console })
            addAndMakeVisible (c);
    }

    void setGrid (bool b)
    {
        if (page.barsGrid == b)
            return;
        page.barsGrid = b;
        page.snapIndex = b ? 4 : 1;
        fillSnap();
        page.refreshAll();
    }

    void fillSnap()
    {
        snapBox.clear (juce::dontSendNotification);
        int id = 1;
        for (auto& g : gridsFor (page.barsGrid))
            snapBox.addItem (g.name, id++);
        snapBox.setSelectedItemIndex (page.snapIndex, juce::dontSendNotification);
    }

    void update()
    {
        auto& tape = page.engine.tape;
        play.setToggleState (tape.isPlaying(), juce::dontSendNotification);
        rec.setToggleState (tape.isRecordEnabled(), juce::dontSendNotification);
        const auto pos = page.engine.tempo.positionAt (tape.getPosition(), tape.getSampleRate());
        bars.setText (juce::String::formatted ("%03d|%d|%03d", pos.bar, pos.beat, pos.tick));
        const auto tenths = (juce::int64) ((double) tape.getPosition() / juce::jmax (1.0, tape.getSampleRate()) * 10.0);
        clock.setText (juce::String::formatted ("%02d:%02d.%d", (int) (tenths / 600), (int) ((tenths / 10) % 60), (int) (tenths % 10)));
        tempoText.setText (BpmReadout::format (page.engine.tempo.bpm->get()) + " " + page.engine.tempo.signatureText());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        for (auto* b : { &rew, &stop, &play, &rec })
        {
            b->setBounds (r.removeFromLeft (40));
            r.removeFromLeft (2);
        }
        r.removeFromLeft (10);
        bars.setBounds (r.removeFromLeft (170));
        r.removeFromLeft (6);
        clock.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (6);
        tempoText.setBounds (r.removeFromLeft (150));
        r.removeFromLeft (16);
        gridLabel = r.removeFromLeft (34);
        gridBars.setBounds (r.removeFromLeft (52));
        gridSecs.setBounds (r.removeFromLeft (64));
        r.removeFromLeft (12);
        snapLabel = r.removeFromLeft (38);
        snapBox.setBounds (r.removeFromLeft (84));
        r.removeFromLeft (4);
        snapOn.setBounds (r.removeFromLeft (78));
        r.removeFromLeft (16);
        pointer.setBounds (r.removeFromLeft (70));
        pencil.setBounds (r.removeFromLeft (66));
        console.setBounds (r.removeFromRight (100));
        r.removeFromRight (8);
        addTrack.setBounds (r.removeFromRight (120));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff3a3d41));
        g.setColour (juce::Colour (0xff1d1f22));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        g.setColour (textDim);
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        g.drawText ("GRID", gridLabel, juce::Justification::centredLeft, false);
        g.drawText ("SNAP", snapLabel, juce::Justification::centredLeft, false);
    }

private:
    MidiPage& page;
    juce::TextButton rew, stop, play, rec;
    SegmentDisplay bars, clock, tempoText;
    juce::TextButton gridBars { "Bars" }, gridSecs { "Min:Sec" }, snapOn { "Snap on" };
    juce::ComboBox snapBox;
    juce::TextButton pointer { "Pointer" }, pencil { "Pencil" };
    juce::TextButton addTrack { "+ MIDI track" }, console { juce::CharPointer_UTF8 ("Console \xe2\x97\x82") };
    juce::Rectangle<int> gridLabel, snapLabel;
};

//==============================================================================
/** One track's header: name, R / M / S, the instrument, the MIDI input and the routing button. */
class MidiPage::TrackHeader : public juce::Component
{
public:
    TrackHeader (MidiPage& p, int s) : page (p), slot (s)
    {
        name.setEditable (false, true, false);
        name.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
        name.setColour (juce::Label::textColourId, legend);
        name.setTooltip ("Double-click to rename");
        name.onTextChange = [this] { page.midi.setName (slot, name.getText()); };
        name.addMouseListener (this, false);
        addAndMakeVisible (name);

        rec.setTooltip ("Record arm: plays the keyboard, and records it while the tape records");
        mute.setTooltip ("Mute the track's clips");
        solo.setTooltip ("Solo: only soloed MIDI tracks play their clips");
        rec.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd6392d));
        rec.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        styleSmall (mute);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3fa34d));
        rec.onClick = [this] { page.midi.setArmed (slot, ! page.midi.isArmed (slot)); };
        mute.onClick = [this] { page.midi.setMuted (slot, ! page.midi.isMuted (slot)); };
        solo.onClick = [this] { page.midi.setSoloed (slot, ! page.midi.isSoloed (slot)); };

        instrument.setTooltip ("The instrument: click to open it (or, with none, to choose one)");
        instrument.onClick = [this] { openInstrument(); };
        list.setTooltip ("Choose the instrument (VST3 instruments)");
        list.onClick = [this] { chooseInstrument(); };
        input.setTooltip ("The MIDI input this track listens to");
        input.onClick = [this] { chooseInput(); };
        route.setTooltip ("Where the instrument goes: an input channel (a stereo instrument takes a linked pair) or a tape track. "
                          "It is patched in the patch bay too.");
        route.onClick = [this] { chooseRoute(); };
        for (auto* b : { &rec, &mute, &solo, &instrument, &list, &input, &route })
            addAndMakeVisible (b);
        refresh();
    }

    const int slot;

    void refresh()
    {
        auto& m = page.midi;
        if (! name.isBeingEdited())
            name.setText (m.getName (slot), juce::dontSendNotification);
        rec.setToggleState (m.isArmed (slot), juce::dontSendNotification);
        mute.setToggleState (m.isMuted (slot), juce::dontSendNotification);
        solo.setToggleState (m.isSoloed (slot), juce::dontSendNotification);
        auto* p = m.getInstrument (slot);
        instrument.setButtonText (p != nullptr ? m.getDescription (slot).name : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 no instrument \xe2\x80\x94")));
        instrument.setColour (juce::TextButton::textColourOffId, p != nullptr ? amber : textDim);
        const auto in = m.getInput (slot);
        juce::String inName = in.isEmpty() ? "None" : in == MidiEngine::allInputs ? "All inputs" : in;
        for (auto& d : MidiEngine::availableInputs())
            if (d.identifier == in)
                inName = d.name;
        input.setButtonText ("In: " + inName);
        route.setButtonText ("Out: " + routeText (page.engine, slot) + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
        repaint();
    }

    void resized() override
    {
        // Short tracks (zoomed out) drop the routing row, then the instrument row.
        auto r = getLocalBounds().reduced (8, getHeight() < 50 ? 5 : 6).withTrimmedLeft (8);
        auto top = r.removeFromTop (22);
        for (auto* b : { &solo, &mute, &rec })
        {
            b->setBounds (top.removeFromRight (24));
            top.removeFromRight (3);
        }
        name.setBounds (top);
        const bool showInstrument = r.getHeight() >= 24, showRouting = r.getHeight() >= 48;
        for (auto* b : { &instrument, &list })
            b->setVisible (showInstrument);
        for (auto* b : { &input, &route })
            b->setVisible (showRouting);
        if (! showInstrument)
            return;
        r.removeFromTop (4);
        auto mid = r.removeFromTop (20);
        list.setBounds (mid.removeFromRight (24));
        mid.removeFromRight (3);
        instrument.setBounds (mid);
        if (! showRouting)
            return;
        r.removeFromTop (4);
        auto bottom = r.removeFromTop (20);
        route.setBounds (bottom.removeFromRight (bottom.getWidth() / 2));
        bottom.removeFromRight (3);
        input.setBounds (bottom);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (page.selectedTrack == slot ? headerSel : headerBg);
        g.setColour (page.midi.getColour (slot));
        g.fillRect (4, 6, 6, getHeight() - 12);
        g.setColour (juce::Colour (0xff1d1f22));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showMenu();
        else if (page.selectedTrack != slot)
            page.select (slot, -1);
    }

private:
    MidiPage& page;
    juce::Label name;
    juce::TextButton rec { "R" }, mute { "M" }, solo { "S" }, instrument, list { juce::CharPointer_UTF8 ("\xe2\x89\xa1") }, input, route;

    void openInstrument()
    {
        if (auto* p = page.midi.getInstrument (slot))
            page.context.windows.open (*p, page.midi.getName (slot) + " - " + page.midi.getDescription (slot).name);
        else
            chooseInstrument();
    }

    void chooseInstrument()
    {
        auto& mgr = page.context.manager;
        const auto instruments = mgr.instruments();
        juce::PopupMenu menu, byMaker;
        std::map<juce::String, juce::PopupMenu> makers;
        for (int i = 0; i < instruments.size(); ++i)
            makers[instruments[i].manufacturerName.isNotEmpty() ? instruments[i].manufacturerName : juce::String ("Other")]
                .addItem (i + 1, instruments[i].name, true, instruments[i].createIdentifierString() == page.midi.getDescription (slot).createIdentifierString());
        for (auto& [maker, sub] : makers)
            menu.addSubMenu (maker, sub);
        if (instruments.isEmpty())
            menu.addItem (-1, "No VST3 instruments found (Options > Plugins to scan)", false);
        if (page.midi.getInstrument (slot) != nullptr)
        {
            menu.addSeparator();
            menu.addItem (10000, "Remove the instrument");
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list),
                            [safe = juce::Component::SafePointer<TrackHeader> (this), instruments] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                auto& pg = safe->page;
                                const int s = safe->slot;
                                if (result == 10000)
                                {
                                    pg.midi.clearInstrument (s);
                                    pg.refreshAll();
                                    return;
                                }
                                const auto d = instruments[result - 1];
                                juce::String error;
                                auto p = pg.context.manager.create (d, pg.context.sampleRate(), Config::maxBlock, error);
                                if (p == nullptr)
                                {
                                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Instrument",
                                                                            d.name + " could not be loaded.\n\n" + error);
                                    return;
                                }
                                const auto routeBefore = pg.engine.getInstrumentRoute (s);
                                pg.midi.setInstrument (s, std::move (p), d);
                                if (routeBefore.kind != Midi::Route::none)
                                    pg.engine.routeInstrument (s, routeBefore);   // a mono / stereo change takes one or two inputs
                                if (pg.context.changed)
                                    pg.context.changed();
                                if (auto* inst = pg.midi.getInstrument (s))
                                    pg.context.windows.open (*inst, pg.midi.getName (s) + " - " + d.name);
                                pg.refreshAll();
                            });
    }

    void chooseInput()
    {
        juce::PopupMenu menu;
        const auto now = page.midi.getInput (slot);
        const auto devices = MidiEngine::availableInputs();
        menu.addItem (1, "All inputs", true, now == MidiEngine::allInputs);
        menu.addItem (2, "None", true, now.isEmpty());
        if (! devices.isEmpty())
            menu.addSeparator();
        for (int i = 0; i < devices.size(); ++i)
            menu.addItem (10 + i, devices[i].name, true, devices[i].identifier == now);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&input),
                            [safe = juce::Component::SafePointer<TrackHeader> (this), devices] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                safe->page.midi.setInput (safe->slot, result == 1 ? juce::String (MidiEngine::allInputs)
                                                                    : result == 2 ? juce::String() : devices[result - 10].identifier);
                                safe->refresh();
                            });
    }

    void chooseRoute()
    {
        auto& e = page.engine;
        const bool stereo = page.midi.isStereo (slot);
        const auto now = e.getInstrumentRoute (slot);
        const auto build = [&] (Midi::Route::Kind kind, int count, int base, const juce::String& word)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < count; i += stereo ? 2 : 1)
            {
                auto text = word + " " + juce::String (i + 1) + (stereo ? "+" + juce::String (i + 2) : juce::String());
                const int other = e.instrumentRoutedInto (base + i);
                const int other2 = stereo ? e.instrumentRoutedInto (base + i + 1) : -1;
                for (int o : { other, other2 })
                    if (o >= 0 && o != slot)
                    {
                        text << "   (" << page.midi.getName (o) << ")";
                        break;
                    }
                sub.addItem ((kind == Midi::Route::input ? 100 : 200) + i, text, true, now.kind == kind && now.first == i);
            }
            return sub;
        };
        juce::PopupMenu menu;
        menu.addSubMenu ("Input Section", build (Midi::Route::input, Config::numInputStrips, AudioEngine::Patch::channelIn, "Input"),
                         true, nullptr, now.kind == Midi::Route::input);
        menu.addSubMenu ("Tape", build (Midi::Route::tape, Config::numTracks, AudioEngine::Patch::tapeIn, "Tape"),
                         true, nullptr, now.kind == Midi::Route::tape);
        menu.addSeparator();
        menu.addItem (1, "Not routed", true, now.kind == Midi::Route::none);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&route),
                            [safe = juce::Component::SafePointer<TrackHeader> (this)] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                Midi::Route r;
                                if (result >= 200)      r = { Midi::Route::tape, result - 200 };
                                else if (result >= 100) r = { Midi::Route::input, result - 100 };
                                safe->page.engine.routeInstrument (safe->slot, r);
                                if (safe->page.context.changed)
                                    safe->page.context.changed();   // the routing (interface inputs) is in the settings
                                safe->page.refreshAll();
                            });
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Rename");
        menu.addItem (2, "Remove track");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<TrackHeader> (this)] (int result)
                            {
                                if (safe == nullptr)
                                    return;
                                if (result == 1)
                                    safe->name.showEditor();
                                else if (result == 2)
                                {
                                    auto& pg = safe->page;
                                    const int s = safe->slot;
                                    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                                      .withTitle ("Remove " + pg.midi.getName (s))
                                                                      .withMessage ("Remove this track with its clips and its instrument? This can't be undone.")
                                                                      .withButton ("Remove").withButton ("Cancel"),
                                                                  [&pg, s] (int r)
                                                                  {
                                                                      if (r != 1)
                                                                          return;
                                                                      pg.engine.removeMidiTrack (s);
                                                                      if (pg.context.changed)
                                                                          pg.context.changed();
                                                                      pg.select (pg.midi.getSelected(), -1);
                                                                  });
                                }
                            });
    }
};

//==============================================================================
/** The ruler and the lanes with their clips. Bars | beats or minutes : seconds, snapping to the toolbar's grid. */
class MidiPage::Timeline : public juce::Component,
                            public juce::FileDragAndDropTarget
{
public:
    explicit Timeline (MidiPage& p) : page (p)
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (hScroll);
        hScroll.setAutoHide (false);
        hWatch.moved = [this] (double start) { scrollBeats = start; page.refreshAll(); };
        hScroll.addListener (&hWatch);
        addAndMakeVisible (zoom);
        zoom.onHorizontal = [this] (int d) { zoomAcross (d > 0 ? 1.25 : 0.8); };
        zoom.onVertical = [this] (int d) { page.zoomLanes (d); };
    }

    double pxPerBeat = 40.0, scrollBeats = 0.0;
    int scrollY = 0;
    juce::ScrollBar hScroll { false };
    ScrollWatch hWatch;
    ZoomButtons zoom;

    double xForTick (double ticks) const { return (ticks / Tempo::ticksPerBeat - scrollBeats) * pxPerBeat; }
    double tickForX (double x) const { return (x / pxPerBeat + scrollBeats) * Tempo::ticksPerBeat; }
    int laneTop (int row) const { return rulerH + row * page.laneH - scrollY; }
    int areaHeight() const { return getHeight() - 26; }   // above the scroll bar and zoom buttons

    void updateScroll()
    {
        const double visibleBeats = getWidth() / pxPerBeat;
        const double songBeats = (double) page.midi.songEnd() / Tempo::ticksPerBeat;
        const double tapeBeats = page.engine.tape.getLength() / page.engine.tempo.samplesPerBeat (page.engine.tape.getSampleRate());
        const double total = juce::jmax (songBeats, tapeBeats, scrollBeats + visibleBeats) + 16.0;
        hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
        hScroll.setCurrentRange (scrollBeats, visibleBeats, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromBottom (26);
        zoom.setBounds (bar.removeFromRight (ZoomButtons::width).reduced (0, 1));
        bar.removeFromRight (6);
        hScroll.setBounds (bar.reduced (0, 5));
        updateScroll();
    }

    /** Zooms across round the middle of the view. */
    void zoomAcross (double factor)
    {
        const double centreBeats = scrollBeats + getWidth() * 0.5 / pxPerBeat;
        pxPerBeat = juce::jlimit (4.0, 400.0, pxPerBeat * factor);
        scrollBeats = juce::jmax (0.0, centreBeats - getWidth() * 0.5 / pxPerBeat);
        updateScroll();
        page.refreshAll();
    }

    void paint (juce::Graphics& g) override
    {
        const auto slots = page.midi.usedSlots();
        const int w = getWidth(), h = areaHeight();
        g.setColour (laneBg);
        g.fillRect (0, 0, w, h);
        g.reduceClipRegion (0, 0, w, h);

        for (int i = 0; i < slots.size(); ++i)
        {
            g.setColour (slots[i] == page.selectedTrack ? laneAlt.brighter (0.06f) : (i % 2 == 0 ? laneBg : laneAlt));
            g.fillRect (0, laneTop (i), w, page.laneH);
            g.setColour (juce::Colour (0xff1d1f22));
            g.fillRect (0, laneTop (i) + page.laneH - 1, w, 1);
        }
        paintGrid (g, rulerH, h);

        for (int i = 0; i < slots.size(); ++i)
        {
            const auto& clips = page.midi.getClips (slots[i]);
            for (int c = 0; c < (int) clips.size(); ++c)
                paintClip (g, clips[(size_t) c], slots[i], laneTop (i) + 4, slots[i] == page.selectedTrack && c == page.selectedClip);
        }
        if (dragNew)
        {
            g.setColour (page.midi.getColour (dragTrack).withAlpha (0.35f));
            const auto r = juce::Rectangle<double> (xForTick ((double) newStart), laneTop (rowOf (dragTrack)) + 4.0,
                                                    xForTick ((double) newEnd) - xForTick ((double) newStart), page.laneH - 8.0);
            g.fillRect (r.toFloat());
        }

        paintRuler (g);

        if (dropping)
        {
            const float dx = (float) xForTick ((double) dropTick ((float) dropAt.x));
            g.setColour (amber);
            g.fillRect (dx - 1.0f, 0.0f, 2.0f, (float) h);
            const int row = dropAt.y < rulerH ? 0 : (int) std::floor ((dropAt.y - rulerH + scrollY) / (double) page.laneH);
            const auto where = juce::isPositiveAndBelow (row, slots.size()) ? page.midi.getName (slots[row]) : juce::String ("a new track");
            g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
            g.drawText ("Drop onto " + where, juce::Rectangle<float> (dx + 6.0f, (float) rulerH + 4.0f, 240.0f, 16.0f), juce::Justification::centredLeft, false);
            if (juce::isPositiveAndBelow (row, slots.size()))
            {
                g.setColour (amber.withAlpha (0.12f));
                g.fillRect (0, laneTop (row), w, page.laneH);
            }
        }

        // In / Out (the tape's locators) on the ruler, and the play line.
        auto& tape = page.engine.tape;
        const double perSample = Tempo::ticksPerBeat / page.engine.tempo.samplesPerBeat (tape.getSampleRate());
        if (tape.getOutPoint() > tape.getInPoint())
        {
            const float x0 = (float) xForTick ((double) tape.getInPoint() * perSample), x1 = (float) xForTick ((double) tape.getOutPoint() * perSample);
            g.setColour (juce::Colour (0x553a8fd6));
            g.fillRect (x0, (float) rulerH - 6.0f, x1 - x0, 6.0f);
        }
        const float px = (float) xForTick ((double) tape.getPosition() * perSample);
        g.setColour (playheadColour);
        g.fillRect (px - 1.0f, 0.0f, 2.0f, (float) h);

        if (slots.isEmpty())
        {
            g.setColour (textDim);
            g.setFont (ConsoleLookAndFeel::uiFont (15.0f));
            g.drawText ("No MIDI tracks yet: click + MIDI track", getLocalBounds().withTrimmedTop (rulerH).withHeight (120), juce::Justification::centred, false);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCommandDown() || e.mods.isAltDown())
        {
            // Zoom round the mouse.
            const double at = tickForX (e.position.x);
            pxPerBeat = juce::jlimit (4.0, 400.0, pxPerBeat * (wheel.deltaY > 0 ? 1.25 : 0.8));
            scrollBeats = juce::jmax (0.0, at / Tempo::ticksPerBeat - e.position.x / pxPerBeat);
        }
        else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
            scrollBeats = juce::jmax (0.0, scrollBeats - (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 200.0 / pxPerBeat);
        else
            page.vScroll.setCurrentRangeStart (page.vScroll.getCurrentRangeStart() - wheel.deltaY * 120.0);
        updateScroll();
        page.refreshAll();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        dragMode = none;
        dragNew = false;
        const auto pos = e.position;
        if (pos.y < rulerH)
        {
            locateTo (pos.x);
            dragMode = locating;
            return;
        }
        const int row = (int) std::floor ((pos.y - rulerH + scrollY) / page.laneH);
        const auto slots = page.midi.usedSlots();
        if (! juce::isPositiveAndBelow (row, slots.size()))
            return;
        const int track = slots[row];
        const int clip = clipAt (track, pos.x);
        if (clip >= 0)
        {
            page.select (track, clip);
            if (e.mods.isPopupMenu())
            {
                clipMenu (track, clip);
                return;
            }
            if (e.mods.isAltDown() && page.tool == Tool::pointer)
            {
                // Alt: a copy of the clip comes away with the mouse (just a click: the copy goes straight after it).
                const auto copy = page.midi.getClips (track)[(size_t) clip];
                int index = clip;
                page.changeClips (track, [&] (std::vector<Midi::Clip>& clips) { clips.push_back (copy); index = (int) clips.size() - 1; });
                page.select (track, index);
                dragMode = move;
                dragTrack = track;
                original = copy;
                downTick = tickForX (pos.x);
                altCopy = true;
                return;
            }
            const auto& c = page.midi.getClips (track)[(size_t) clip];
            const double x0 = xForTick ((double) c.start), x1 = xForTick ((double) c.end());
            dragMode = pos.x < x0 + 6 ? trimStart : pos.x > x1 - 6 ? trimEnd : move;
            dragTrack = track;
            original = c;
            downTick = tickForX (pos.x);
            return;
        }
        if (page.selectedTrack != track || page.selectedClip >= 0)
            page.select (track, -1);
        if (page.tool == Tool::pencil && ! e.mods.isPopupMenu())
        {
            dragNew = true;
            dragTrack = track;
            newStart = page.snapOn ? floorToGrid (tickForX (pos.x)) : (juce::int64) tickForX (pos.x);
            newEnd = newStart + juce::jmax ((juce::int64) page.snapTicks(), (juce::int64) 1);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double tick = tickForX (e.position.x);
        if (dragMode == locating)
        {
            locateTo (e.position.x);
            return;
        }
        if (dragNew)
        {
            newEnd = juce::jmax (newStart + 1, page.snap (tick));
            if (newEnd <= newStart)
                newEnd = newStart + (juce::int64) juce::jmax (1.0, page.snapTicks());
            repaint();
            return;
        }
        if (dragMode == none)
            return;
        const auto delta = page.snap ((double) original.start + (tick - downTick)) - original.start;
        page.changeClips (dragTrack, [&] (std::vector<Midi::Clip>& clips)
        {
            if (! juce::isPositiveAndBelow (page.selectedClip, (int) clips.size()))
                return;
            auto& c = clips[(size_t) page.selectedClip];
            if (dragMode == move)
                c.start = juce::jmax ((juce::int64) 0, original.start + delta);
            else if (dragMode == trimEnd)
                c.length = juce::jmax ((juce::int64) 1, page.snap ((double) original.end() + (tick - downTick)) - original.start);
            else if (dragMode == trimStart)
            {
                // The left edge moves; the notes stay where they are in time.
                const auto newStartTick = juce::jlimit ((juce::int64) 0, original.end() - 1, page.snap ((double) original.start + (tick - downTick)));
                const auto shift = newStartTick - original.start;
                c = original;
                c.start = newStartTick;
                c.length = original.length - shift;
                std::vector<Midi::Note> kept;
                for (auto n : original.notes)
                {
                    n.start -= shift;
                    if (n.start >= 0 && n.start < c.length)
                        kept.push_back (n);
                }
                c.notes = kept;
                std::vector<Midi::Control> keptControls;
                for (auto ctl : original.controls)
                {
                    ctl.tick -= shift;
                    if (ctl.tick >= 0 && ctl.tick < c.length)
                        keptControls.push_back (ctl);
                }
                c.controls = keptControls;
            }
        });
        page.refreshAll();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (altCopy)
        {
            altCopy = false;
            const auto& clips = page.midi.getClips (dragTrack);
            if (juce::isPositiveAndBelow (page.selectedClip, (int) clips.size()) && clips[(size_t) page.selectedClip].start == original.start)
            {
                const int index = page.selectedClip;
                const auto after = original.end();
                page.changeClips (dragTrack, [index, after] (std::vector<Midi::Clip>& cl) { cl[(size_t) index].start = after; });
                page.refreshAll();
            }
        }
        if (dragNew)
        {
            dragNew = false;
            const auto start = newStart, end = juce::jmax (newEnd, newStart + 1);
            int index = 0;
            page.changeClips (dragTrack, [&] (std::vector<Midi::Clip>& clips)
            {
                Midi::Clip c;
                c.start = start;
                c.length = end - start;
                c.name = page.midi.getName (dragTrack) + " " + juce::String ((int) clips.size() + 1);
                clips.push_back (c);
                index = (int) clips.size() - 1;
            });
            page.select (dragTrack, index);
        }
        dragMode = none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Pointer tool: double-click empty lane space for a one-bar clip.
        if (e.position.y < rulerH || page.tool != Tool::pointer)
            return;
        const int row = (int) std::floor ((e.position.y - rulerH + scrollY) / page.laneH);
        const auto slots = page.midi.usedSlots();
        if (! juce::isPositiveAndBelow (row, slots.size()) || clipAt (slots[row], e.position.x) >= 0)
            return;
        const int track = slots[row];
        const auto bar = page.barTicks();
        const auto start = (juce::int64) (tickForX (e.position.x)) / bar * bar;
        int index = 0;
        page.changeClips (track, [&] (std::vector<Midi::Clip>& clips)
        {
            Midi::Clip c;
            c.start = start;
            c.length = bar;
            c.name = page.midi.getName (track) + " " + juce::String ((int) clips.size() + 1);
            clips.push_back (c);
            index = (int) clips.size() - 1;
        });
        page.select (track, index);
    }

    void deleteSelected()
    {
        if (! page.midi.isUsed (page.selectedTrack) || page.selectedClip < 0)
            return;
        const int clip = page.selectedClip;
        page.changeClips (page.selectedTrack, [clip] (std::vector<Midi::Clip>& clips)
        {
            if (juce::isPositiveAndBelow (clip, (int) clips.size()))
                clips.erase (clips.begin() + clip);
        });
        page.select (page.selectedTrack, -1);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }
        return false;
    }

    // MIDI files dropped on the timeline (a plugin's groove browser, or Explorer): a clip on the lane under the mouse,
    // snapped to the grid. Below the last lane: a new track.
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (MidiEngine::isMidiFile (f))
                return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { dropAt = { x, y }; dropping = true; repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override  { dropAt = { x, y }; repaint(); }
    void fileDragExit (const juce::StringArray&) override                { dropping = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        dropping = false;
        const auto slots = page.midi.usedSlots();
        const int row = y < rulerH ? 0 : (int) std::floor ((y - rulerH + scrollY) / (double) page.laneH);
        int track = juce::isPositiveAndBelow (row, slots.size()) ? slots[row] : -1;
        auto start = dropTick ((float) x);
        juce::StringArray problems;
        int index = -1;
        for (auto& path : files)
        {
            if (! MidiEngine::isMidiFile (path))
                continue;
            juce::String error;
            auto clip = MidiEngine::readMidiFile (juce::File (path), page.engine.tempo.getBeatUnit(), page.engine.tempo.getBeatsPerBar(),
                                                  page.ticksPerSecond(), error);
            if (error.isNotEmpty())
            {
                problems.add (error);
                continue;
            }
            if (track < 0)
            {
                track = page.midi.addTrack (clip.name);
                if (track < 0)
                {
                    problems.add ("All " + juce::String (Config::numMidiTracks) + " MIDI tracks are in use.");
                    break;
                }
            }
            clip.start = start;
            start = clip.end();   // several files land one after another
            page.changeClips (track, [&] (std::vector<Midi::Clip>& clips) { clips.push_back (clip); index = (int) clips.size() - 1; });
        }
        if (track >= 0 && index >= 0)
            page.select (track, index);
        repaint();
        if (! problems.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI file", problems.joinIntoString ("\n"));
    }

    /** Keeps the play line in view while the tape runs. */
    void follow()
    {
        auto& tape = page.engine.tape;
        if (! tape.isPlaying() || dragMode != none)
            return;
        const double x = xForTick ((double) tape.getPosition() * Tempo::ticksPerBeat / page.engine.tempo.samplesPerBeat (tape.getSampleRate()));
        if (x > getWidth() - 40 || x < 0)
        {
            scrollBeats = juce::jmax (0.0, scrollBeats + (x - 80.0) / pxPerBeat);
            updateScroll();
            page.refreshAll();
        }
    }

private:
    MidiPage& page;
    enum DragMode { none, locating, move, trimStart, trimEnd } dragMode = none;
    int dragTrack = -1;
    Midi::Clip original;
    double downTick = 0.0;
    bool dragNew = false;
    juce::int64 newStart = 0, newEnd = 0;
    bool dropping = false;
    juce::Point<int> dropAt;
    bool altCopy = false;

    juce::int64 dropTick (float x) const
    {
        const double t = juce::jmax (0.0, tickForX (x));
        return page.snapOn ? floorToGrid (t) : (juce::int64) t;
    }

    int rowOf (int track) const { return page.midi.usedSlots().indexOf (track); }

    juce::int64 floorToGrid (double ticks) const
    {
        const double g = juce::jmax (1.0, page.snapTicks());
        return (juce::int64) (std::floor (ticks / g) * g);
    }

    int clipAt (int track, float x) const
    {
        const auto& clips = page.midi.getClips (track);
        for (int c = (int) clips.size() - 1; c >= 0; --c)   // the last drawn is on top
            if (x >= xForTick ((double) clips[(size_t) c].start) && x <= xForTick ((double) clips[(size_t) c].end()))
                return c;
        return -1;
    }

    void locateTo (float x)
    {
        const auto ticks = juce::jmax (0.0, tickForX (x));
        const auto snapped = page.snapOn ? (double) page.snap (ticks) : ticks;
        page.engine.tape.locate ((juce::int64) (snapped / Tempo::ticksPerBeat * page.engine.tempo.samplesPerBeat (page.engine.tape.getSampleRate())));
        repaint();
    }

    void clipMenu (int track, int clip)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Rename");
        menu.addItem (2, "Duplicate (after it)");
        menu.addItem (3, "Delete");
        menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<Timeline> (this), track, clip] (int r)
        {
            if (safe == nullptr || r == 0)
                return;
            auto& pg = safe->page;
            if (r == 3)
            {
                safe->deleteSelected();
                return;
            }
            if (r == 2)
            {
                int index = clip;
                pg.changeClips (track, [&] (std::vector<Midi::Clip>& clips)
                {
                    if (! juce::isPositiveAndBelow (clip, (int) clips.size()))
                        return;
                    auto copy = clips[(size_t) clip];
                    copy.start = copy.end();
                    clips.push_back (copy);
                    index = (int) clips.size() - 1;
                });
                pg.select (track, index);
                return;
            }
            auto* w = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", pg.midi.getClips (track)[(size_t) clip].name);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([&pg, w, track, clip] (int ok)
            {
                if (ok == 1)
                {
                    const auto text = w->getTextEditorContents ("name").trim();
                    pg.changeClips (track, [&] (std::vector<Midi::Clip>& clips)
                    {
                        if (juce::isPositiveAndBelow (clip, (int) clips.size()) && text.isNotEmpty())
                            clips[(size_t) clip].name = text;
                    });
                    pg.refreshAll();
                }
            }), true);
        });
    }

    void paintGrid (juce::Graphics& g, int top, int bottom)
    {
        const int w = getWidth();
        if (page.barsGrid)
        {
            const int perBar = page.engine.tempo.getBeatsPerBar();
            const int firstBeat = (int) std::floor (scrollBeats);
            const int lastBeat = (int) std::ceil (scrollBeats + w / pxPerBeat);
            const double fine = page.snapTicks() / Tempo::ticksPerBeat;   // the snap, in beats
            for (int b = firstBeat; b <= lastBeat; ++b)
            {
                const float x = (float) ((b - scrollBeats) * pxPerBeat);
                if (fine < 1.0 && pxPerBeat * fine >= 8.0)
                    for (double f = fine; f < 0.999; f += fine)
                    {
                        g.setColour (lineFine);
                        g.fillRect (x + (float) (f * pxPerBeat), (float) top, 1.0f, (float) (bottom - top));
                    }
                const bool barLine = b % perBar == 0;
                if (! barLine && pxPerBeat < 6.0)
                    continue;
                g.setColour (barLine ? lineBar : lineBeat);
                g.fillRect (x, (float) top, 1.0f, (float) (bottom - top));
            }
        }
        else
        {
            const double pxPerSecond = pxPerBeat * page.ticksPerSecond() / Tempo::ticksPerBeat;
            const double step = pxPerSecond >= 40.0 ? 1.0 : pxPerSecond >= 8.0 ? 5.0 : 30.0;
            const double firstSec = std::floor (scrollBeats * Tempo::ticksPerBeat / page.ticksPerSecond() / step) * step;
            for (double s = firstSec; ; s += step)
            {
                const float x = (float) xForTick (s * page.ticksPerSecond());
                if (x > w)
                    break;
                g.setColour (std::fmod (s, 60.0) == 0.0 ? lineBar : lineBeat);
                g.fillRect (x, (float) top, 1.0f, (float) (bottom - top));
            }
        }
    }

    void paintRuler (juce::Graphics& g)
    {
        const int w = getWidth();
        g.setColour (rulerBg);
        g.fillRect (0, 0, w, rulerH);
        g.setColour (juce::Colour (0xff1d1f22));
        g.fillRect (0, rulerH - 1, w, 1);
        g.setColour (textDim);
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        if (page.barsGrid)
        {
            const int perBar = page.engine.tempo.getBeatsPerBar();
            const double pxPerBar = pxPerBeat * perBar;
            const int every = pxPerBar >= 40.0 ? 1 : pxPerBar >= 10.0 ? 4 : 16;
            const int firstBar = (int) std::floor (scrollBeats / perBar);
            for (int bar = firstBar; ; ++bar)
            {
                const float x = (float) ((bar * perBar - scrollBeats) * pxPerBeat);
                if (x > w)
                    break;
                if (bar % every != 0)
                    continue;
                g.fillRect (x, 14.0f, 1.0f, (float) rulerH - 14.0f);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
            }
        }
        else
        {
            const double tps = page.ticksPerSecond();
            const double pxPerSecond = pxPerBeat * tps / Tempo::ticksPerBeat;
            const double step = pxPerSecond >= 40.0 ? 1.0 : pxPerSecond >= 8.0 ? 5.0 : 30.0;
            const double firstSec = std::floor (scrollBeats * Tempo::ticksPerBeat / tps / step) * step;
            for (double s = firstSec; ; s += step)
            {
                const float x = (float) xForTick (s * tps);
                if (x > w)
                    break;
                const int whole = (int) s;
                g.fillRect (x, 14.0f, 1.0f, (float) rulerH - 14.0f);
                g.drawText (juce::String::formatted ("%d:%02d", whole / 60, whole % 60), juce::Rectangle<float> (x + 3.0f, 2.0f, 50.0f, 14.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    void paintClip (juce::Graphics& g, const Midi::Clip& c, int track, int y, bool selected)
    {
        const float x0 = (float) xForTick ((double) c.start), x1 = (float) xForTick ((double) c.end());
        if (x1 < 0.0f || x0 > (float) getWidth())
            return;
        const auto colour = page.midi.getColour (track);
        const juce::Rectangle<float> r (x0, (float) y, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) page.laneH - 8.0f);
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (selected ? juce::Colours::white : colour);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);
        auto head = r.withHeight (15.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (head, 3.0f);
        g.setColour (juce::Colour (0xff141414));
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
        g.drawText (c.name, head.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);

        if (c.notes.empty())
            return;
        int lo = 127, hi = 0;
        for (auto& n : c.notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        const float top = r.getY() + 18.0f, h = r.getBottom() - 3.0f - top;
        const float rowH = juce::jmax (1.5f, juce::jmin (4.0f, h / (float) (hi - lo + 1)));
        g.setColour (colour);
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        for (auto& n : c.notes)
        {
            const float nx = (float) xForTick ((double) (c.start + n.start));
            const float nw = juce::jmax (1.5f, (float) (xForTick ((double) (c.start + n.start + n.length)) - nx));
            const float ny = hi == lo ? top + h * 0.5f : top + (float) (hi - n.pitch) / (float) (hi - lo) * (h - rowH);
            g.fillRect (nx, ny, nw, rowH);
        }
        g.restoreState();
    }
};

//==============================================================================
/** The selected clip's notes: keyboard down the left, the notes on a grid, velocities underneath. */
class MidiPage::PianoRoll : public juce::Component
{
public:
    explicit PianoRoll (MidiPage& p) : page (p)
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (zoom);
        zoom.onHorizontal = [this] (int d) { zoomAcross (d > 0 ? 1.25 : 0.8); };
        zoom.onVertical = [this] (int d) { zoomRows (d); };
    }

    void resized() override
    {
        zoom.setBounds (getLocalBounds().removeFromBottom (zoomBarH).removeFromRight (ZoomButtons::width + 6).reduced (3, 1).withTrimmedRight (3));
    }

    void zoomAcross (double factor)
    {
        const double centre = tickFor ((float) (keysW + (getWidth() - keysW) / 2));
        pxPerTick = juce::jlimit (0.005, 2.0, pxPerTick * factor);
        scrollTicks = juce::jmax (0.0, centre - (getWidth() - keysW) * 0.5 / pxPerTick);
        repaint();
    }

    /** Taller (+1) or shorter (-1) note rows, keeping the middle pitch in place. */
    void zoomRows (int direction)
    {
        static constexpr int heights[] = { 6, 8, 11, 14, 18, 24 };
        int i = 0;
        while (i < 5 && heights[i] < rowH)
            ++i;
        const int centrePitch = topPitch - visibleRows() / 2;
        rowH = heights[juce::jlimit (0, 5, i + direction)];
        topPitch = juce::jlimit (visibleRows() - 1, 127, centrePitch + visibleRows() / 2);
        repaint();
    }

    std::set<int> selection;   // note indices in the clip
    int shownTrack = -1, shownClip = -1;

    static constexpr int keysW = 46, velocityH = 84, rulerH2 = 18, zoomBarH = 28;
    int rowH = 11;   // the vertical zoom
    int areaH() const { return getHeight() - zoomBarH; }   // above the zoom buttons

    const Midi::Clip* clip() const
    {
        if (! page.midi.isUsed (page.selectedTrack))
            return nullptr;
        const auto& clips = page.midi.getClips (page.selectedTrack);
        return juce::isPositiveAndBelow (page.selectedClip, (int) clips.size()) ? &clips[(size_t) page.selectedClip] : nullptr;
    }

    void clipChanged()
    {
        if (shownTrack == page.selectedTrack && shownClip == page.selectedClip)
            return;
        shownTrack = page.selectedTrack;
        shownClip = page.selectedClip;
        selection.clear();
        selectedControl = -1;
        if (auto* c = clip())
        {
            // Fit the clip across, and centre its notes (or C3) down.
            pxPerTick = juce::jmax (0.01, (double) (getWidth() - keysW - 8) / (double) juce::jmax ((juce::int64) 1, c->length));
            scrollTicks = 0.0;
            int lo = 127, hi = 0;
            for (auto& n : c->notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
            const int centre = c->notes.empty() ? 60 : (lo + hi) / 2;
            topPitch = juce::jlimit (visibleRows() - 1, 127, centre + visibleRows() / 2);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff222528));
        const auto* c = clip();
        if (c == nullptr)
        {
            g.setColour (textDim);
            g.setFont (ConsoleLookAndFeel::uiFont (15.0f));
            g.drawText ("Select a clip to edit its notes here (double-click a lane, or draw one with the pencil)", getLocalBounds(), juce::Justification::centred, false);
            return;
        }
        const int notesBottom = areaH() - velocityH;
        // Keys and rows.
        for (int row = 0; row <= visibleRows(); ++row)
        {
            const int pitch = topPitch - row;
            if (pitch < 0)
                break;
            const int y = rulerH2 + row * rowH;
            if (y >= notesBottom)
                break;   // the keyboard stops at the lane
            g.setColour (isBlackKey (pitch) ? juce::Colour (0xff1f2225) : juce::Colour (0xff26292c));
            g.fillRect (keysW, y, getWidth() - keysW, rowH);
            g.setColour (isBlackKey (pitch) ? juce::Colour (0xff222222) : juce::Colour (0xffe8e8e8));
            g.fillRect (0, y, keysW - 1, rowH - 1);
            if (pitch % 12 == 0)
            {
                g.setColour (juce::Colour (0xff333333));
                g.setFont (ConsoleLookAndFeel::uiFont (9.0f, true));
                g.drawText (noteName (pitch), 2, y, keysW - 4, rowH, juce::Justification::centredRight, false);
                g.setColour (lineBar.withAlpha (0.6f));
                g.fillRect (keysW, y + rowH - 1, getWidth() - keysW, 1);
            }
        }
        // Grid.
        g.saveState();
        g.reduceClipRegion (keysW, 0, getWidth() - keysW, areaH());
        const double step = juce::jmax (1.0, page.snapTicks());
        const double beat = Tempo::ticksPerBeat, bar = (double) page.barTicks();
        for (double t = 0.0; t <= (double) c->length + 0.5; t += (step * pxPerTick >= 6.0 ? step : beat))
        {
            const float x = xFor (t);
            const bool isBar = std::fmod ((double) c->start + t, bar) < 0.5;
            const bool isBeat = std::fmod ((double) c->start + t, beat) < 0.5;
            g.setColour (isBar ? lineBar : isBeat ? lineBeat.brighter (0.15f) : lineFine.brighter (0.1f));
            g.fillRect (x, (float) rulerH2, 1.0f, (float) (notesBottom - rulerH2));
            if (isBar)
            {
                g.setColour (textDim);
                g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
                const auto barNumber = (c->start + (juce::int64) t) / page.barTicks() + 1;
                g.drawText (juce::String (barNumber), juce::Rectangle<float> (x + 3.0f, 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
            }
        }
        // Past the clip's end: shaded.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (xFor ((double) c->length), (float) rulerH2, (float) getWidth(), (float) (notesBottom - rulerH2));

        // Notes.
        const auto colour = page.midi.getColour (page.selectedTrack);
        for (int i = 0; i < (int) c->notes.size(); ++i)
        {
            const auto r = noteRect (c->notes[(size_t) i]);
            if (r.getBottom() < rulerH2 || r.getY() > notesBottom)
                continue;
            const bool sel = selection.count (i) > 0;
            g.setColour (sel ? colour.brighter (0.6f) : colour.withMultipliedSaturation (0.9f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (sel ? juce::Colours::white : colour.darker (0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
        }
        // The lane under the notes: velocities, a controller or program changes.
        g.setColour (juce::Colour (0xff1d1f22));
        g.fillRect (keysW, notesBottom, getWidth() - keysW, velocityH);
        g.setColour (juce::Colour (0xff111111));
        g.fillRect (keysW, notesBottom, getWidth() - keysW, 1);
        if (laneMode < 0)
            for (int i = 0; i < (int) c->notes.size(); ++i)
            {
                const auto& n = c->notes[(size_t) i];
                const float x = xFor ((double) n.start);
                const float h = (float) (velocityH - 14) * (float) n.velocity / 127.0f;
                g.setColour (selection.count (i) > 0 ? colour.brighter (0.6f) : colour);
                g.fillRect (x, (float) areaH() - 4.0f - h, 4.0f, h);
                g.fillEllipse (x - 1.0f, (float) areaH() - 7.0f - h, 6.0f, 6.0f);
            }
        else
            paintControls (g, *c, colour);
        g.restoreState();
        // The lane's name, as a button: click for Velocity, Program change or any CC.
        g.setColour (juce::Colour (0xff1d1f22));
        g.fillRect (0, notesBottom, keysW, velocityH);
        const auto laneButton = juce::Rectangle<float> (3.0f, (float) notesBottom + 5.0f, (float) keysW - 6.0f, 30.0f);
        g.setColour (juce::Colour (0xff4a4e53));
        g.fillRoundedRectangle (laneButton, 3.0f);
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
        g.drawFittedText (laneName (laneMode) + "\n" + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")), laneButton.toNearestInt(),
                          juce::Justification::centred, 2, 0.7f);
        g.setColour (textDim);
        g.drawText ("BAR", 0, 2, keysW - 4, 14, juce::Justification::centredRight, false);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (clip() == nullptr)
            return;
        if (e.mods.isCommandDown() || e.mods.isAltDown())
        {
            const double at = tickFor (e.position.x);
            pxPerTick = juce::jlimit (0.005, 2.0, pxPerTick * (wheel.deltaY > 0 ? 1.25 : 0.8));
            scrollTicks = juce::jmax (0.0, at - (e.position.x - keysW) / pxPerTick);
        }
        else if (e.mods.isShiftDown())
            scrollTicks = juce::jmax (0.0, scrollTicks - wheel.deltaY * 200.0 / pxPerTick);
        else
            topPitch = juce::jlimit (visibleRows() - 1, 127, topPitch + (wheel.deltaY > 0 ? 3 : -3));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        drag = Drag::none;
        const auto* c = clip();
        if (c == nullptr)
            return;
        const int notesBottom = areaH() - velocityH;
        if (e.position.x < keysW)
        {
            if (e.position.y >= notesBottom && e.position.y < areaH())
                showLaneMenu();
            return;
        }
        if (e.position.y >= notesBottom && laneMode >= 0)
        {
            controlMouseDown (e);
            return;
        }
        if (e.position.y >= notesBottom)
        {
            // Velocity: the bar nearest the mouse (all the selected notes if it is one of them).
            int best = -1;
            float bestD = 8.0f;
            for (int i = 0; i < (int) c->notes.size(); ++i)
            {
                const float d = std::abs (xFor ((double) c->notes[(size_t) i].start) + 2.0f - e.position.x);
                if (d < bestD) { bestD = d; best = i; }
            }
            if (best >= 0)
            {
                if (selection.count (best) == 0)
                    selection = { best };
                drag = Drag::velocity;
                setVelocity (e.position.y);
            }
            return;
        }

        const int hit = noteAt (e.position);
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu())
            {
                selection = { hit };
                deleteSelected();
                return;
            }
            if (e.mods.isShiftDown())
            {
                if (selection.count (hit) > 0) selection.erase (hit); else selection.insert (hit);
            }
            else if (selection.count (hit) == 0)
                selection = { hit };
            const auto r = noteRect (c->notes[(size_t) hit]);
            drag = e.position.x > r.getRight() - 5.0f ? Drag::resize : Drag::move;
            original = c->notes;
            downTick = tickFor (e.position.x);
            downPitch = pitchFor (e.position.y);
            anchor = hit;
            repaint();
            return;
        }

        if (! e.mods.isShiftDown())
            selection.clear();
        if (page.tool == Tool::pencil && ! e.mods.isPopupMenu())
            addNote (e.position, true);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (page.tool == Tool::pointer && clip() != nullptr && e.position.x >= keysW && e.position.y < areaH() - velocityH && noteAt (e.position) < 0)
            addNote (e.position, false);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::velocity)
        {
            setVelocity (e.position.y);
            return;
        }
        if (drag == Drag::control)
        {
            controlDrag (e);
            return;
        }
        if (drag == Drag::none || clip() == nullptr)
            return;
        const double tick = tickFor (e.position.x);
        const auto& anchorNote = original[(size_t) anchor];
        edit ([&] (Midi::Clip& c)
        {
            if (drag == Drag::move)
            {
                const auto dt = page.snap ((double) anchorNote.start + (tick - downTick)) - anchorNote.start;
                const int dp = pitchFor (e.position.y) - downPitch;
                for (int i : selection)
                    if (juce::isPositiveAndBelow (i, (int) original.size()))
                    {
                        auto& n = c.notes[(size_t) i];
                        n.start = juce::jlimit ((juce::int64) 0, c.length - 1, original[(size_t) i].start + dt);
                        n.pitch = juce::jlimit (0, 127, original[(size_t) i].pitch + dp);
                    }
            }
            else
            {
                const auto end = page.snap ((double) (anchorNote.start + anchorNote.length) + (tick - downTick));
                const auto dl = juce::jmax ((juce::int64) 1, end - anchorNote.start) - anchorNote.length;
                for (int i : selection)
                    if (juce::isPositiveAndBelow (i, (int) original.size()))
                        c.notes[(size_t) i].length = juce::jmax ((juce::int64) 1, original[(size_t) i].length + dl);
            }
        }, false);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag == Drag::move || drag == Drag::resize)
            sortKeepingSelection();
        drag = Drag::none;
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            if (laneMode >= 0 && selectedControl >= 0)
                deleteSelectedControl();
            else
                deleteSelected();
            return true;
        }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            selectAll();
            return true;
        }
        return false;
    }

    void selectAll()
    {
        selection.clear();
        if (auto* c = clip())
            for (int i = 0; i < (int) c->notes.size(); ++i)
                selection.insert (i);
        repaint();
    }

    void deleteSelected()
    {
        if (selection.empty())
            return;
        edit ([this] (Midi::Clip& c)
        {
            std::vector<Midi::Note> kept;
            for (int i = 0; i < (int) c.notes.size(); ++i)
                if (selection.count (i) == 0)
                    kept.push_back (c.notes[(size_t) i]);
            c.notes = kept;
        }, false);
        selection.clear();
        repaint();
    }

    /** The selected notes (all of them with none selected): starts onto the snap grid. */
    void quantise()
    {
        edit ([this] (Midi::Clip& c)
        {
            for (int i = 0; i < (int) c.notes.size(); ++i)
                if (selection.empty() || selection.count (i) > 0)
                {
                    auto& n = c.notes[(size_t) i];
                    // On the song's grid, not the clip's: a clip can start off the grid.
                    n.start = juce::jlimit ((juce::int64) 0, c.length - 1, page.snapOn ? juce::jmax ((juce::int64) 0, (juce::int64) page.snap ((double) (c.start + n.start)) - c.start)
                                                                                         : n.start);
                }
        }, true);
    }

    void transpose (int semitones)
    {
        edit ([this, semitones] (Midi::Clip& c)
        {
            for (int i = 0; i < (int) c.notes.size(); ++i)
                if (selection.empty() || selection.count (i) > 0)
                    c.notes[(size_t) i].pitch = juce::jlimit (0, 127, c.notes[(size_t) i].pitch + semitones);
        }, false);
    }

    void changeVelocity (int setTo, int add)
    {
        edit ([this, setTo, add] (Midi::Clip& c)
        {
            for (int i = 0; i < (int) c.notes.size(); ++i)
                if (selection.empty() || selection.count (i) > 0)
                {
                    auto& v = c.notes[(size_t) i].velocity;
                    v = juce::jlimit (1, 127, setTo > 0 ? setTo : v + add);
                }
        }, false);
    }

    juce::String selectionText() const
    {
        const auto* c = clip();
        if (c == nullptr)
            return {};
        if (selection.empty())
            return juce::String ((int) c->notes.size()) + " notes";
        if (selection.size() > 1)
            return juce::String ((int) selection.size()) + " notes selected";
        const auto& n = c->notes[(size_t) *selection.begin()];
        return "Note " + noteName (n.pitch) + "   vel " + juce::String (n.velocity) + "   " + juce::String ((double) n.length / Tempo::ticksPerBeat, 2) + " beats";
    }

private:
    MidiPage& page;
    ZoomButtons zoom;
    double pxPerTick = 0.1, scrollTicks = 0.0;
    int topPitch = 72;
    enum class Drag { none, move, resize, velocity, control } drag = Drag::none;

    // The controller lane (laneMode: -1 velocity, 0-127 a CC, Midi::programChange).
    int laneMode = -1;
    int selectedControl = -1;   // index into the clip's controls

    static juce::String ccName (int cc)
    {
        switch (cc)
        {
            case 1: return "Mod wheel";  case 2: return "Breath";      case 4: return "Foot";      case 7: return "Volume";
            case 10: return "Pan";       case 11: return "Expression"; case 64: return "Sustain";  case 65: return "Portamento";
            case 66: return "Sostenuto"; case 67: return "Soft pedal"; case 71: return "Resonance"; case 74: return "Brightness";
            default: return {};
        }
    }

    static juce::String laneName (int mode)
    {
        if (mode < 0) return "VEL";
        if (mode == Midi::programChange) return "PROG";
        return "CC " + juce::String (mode);
    }

    float yForValue (int v) const { return (float) areaH() - 4.0f - (float) (velocityH - 14) * (float) v / 127.0f; }
    int valueForY (float y) const { return juce::jlimit (0, 127, juce::roundToInt (((float) areaH() - 4.0f - y) / (float) (velocityH - 14) * 127.0f)); }

    void paintControls (juce::Graphics& g, const Midi::Clip& c, juce::Colour colour)
    {
        // A step line through the values (each holds until the next change), a dot on each change.
        const bool program = laneMode == Midi::programChange;
        float lastX = xFor (0.0), lastY = -1.0f;
        g.setFont (ConsoleLookAndFeel::uiFont (10.0f, true));
        for (int i = 0; i < (int) c.controls.size(); ++i)
        {
            const auto& ctl = c.controls[(size_t) i];
            if (ctl.number != laneMode)
                continue;
            const float x = xFor ((double) ctl.tick), y = yForValue (ctl.value);
            g.setColour (colour.withAlpha (0.8f));
            if (! program && lastY >= 0.0f)
            {
                g.fillRect (lastX, lastY, x - lastX, 1.5f);
                g.fillRect (x, juce::jmin (lastY, y), 1.5f, std::abs (y - lastY) + 1.0f);
            }
            g.setColour (i == selectedControl ? juce::Colours::white : colour);
            if (program)
            {
                g.fillRect (x, y, 2.0f, (float) areaH() - 4.0f - y);
                g.drawText ("P" + juce::String (ctl.value + 1), juce::Rectangle<float> (x + 5.0f, y - 2.0f, 40.0f, 12.0f), juce::Justification::centredLeft, false);
            }
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            lastX = x;
            lastY = y;
        }
        if (! program && lastY >= 0.0f)
        {
            g.setColour (colour.withAlpha (0.8f));
            g.fillRect (lastX, lastY, xFor ((double) c.length) - lastX, 1.5f);
        }
    }

    int controlAt (float x) const
    {
        if (auto* c = clip())
            for (int i = (int) c->controls.size() - 1; i >= 0; --i)
                if (c->controls[(size_t) i].number == laneMode && std::abs (xFor ((double) c->controls[(size_t) i].tick) - x) <= 5.0f)
                    return i;
        return -1;
    }

    juce::int64 laneTick (float x) const
    {
        const auto* c = clip();
        const double t = juce::jmax (0.0, tickFor (x));
        const auto snapped = page.snapOn ? (juce::int64) page.snap ((double) c->start + t) - c->start : (juce::int64) t;
        return juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, c->length - 1), snapped);
    }

    /** Puts a value at a tick on the current lane (replacing one already on that tick). Returns its index. */
    int setControlAt (juce::int64 tick, int value, int replacing = -1)
    {
        const Midi::Control ctl { tick, laneMode, value };
        const int mode = laneMode;
        edit ([&] (Midi::Clip& c)
        {
            if (juce::isPositiveAndBelow (replacing, (int) c.controls.size()))
                c.controls.erase (c.controls.begin() + replacing);
            c.controls.erase (std::remove_if (c.controls.begin(), c.controls.end(),
                                              [&] (const Midi::Control& o) { return o.number == mode && o.tick == tick; }),
                              c.controls.end());
            c.controls.push_back (ctl);
        }, false);
        if (auto* c = clip())   // setClips sorted them
            for (int i = 0; i < (int) c->controls.size(); ++i)
                if (c->controls[(size_t) i] == ctl)
                    return i;
        return -1;
    }

    void controlMouseDown (const juce::MouseEvent& e)
    {
        const int hit = controlAt (e.position.x);
        if (e.mods.isPopupMenu())
        {
            if (hit >= 0)
            {
                selectedControl = hit;
                deleteSelectedControl();
            }
            return;
        }
        drag = Drag::control;
        if (hit >= 0 && page.tool == Tool::pointer)
            selectedControl = hit;   // drag it
        else
            selectedControl = setControlAt (laneTick (e.position.x), valueForY (e.position.y));   // a new point (the pencil draws on)
        repaint();
    }

    void controlDrag (const juce::MouseEvent& e)
    {
        const auto* c = clip();
        if (c == nullptr)
            return;
        if (page.tool == Tool::pencil)
            selectedControl = setControlAt (laneTick (e.position.x), valueForY (e.position.y));
        else if (juce::isPositiveAndBelow (selectedControl, (int) c->controls.size()))
            selectedControl = setControlAt (laneTick (e.position.x), valueForY (e.position.y), selectedControl);   // time on the grid, value up and down
    }

    void deleteSelectedControl()
    {
        const int index = selectedControl;
        selectedControl = -1;
        edit ([index] (Midi::Clip& c) { if (juce::isPositiveAndBelow (index, (int) c.controls.size())) c.controls.erase (c.controls.begin() + index); }, false);
    }

    void showLaneMenu()
    {
        // The lanes the clip already has something on are marked with a dot.
        std::set<int> used;
        if (auto* c = clip())
            for (auto& ctl : c->controls)
                used.insert (ctl.number);
        const auto label = [&] (int mode)
        {
            auto text = mode == Midi::programChange ? juce::String ("Program change")
                                                    : "CC " + juce::String (mode) + (ccName (mode).isNotEmpty() ? "  " + ccName (mode) : juce::String());
            return used.count (mode) > 0 ? text + juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\xa2")) : text;
        };
        juce::PopupMenu menu, other;
        menu.addItem (1000, "Velocity", true, laneMode < 0);
        menu.addItem (1001 + Midi::programChange, label (Midi::programChange), true, laneMode == Midi::programChange);
        menu.addSeparator();
        for (int cc : { 1, 2, 4, 7, 10, 11, 64, 71, 74 })
            menu.addItem (1001 + cc, label (cc), true, laneMode == cc);
        for (int block = 0; block < 128; block += 32)
        {
            juce::PopupMenu sub;
            for (int cc = block; cc < block + 32; ++cc)
                sub.addItem (1001 + cc, label (cc), true, laneMode == cc);
            other.addSubMenu ("CC " + juce::String (block) + "-" + juce::String (block + 31), sub);
        }
        menu.addSubMenu ("Other CC", other);
        if (laneMode >= 0)
        {
            menu.addSeparator();
            menu.addItem (1, "Clear this lane in the clip");
        }
        menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PianoRoll> (this)] (int r)
        {
            if (safe == nullptr || r == 0)
                return;
            if (r == 1)
            {
                const int mode = safe->laneMode;
                safe->edit ([mode] (Midi::Clip& c)
                {
                    c.controls.erase (std::remove_if (c.controls.begin(), c.controls.end(), [mode] (const Midi::Control& o) { return o.number == mode; }),
                                      c.controls.end());
                }, false);
            }
            else
                safe->laneMode = r - 1001;
            safe->selectedControl = -1;
            safe->repaint();
        });
    }
    std::vector<Midi::Note> original;
    double downTick = 0.0;
    int downPitch = 60, anchor = 0;

    int visibleRows() const { return juce::jmax (1, (areaH() - velocityH - rulerH2) / rowH); }
    float xFor (double ticks) const { return (float) (keysW + (ticks - scrollTicks) * pxPerTick); }
    double tickFor (float x) const { return (x - keysW) / pxPerTick + scrollTicks; }
    int pitchFor (float y) const { return juce::jlimit (0, 127, topPitch - (int) std::floor ((y - rulerH2) / rowH)); }

    juce::Rectangle<float> noteRect (const Midi::Note& n) const
    {
        const float x = xFor ((double) n.start);
        return { x, (float) (rulerH2 + (topPitch - n.pitch) * rowH) + 1.0f, juce::jmax (3.0f, xFor ((double) (n.start + n.length)) - x), (float) rowH - 1.0f };
    }

    int noteAt (juce::Point<float> p) const
    {
        if (auto* c = clip())
            for (int i = (int) c->notes.size() - 1; i >= 0; --i)
                if (noteRect (c->notes[(size_t) i]).contains (p))
                    return i;
        return -1;
    }

    /** Changes the shown clip (notes stay sorted when `sort`). */
    void edit (const std::function<void (Midi::Clip&)>& fn, bool sort)
    {
        const int clipIndex = page.selectedClip;
        page.changeClips (page.selectedTrack, [&] (std::vector<Midi::Clip>& clips)
        {
            if (juce::isPositiveAndBelow (clipIndex, (int) clips.size()))
                fn (clips[(size_t) clipIndex]);
        });
        if (sort)
            sortKeepingSelection();
        page.refreshAll();
    }

    /** MidiEngine::setClips sorts notes by start; keep the same notes selected afterwards. */
    void sortKeepingSelection()
    {
        const auto* c = clip();
        if (c == nullptr)
            return;
        std::vector<Midi::Note> selectedNotes;
        for (int i : selection)
            if (juce::isPositiveAndBelow (i, (int) c->notes.size()))
                selectedNotes.push_back (c->notes[(size_t) i]);
        page.changeClips (page.selectedTrack, [] (std::vector<Midi::Clip>&) {});   // setClips sorts
        selection.clear();
        if (const auto* sorted = clip())
            for (auto& n : selectedNotes)
                for (int i = 0; i < (int) sorted->notes.size(); ++i)
                    if (sorted->notes[(size_t) i] == n && selection.count (i) == 0)
                    {
                        selection.insert (i);
                        break;
                    }
        repaint();
    }

    void addNote (juce::Point<float> p, bool fromPencil)
    {
        const auto* c = clip();
        if (c == nullptr)
            return;
        Midi::Note n;
        const double g = juce::jmax (1.0, page.snapTicks());
        const double t = tickFor (p.x);
        n.start = page.snapOn ? (juce::int64) (std::floor ((c->start + t) / g) * g) - c->start : (juce::int64) t;
        n.start = juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, c->length - 1), n.start);
        n.length = juce::jmax ((juce::int64) 1, (juce::int64) g);
        n.pitch = pitchFor (p.y);
        n.velocity = 100;
        edit ([n] (Midi::Clip& cl) { cl.notes.push_back (n); }, false);
        // Select the new note (setClips sorted the notes).
        selection.clear();
        if (const auto* now = clip())
            for (int i = 0; i < (int) now->notes.size(); ++i)
                if (now->notes[(size_t) i] == n)
                    selection = { i };
        if (fromPencil)
        {
            // Dragging on from here sets its length.
            drag = Drag::resize;
            original = clip()->notes;
            anchor = selection.empty() ? 0 : *selection.begin();
            downTick = tickFor (p.x);
        }
        repaint();
    }

    void setVelocity (float y)
    {
        const int notesBottom = areaH() - velocityH;
        const int v = juce::jlimit (1, 127, juce::roundToInt ((float) (areaH() - 4 - y) / (float) (velocityH - 14) * 127.0f));
        juce::ignoreUnused (notesBottom);
        edit ([this, v] (Midi::Clip& c)
        {
            for (int i : selection)
                if (juce::isPositiveAndBelow (i, (int) c.notes.size()))
                    c.notes[(size_t) i].velocity = v;
        }, false);
    }
};

//==============================================================================
/** Left of the piano roll, under the track headers: the clip's name, what is selected, and the note tools. */
class MidiPage::EditorPanel : public juce::Component
{
public:
    EditorPanel (MidiPage& p, PianoRoll& r) : page (p), roll (r)
    {
        quantise.setTooltip ("Move the selected notes (all of them, with none selected) onto the snap grid");
        quantise.onClick = [this] { roll.quantise(); };
        transpose.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "Up a semitone");
            m.addItem (2, "Down a semitone");
            m.addItem (3, "Up an octave");
            m.addItem (4, "Down an octave");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&transpose), [this] (int r)
            {
                const int amounts[] = { 0, 1, -1, 12, -12 };
                if (r > 0)
                    roll.transpose (amounts[r]);
            });
        };
        velocity.onClick = [this]
        {
            juce::PopupMenu m;
            for (int v : { 127, 100, 80, 64, 40 })
                m.addItem (v, "Set to " + juce::String (v));
            m.addSeparator();
            m.addItem (1000 + 10, "Louder (+10)");
            m.addItem (1000 - 10, "Softer (-10)");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&velocity), [this] (int r)
            {
                if (r >= 900)
                    roll.changeVelocity (0, r - 1000);
                else if (r > 0)
                    roll.changeVelocity (r, 0);
            });
        };
        selectAll.onClick = [this] { roll.selectAll(); roll.grabKeyboardFocus(); };
        remove.onClick = [this] { roll.deleteSelected(); };
        for (auto* b : { &quantise, &transpose, &velocity, &selectAll, &remove })
            addAndMakeVisible (b);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10).withTrimmedTop (48);
        auto row = r.removeFromTop (24);
        quantise.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
        row.removeFromLeft (4);
        transpose.setBounds (row);
        r.removeFromTop (6);
        row = r.removeFromTop (24);
        velocity.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
        row.removeFromLeft (4);
        selectAll.setBounds (row);
        r.removeFromTop (6);
        remove.setBounds (r.removeFromTop (24).removeFromLeft (r.getWidth() / 2 - 2));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (headerBg);
        g.setColour (juce::Colour (0xff1d1f22));
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
        auto r = getLocalBounds().reduced (10);
        const auto* c = roll.clip();
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
        g.drawText (c != nullptr ? "Editor: " + page.midi.getName (page.selectedTrack) + "  |  " + c->name : juce::String ("Editor"),
                    r.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
        g.drawText (c != nullptr ? roll.selectionText() : juce::String ("No clip selected"), r.removeFromTop (20), juce::Justification::centredLeft, true);
        auto help = getLocalBounds().reduced (10).withTrimmedTop (170);
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
        g.drawFittedText ("Pointer: click a note to select (Shift adds), drag to move, drag its right end to resize, "
                          "double-click to add, right-click or Delete to remove. Drag the velocity bars. Click VEL (left of the lane) for CC and "
                          "program change lanes: click or drag to add and move points, right-click to remove. "
                          "Pencil: click and drag to draw. Ctrl + wheel zooms, Shift + wheel scrolls.",
                          help, juce::Justification::topLeft, 8, 0.9f);
    }

private:
    MidiPage& page;
    PianoRoll& roll;
    juce::TextButton quantise { "Quantise" }, transpose { juce::CharPointer_UTF8 ("Transpose \xe2\x96\xbe") },
                     velocity { juce::CharPointer_UTF8 ("Velocity \xe2\x96\xbe") }, selectAll { "Select all" }, remove { "Delete" };
};

//==============================================================================
MidiPage::MidiPage (AudioEngine& e, PluginContext& c)
    : engine (e), midi (e.midi), context (c)
{
    toolbar = std::make_unique<Toolbar> (*this);
    timeline = std::make_unique<Timeline> (*this);
    roll = std::make_unique<PianoRoll> (*this);
    editorPanel = std::make_unique<EditorPanel> (*this, *roll);
    addAndMakeVisible (*toolbar);
    addAndMakeVisible (headerHolder);
    addAndMakeVisible (*timeline);
    addAndMakeVisible (*roll);
    addAndMakeVisible (*editorPanel);
    addAndMakeVisible (vScroll);
    vScroll.setAutoHide (false);
    vWatch.moved = [this] (double start) { timeline->scrollY = (int) start; layoutHeaders(); timeline->repaint(); };
    vScroll.addListener (&vWatch);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void MidiPage::zoomLanes (int direction)
{
    static constexpr int heights[] = { 34, 58, 82, 110, 140, 180 };
    int i = 0;
    while (i < 5 && heights[i] < laneH)
        ++i;
    laneH = heights[juce::jlimit (0, 5, i + direction)];
    layoutHeaders();
    refreshAll();
}

MidiPage::~MidiPage()
{
    vScroll.removeListener (&vWatch);
}

void MidiPage::visibilityChanged()
{
    if (isVisible())
        refreshAll();
}

void MidiPage::refreshAll()
{
    repaint (0, toolbarH, headerW, rulerH);   // the track count
    for (auto* h : headers)
        h->refresh();
    timeline->updateScroll();
    timeline->repaint();
    roll->clipChanged();
    roll->repaint();
    editorPanel->repaint();
}

void MidiPage::rebuildHeaders()
{
    shownSlots = midi.usedSlots();
    headers.clear();
    for (int k : shownSlots)
        headerHolder.addAndMakeVisible (headers.add (new TrackHeader (*this, k)));
    if (! midi.isUsed (selectedTrack))
    {
        selectedTrack = shownSlots.isEmpty() ? -1 : shownSlots.getFirst();
        selectedClip = -1;
    }
    layoutHeaders();
}

void MidiPage::layoutHeaders()
{
    const int lanesH = timeline->areaHeight() - rulerH;
    const int total = headers.size() * laneH;
    vScroll.setRangeLimits (0.0, juce::jmax (total, lanesH), juce::dontSendNotification);
    vScroll.setCurrentRange (timeline->scrollY, lanesH, juce::dontSendNotification);
    timeline->scrollY = juce::jlimit (0, juce::jmax (0, total - lanesH), timeline->scrollY);
    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, rulerH + i * laneH - timeline->scrollY, headerW, laneH);
}

void MidiPage::resized()
{
    auto r = getLocalBounds();
    toolbar->setBounds (r.removeFromTop (toolbarH));
    auto bottom = r.removeFromBottom (juce::jmin (editorH, r.getHeight() / 2));
    editorPanel->setBounds (bottom.removeFromLeft (headerW));
    roll->setBounds (bottom.withTrimmedTop (2));
    headerHolder.setBounds (r.removeFromLeft (headerW));
    vScroll.setBounds (r.removeFromRight (14));
    timeline->setBounds (r);
    layoutHeaders();
    roll->clipChanged();
}

void MidiPage::paint (juce::Graphics& g)
{
    g.fillAll (pageBg);
    // The header column's ruler corner.
    g.setColour (rulerBg);
    g.fillRect (0, toolbarH, headerW, rulerH);
    g.setColour (textDim);
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    g.drawText ("TRACKS", juce::Rectangle<int> (12, toolbarH, headerW - 24, rulerH), juce::Justification::centredLeft, false);
    g.drawText (juce::String (midi.usedSlots().size()) + " / " + juce::String (Config::numMidiTracks),
                juce::Rectangle<int> (12, toolbarH, headerW - 24, rulerH), juce::Justification::centredRight, false);
    g.setColour (juce::Colour (0xff111111));
    g.fillRect (0, roll->getY() - 2, getWidth(), 2);
}

bool MidiPage::keyPressed (const juce::KeyPress& k)
{
    return roll->hasKeyboardFocus (false) ? roll->keyPressed (k) : timeline->keyPressed (k);
}

void MidiPage::timerCallback()
{
    if (! isShowing())
        return;
    toolbar->update();
    if (midi.usedSlots() != shownSlots)
    {
        rebuildHeaders();
        refreshAll();
    }
    else if (midi.getRevision() != shownRevision)
        refreshAll();
    shownRevision = midi.getRevision();
    timeline->follow();
    // The play line moves: repaint the timeline (cheap enough at 30 Hz).
    timeline->repaint();
}
