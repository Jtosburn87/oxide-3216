#include "MidiEngine.h"
#include "TapeMachine.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace
{
    const juce::uint32 trackColours[] = { 0xff5aa9ff, 0xfff2b33d, 0xff7ad48a, 0xffe07ad4, 0xffff8a5a, 0xff6fd6d0, 0xffc9a0ff, 0xffd6d65a };

    juce::String noteList (const std::vector<Midi::Note>& notes)
    {
        juce::String s;
        for (auto& n : notes)
            s << n.start << ' ' << n.length << ' ' << n.pitch << ' ' << n.velocity << ';';
        return s;
    }

    juce::String controlList (const std::vector<Midi::Control>& controls)
    {
        juce::String s;
        for (auto& c : controls)
            s << c.tick << ' ' << c.number << ' ' << c.value << ';';
        return s;
    }

    std::vector<Midi::Control> parseControls (const juce::String& s)
    {
        std::vector<Midi::Control> controls;
        for (auto& item : juce::StringArray::fromTokens (s, ";", ""))
        {
            const auto f = juce::StringArray::fromTokens (item, " ", "");
            if (f.size() < 3)
                continue;
            controls.push_back ({ f[0].getLargeIntValue(), juce::jlimit (0, Midi::programChange, f[1].getIntValue()), juce::jlimit (0, 127, f[2].getIntValue()) });
        }
        return controls;
    }

    std::vector<Midi::Note> parseNotes (const juce::String& s)
    {
        std::vector<Midi::Note> notes;
        for (auto& item : juce::StringArray::fromTokens (s, ";", ""))
        {
            const auto f = juce::StringArray::fromTokens (item, " ", "");
            if (f.size() < 4)
                continue;
            Midi::Note n;
            n.start = f[0].getLargeIntValue();
            n.length = juce::jmax ((juce::int64) 1, f[1].getLargeIntValue());
            n.pitch = juce::jlimit (0, 127, f[2].getIntValue());
            n.velocity = juce::jlimit (1, 127, f[3].getIntValue());
            notes.push_back (n);
        }
        return notes;
    }
}

MidiEngine::MidiEngine (Tempo& t, TapeMachine& tm) : tempo (t), tape (tm)
{
    for (auto& s : slots)
    {
        s = std::make_unique<Slot>();
        s->recordHeld.fill (-1);
        s->midi.ensureSize (4096);
        s->buffer.setSize (2, Config::maxBlock);
    }
    for (auto& o : outputs)
        o.fill (0.0f);
    startTimerHz (30);
}

MidiEngine::~MidiEngine()
{
    stopTimer();
    for (auto* in : openInputs)
        in->stop();
    openInputs.clear();
    removeAllTracks();
}

//==============================================================================
int MidiEngine::addTrack (const juce::String& name)
{
    for (int k = 0; k < maxTracks; ++k)
        if (! isUsed (k))
            return initSlot (k, name);
    return -1;
}

int MidiEngine::initSlot (int k, const juce::String& name)
{
    {
        auto& s = *slots[(size_t) k];
        s.name = name.isNotEmpty() ? name : "MIDI " + juce::String (k + 1);
        s.colour = juce::Colour (trackColours[(size_t) k % std::size (trackColours)]);
        {
            const juce::ScopedLock sl (inputLock);
            s.input = allInputs;
        }
        s.clips.clear();
        s.restore.clear();
        s.recorded.clear();
        s.recordHeld.fill (-1);
        s.recordingTake = false;
        s.armed = false;
        s.muted = false;
        s.soloed = false;
        s.stereo = true;
        compile (s);
        s.used = true;
        if (selected.load() < 0 || ! isUsed (selected.load()))
            selected = k;
        updateListening();
        openNewInputs();
        changed();
        return k;
    }
}

void MidiEngine::removeTrack (int k)
{
    if (! isUsed (k))
        return;
    auto& s = *slots[(size_t) k];
    s.used = false;
    clearInstrument (k);
    s.clips.clear();
    s.restore.clear();
    s.recorded.clear();
    s.recordingTake = false;
    s.armed = false;
    compile (s);
    if (selected.load() == k)
    {
        const auto used = usedSlots();
        selected = used.isEmpty() ? -1 : used.getFirst();
    }
    updateListening();
    changed();
}

void MidiEngine::removeAllTracks()
{
    for (int k = 0; k < maxTracks; ++k)
        removeTrack (k);
    selected = -1;
}

bool MidiEngine::isUsed (int k) const noexcept
{
    return juce::isPositiveAndBelow (k, maxTracks) && slots[(size_t) k]->used.load();
}

juce::Array<int> MidiEngine::usedSlots() const
{
    juce::Array<int> a;
    for (int k = 0; k < maxTracks; ++k)
        if (isUsed (k))
            a.add (k);
    return a;
}

juce::String MidiEngine::getName (int k) const              { return isUsed (k) ? slots[(size_t) k]->name : juce::String(); }
void MidiEngine::setName (int k, const juce::String& n)     { if (isUsed (k) && n.trim().isNotEmpty()) { slots[(size_t) k]->name = n.trim(); changed(); } }
juce::Colour MidiEngine::getColour (int k) const            { return isUsed (k) ? slots[(size_t) k]->colour : juce::Colours::grey; }
void MidiEngine::setColour (int k, juce::Colour c)          { if (isUsed (k)) { slots[(size_t) k]->colour = c; changed(); } }

void MidiEngine::setArmed (int k, bool on)   { if (isUsed (k)) { slots[(size_t) k]->armed = on; updateListening(); changed(); } }
bool MidiEngine::isArmed (int k) const noexcept   { return isUsed (k) && slots[(size_t) k]->armed.load(); }
void MidiEngine::setMuted (int k, bool on)   { if (isUsed (k)) { slots[(size_t) k]->muted = on; changed(); } }
bool MidiEngine::isMuted (int k) const noexcept   { return isUsed (k) && slots[(size_t) k]->muted.load(); }
void MidiEngine::setSoloed (int k, bool on)  { if (isUsed (k)) { slots[(size_t) k]->soloed = on; changed(); } }
bool MidiEngine::isSoloed (int k) const noexcept  { return isUsed (k) && slots[(size_t) k]->soloed.load(); }

void MidiEngine::setSelected (int k)
{
    selected = isUsed (k) ? k : -1;
    updateListening();
}

void MidiEngine::updateListening()
{
    bool anyArmed = false;
    for (auto& s : slots)
        anyArmed = anyArmed || (s->used.load() && s->armed.load());
    for (int k = 0; k < maxTracks; ++k)
    {
        auto& s = *slots[(size_t) k];
        s.listens = s.used.load() && (anyArmed ? s.armed.load() : k == selected.load());
    }
}

void MidiEngine::setInput (int k, const juce::String& id)
{
    if (! isUsed (k))
        return;
    {
        const juce::ScopedLock sl (inputLock);
        slots[(size_t) k]->input = id;
    }
    openNewInputs();
    changed();
}

juce::String MidiEngine::getInput (int k) const
{
    const juce::ScopedLock sl (inputLock);
    return isUsed (k) ? slots[(size_t) k]->input : juce::String();
}

void MidiEngine::openNewInputs()
{
    if (! openDevices)
        return;
    for (auto& info : juce::MidiInput::getAvailableDevices())
    {
        if (openIds.contains (info.identifier))
            continue;
        if (auto in = juce::MidiInput::openDevice (info.identifier, this))
        {
            in->start();
            openInputs.add (in.release());
        }
        openIds.add (info.identifier);   // tried once; a device that refused is not retried every few seconds
    }
}

void MidiEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (m.isActiveSense() || m.isMidiClock() || m.isSysEx())
        return;
    const auto id = source != nullptr ? source->getIdentifier() : juce::String();
    const juce::ScopedLock sl (inputLock);
    for (auto& s : slots)
        if (s->listens.load() && (s->input == allInputs || (s->input.isNotEmpty() && s->input == id)))
            s->live.addMessageToQueue (m);
}

void MidiEngine::injectLive (int k, const juce::MidiMessage& m)
{
    if (! isUsed (k))
        return;
    auto msg = m;
    msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    slots[(size_t) k]->live.addMessageToQueue (msg);
}

//==============================================================================
void MidiEngine::setInstrument (int k, std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d)
{
    if (! isUsed (k) || p == nullptr)
        return;
    auto& s = *slots[(size_t) k];

    // Stereo out (no audio in) if the instrument takes it, else whatever it offers.
    auto layout = p->getBusesLayout();
    if (! layout.outputBuses.isEmpty())
    {
        auto wanted = layout;
        wanted.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        for (auto& in : wanted.inputBuses)
            in = juce::AudioChannelSet::disabled();
        if (p->checkBusesLayoutSupported (wanted))
            p->setBusesLayout (wanted);
        else
        {
            wanted.inputBuses = layout.inputBuses;
            if (p->checkBusesLayoutSupported (wanted))
                p->setBusesLayout (wanted);
        }
    }
    p->prepareToPlay (sampleRate, blockSize);
    p->addListener (this);
    const int channels = juce::jmax (2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, juce::jmax (blockSize, Config::maxBlock));
    const bool stereo = p->getMainBusNumOutputChannels() >= 2;

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        old = std::move (s.processor);
        s.processor = std::move (p);
        std::swap (s.buffer, buffer);
        s.stereo = stereo;
        s.sounding.fill (false);
    }
    s.description = d;
    if (old != nullptr)
    {
        old->removeListener (this);
        if (beforeDelete)
            beforeDelete (old.get());
        old->releaseResources();
    }
    changed();
}

void MidiEngine::clearInstrument (int k)
{
    if (! juce::isPositiveAndBelow (k, maxTracks))
        return;
    auto& s = *slots[(size_t) k];
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        old = std::move (s.processor);
        s.stereo = true;
    }
    s.description = {};
    if (old != nullptr)
    {
        old->removeListener (this);
        if (beforeDelete)
            beforeDelete (old.get());
        old->releaseResources();
        changed();
    }
}

juce::AudioPluginInstance* MidiEngine::getInstrument (int k) const
{
    return isUsed (k) ? slots[(size_t) k]->processor.get() : nullptr;
}

const juce::PluginDescription& MidiEngine::getDescription (int k) const
{
    static const juce::PluginDescription none;
    return isUsed (k) ? slots[(size_t) k]->description : none;
}

bool MidiEngine::isStereo (int k) const noexcept
{
    return ! isUsed (k) || slots[(size_t) k]->stereo.load();
}

//==============================================================================
const std::vector<Midi::Clip>& MidiEngine::getClips (int k) const
{
    static const std::vector<Midi::Clip> none;
    return isUsed (k) ? slots[(size_t) k]->clips : none;
}

void MidiEngine::setClips (int k, std::vector<Midi::Clip> clips)
{
    if (! isUsed (k))
        return;
    auto& s = *slots[(size_t) k];
    for (auto& c : clips)
    {
        c.start = juce::jmax ((juce::int64) 0, c.start);
        c.length = juce::jmax ((juce::int64) 1, c.length);
        std::stable_sort (c.controls.begin(), c.controls.end(), [] (const Midi::Control& a, const Midi::Control& b) { return a.tick < b.tick; });
        std::sort (c.notes.begin(), c.notes.end(), [] (const Midi::Note& a, const Midi::Note& b) { return a.start < b.start; });
    }
    s.clips = std::move (clips);
    compile (s);
    changed();
}

juce::int64 MidiEngine::songEnd() const
{
    juce::int64 end = 0;
    for (auto& s : slots)
        if (s->used.load())
            for (auto& c : s->clips)
                end = juce::jmax (end, c.end());
    return end;
}

void MidiEngine::compile (Slot& s)
{
    // Every note of every clip as an on and an off, cut off at the end of its clip. Offs sort before ons on the
    // same tick, so a repeated note sounds again.
    std::vector<Event> events;
    for (auto& c : s.clips)
    {
        for (auto& n : c.notes)
        {
            if (n.start < 0 || n.start >= c.length)
                continue;
            const auto on = c.start + n.start, off = c.start + juce::jmin (n.start + n.length, c.length);
            events.push_back ({ on, Event::noteOn, (std::uint8_t) n.pitch, (std::uint8_t) juce::jlimit (1, 127, n.velocity) });
            events.push_back ({ off, Event::noteOff, (std::uint8_t) n.pitch, 0 });
        }
        for (auto& ctl : c.controls)
        {
            if (ctl.tick < 0 || ctl.tick >= c.length)
                continue;
            if (ctl.number == Midi::programChange)
                events.push_back ({ c.start + ctl.tick, Event::program, (std::uint8_t) juce::jlimit (0, 127, ctl.value), 0 });
            else if (juce::isPositiveAndBelow (ctl.number, 128))
                events.push_back ({ c.start + ctl.tick, Event::controller, (std::uint8_t) ctl.number, (std::uint8_t) juce::jlimit (0, 127, ctl.value) });
        }
    }
    // Same tick: program, then controllers, then note offs, then note ons (a repeated note sounds again).
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
    {
        return a.tick != b.tick ? a.tick < b.tick : a.kind < b.kind;
    });
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        std::swap (s.events, events);
    }
}

std::vector<MidiEngine::Restore>& MidiEngine::restoreList (int k)
{
    return slots[(size_t) juce::jlimit (0, maxTracks - 1, k)]->restore;
}

const std::vector<MidiEngine::Restore>& MidiEngine::restoreList (int k) const
{
    return slots[(size_t) juce::jlimit (0, maxTracks - 1, k)]->restore;
}

void MidiEngine::changed()
{
    ++revision;
    if (onChange)
        onChange();
}

double MidiEngine::ticksAt (juce::int64 samples) const noexcept
{
    return tempo.ticksAt ((double) samples, tape.getSampleRate());
}

juce::int64 MidiEngine::samplesAt (juce::int64 ticks) const noexcept
{
    return (juce::int64) std::ceil (tempo.samplesAt ((double) ticks, tape.getSampleRate()) - 1.0e-6);
}

//==============================================================================
void MidiEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    blockSize = juce::jmax (maxBlockSize, Config::maxBlock);
    for (auto& s : slots)
    {
        s->live.reset (sampleRate);
        const juce::SpinLock::ScopedLockType sl (s->lock);
        if (s->processor != nullptr)
            s->processor->prepareToPlay (sampleRate, blockSize);
        s->sounding.fill (false);
        s->expectedFrom = -1;
    }
}

void MidiEngine::pushRecorded (const Midi::RecordedEvent& e) noexcept
{
    const auto scope = recordFifo.write (1);
    if (scope.blockSize1 > 0)
        recordBuffer[(size_t) scope.startIndex1] = e;
}

void MidiEngine::process (int n, juce::int64 from, double rate, bool rolling, bool recording) noexcept
{
    bool anySolo = false;
    for (auto& s : slots)
        anySolo = anySolo || (s->used.load (std::memory_order_relaxed) && s->soloed.load (std::memory_order_relaxed));

    // The block's ticks (the tempo map can change the tempo anywhere; within one block it is taken as steady).
    const double t0 = tempo.ticksAt ((double) from, tape.getSampleRate()), t1 = tempo.ticksAt ((double) from + (double) n * rate, tape.getSampleRate());
    const auto to = from + (juce::int64) std::llround ((double) n * rate);
    recordingNow.store (recording, std::memory_order_relaxed);

    for (int k = 0; k < maxTracks; ++k)
    {
        auto& s = *slots[(size_t) k];
        float* outL = outputs[(size_t) (2 * k)].data();
        float* outR = outputs[(size_t) (2 * k + 1)].data();
        if (! s.used.load (std::memory_order_relaxed))
        {
            juce::FloatVectorOperations::clear (outL, n);
            juce::FloatVectorOperations::clear (outR, n);
            continue;
        }

        const juce::SpinLock::ScopedTryLockType tl (s.lock);
        if (! tl.isLocked())
        {
            juce::FloatVectorOperations::clear (outL, n);   // mid-change on the message thread: silent for one block
            juce::FloatVectorOperations::clear (outR, n);
            continue;
        }

        // Live input (from the keyboard, as it arrived during the last block).
        auto& midi = s.midi;
        midi.clear();
        s.live.removeNextBlockOfMessages (midi, n);
        if (! s.listens.load (std::memory_order_relaxed))
            midi.clear();
        else if (recording && rolling && s.armed.load (std::memory_order_relaxed))
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                const auto tick = (juce::int64) std::floor (t0 + (t1 - t0) * meta.samplePosition / (double) n);
                using R = Midi::RecordedEvent;
                if (m.isNoteOnOrOff())
                    pushRecorded ({ k, tick, R::note, (std::uint8_t) m.getNoteNumber(), (std::uint8_t) (m.isNoteOn() ? juce::jmax (1, (int) m.getVelocity()) : 0) });
                else if (m.isController())
                    pushRecorded ({ k, tick, R::controller, (std::uint8_t) m.getControllerNumber(), (std::uint8_t) m.getControllerValue() });
                else if (m.isProgramChange())
                    pushRecorded ({ k, tick, R::program, (std::uint8_t) m.getProgramChangeNumber(), 0 });
            }

        // The clips, while the tape plays. A jump (locate, loop) or a stop ends whatever they had sounding.
        const bool audible = ! s.muted.load (std::memory_order_relaxed) && ! (anySolo && ! s.soloed.load (std::memory_order_relaxed));
        const bool jumped = s.expectedFrom >= 0 && std::abs (from - s.expectedFrom) > juce::jmax ((juce::int64) 2, (juce::int64) n / 4);
        if (! rolling || ! audible || jumped)
            for (int p = 0; p < 128; ++p)
                if (s.sounding[(size_t) p])
                {
                    midi.addEvent (juce::MidiMessage::noteOff (1, p), 0);
                    s.sounding[(size_t) p] = false;
                }
        if (rolling && audible && t1 > t0)
        {
            auto it = std::lower_bound (s.events.begin(), s.events.end(), (juce::int64) std::ceil (t0),
                                        [] (const Event& e, juce::int64 tick) { return e.tick < tick; });

            // Starting (or jumping) part-way: send the last program and controller values before here first.
            if (s.expectedFrom < 0 || jumped)
            {
                std::array<int, 129> last;
                last.fill (-1);
                for (auto back = it; back != s.events.begin();)
                {
                    --back;
                    if (back->kind == Event::controller && last[back->a] < 0) last[back->a] = back->b;
                    if (back->kind == Event::program && last[128] < 0)         last[128] = back->a;
                }
                if (last[128] >= 0)
                    midi.addEvent (juce::MidiMessage::programChange (1, last[128]), 0);
                for (int c = 0; c < 128; ++c)
                    if (last[(size_t) c] >= 0)
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, c, last[(size_t) c]), 0);
            }

            for (; it != s.events.end() && (double) it->tick < t1; ++it)
            {
                const int at = juce::jlimit (0, n - 1, (int) (((double) it->tick - t0) / (t1 - t0) * (double) n));
                switch (it->kind)
                {
                    case Event::noteOn:
                        midi.addEvent (juce::MidiMessage::noteOn (1, it->a, (juce::uint8) it->b), at);
                        s.sounding[it->a] = true;
                        break;
                    case Event::noteOff:
                        if (s.sounding[it->a])
                        {
                            midi.addEvent (juce::MidiMessage::noteOff (1, it->a), at);
                            s.sounding[it->a] = false;
                        }
                        break;
                    case Event::controller: midi.addEvent (juce::MidiMessage::controllerEvent (1, it->a, it->b), at); break;
                    case Event::program:    midi.addEvent (juce::MidiMessage::programChange (1, it->a), at); break;
                }
            }
        }
        s.expectedFrom = rolling ? to : -1;

        // The instrument.
        if (s.processor == nullptr)
        {
            juce::FloatVectorOperations::clear (outL, n);
            juce::FloatVectorOperations::clear (outR, n);
            continue;
        }
        juce::AudioBuffer<float> block (s.buffer.getArrayOfWritePointers(), s.buffer.getNumChannels(), n);
        block.clear();
        s.processor->processBlock (block, midi);
        std::memcpy (outL, block.getReadPointer (0), sizeof (float) * (size_t) n);
        std::memcpy (outR, block.getReadPointer (s.stereo.load (std::memory_order_relaxed) ? 1 : 0), sizeof (float) * (size_t) n);
    }
}

//==============================================================================
void MidiEngine::finishTake (Slot& s, juce::int64 endTick)
{
    for (int p = 0; p < 128; ++p)
        if (s.recordHeld[(size_t) p] >= 0)
        {
            s.recorded.push_back ({ s.recordHeld[(size_t) p], juce::jmax ((juce::int64) 1, endTick - s.recordHeld[(size_t) p]), p, s.recordVelocity[(size_t) p] });
            s.recordHeld[(size_t) p] = -1;
        }
    s.recordingTake = false;
    if (s.recorded.empty() && s.recordedControls.empty())
        return;

    // The new clip runs from the bar of the first event to the end of the bar of the last one.
    juce::int64 first = std::numeric_limits<juce::int64>::max(), last = 0;
    for (auto& nt : s.recorded)
    {
        first = juce::jmin (first, nt.start);
        last = juce::jmax (last, nt.start + nt.length);
    }
    for (auto& ctl : s.recordedControls)
    {
        first = juce::jmin (first, ctl.tick);
        last = juce::jmax (last, ctl.tick + 1);
    }
    Midi::Clip clip;
    clip.start = (juce::int64) std::llround (tempo.barAt ((double) first).tick);
    clip.length = barsLength (tempo, clip.start, last);
    clip.name = s.name + " " + juce::String ((int) s.clips.size() + 1);
    for (auto nt : s.recorded)
    {
        nt.start -= clip.start;
        clip.notes.push_back (nt);
    }
    for (auto ctl : s.recordedControls)
    {
        ctl.tick -= clip.start;
        clip.controls.push_back (ctl);
    }
    s.recordedControls.clear();
    std::sort (clip.notes.begin(), clip.notes.end(), [] (const Midi::Note& a, const Midi::Note& b) { return a.start < b.start; });
    s.recorded.clear();
    s.clips.push_back (std::move (clip));
    compile (s);
    changed();
}

void MidiEngine::timerCallback()
{
    // What was played on armed tracks while the tape recorded.
    const auto ready = recordFifo.getNumReady();
    if (ready > 0)
    {
        const auto scope = recordFifo.read (ready);
        const auto take = [this] (int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                const auto& e = recordBuffer[(size_t) i];
                if (! isUsed (e.track))
                    continue;
                auto& s = *slots[(size_t) e.track];
                if (! s.recordingTake)
                {
                    s.recordingTake = true;
                    s.recorded.clear();
                    s.recordedControls.clear();
                    s.recordHeld.fill (-1);
                }
                if (e.kind == Midi::RecordedEvent::controller)
                {
                    s.recordedControls.push_back ({ e.tick, e.a, e.b });
                    continue;
                }
                if (e.kind == Midi::RecordedEvent::program)
                {
                    s.recordedControls.push_back ({ e.tick, Midi::programChange, e.a });
                    continue;
                }
                auto& held = s.recordHeld[e.a];
                if (held >= 0)   // a note on while held, or its note off: the held note ends here
                {
                    s.recorded.push_back ({ held, juce::jmax ((juce::int64) 1, e.tick - held), e.a, s.recordVelocity[e.a] });
                    held = -1;
                }
                if (e.b > 0)
                {
                    held = e.tick;
                    s.recordVelocity[e.a] = e.b;
                }
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    // Recording stopped (or the track was disarmed): the take becomes a clip.
    const bool tapeRecording = tape.isRecordEnabled() && tape.isPlaying();
    const auto nowTick = (juce::int64) ticksAt (tape.getPosition());
    for (auto& s : slots)
        if (s->recordingTake && (! tapeRecording || ! s->armed.load()))
            finishTake (*s, nowTick);

    // A blank tape plays on to the end of the last clip.
    tape.setPlayOnUntil (samplesAt (songEnd()));

    if (--inputCheckCountdown <= 0)
    {
        inputCheckCountdown = 60;   // every 2 s: MIDI keyboards plugged in later are opened
        openNewInputs();
    }
}

//==============================================================================
juce::var MidiEngine::toVar() const
{
    juce::Array<juce::var> tracks;
    for (int k = 0; k < maxTracks; ++k)
    {
        if (! isUsed (k))
            continue;
        auto& s = *slots[(size_t) k];
        auto* t = new juce::DynamicObject();
        t->setProperty ("slot", k);
        t->setProperty ("name", s.name);
        t->setProperty ("colour", s.colour.toString());
        t->setProperty ("input", getInput (k));
        t->setProperty ("muted", s.muted.load());
        t->setProperty ("soloed", s.soloed.load());
        if (s.processor != nullptr)
        {
            auto xml = std::make_unique<juce::XmlElement> ("INSTRUMENT");
            if (auto d = s.description.createXml())
                xml->addChildElement (d.release());
            juce::MemoryBlock state;
            s.processor->getStateInformation (state);
            xml->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
            t->setProperty ("instrument", xml->toString (juce::XmlElement::TextFormat().singleLine()));
        }
        juce::Array<juce::var> clips;
        for (auto& c : s.clips)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", c.name);
            o->setProperty ("start", c.start);
            o->setProperty ("length", c.length);
            o->setProperty ("notes", noteList (c.notes));   // "start length pitch velocity;" each
            if (! c.controls.empty())
                o->setProperty ("controls", controlList (c.controls));   // "tick number value;" each (number 128 = program)
            clips.add (juce::var (o));
        }
        t->setProperty ("clips", clips);
        juce::Array<juce::var> restore;
        for (auto& r : s.restore)
            restore.add (juce::Array<juce::var> { r.destination, (juce::int64) r.mask.words[0], (juce::int64) r.mask.words[1], r.interfaceInput, r.link });
        t->setProperty ("restore", restore);
        tracks.add (juce::var (t));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("tracks", tracks);
    root->setProperty ("selected", selected.load());
    root->setProperty ("ticks", "quarter");   // 960 a quarter note (before the tempo map: 960 a beat of the time signature)
    return juce::var (root);
}

juce::StringArray MidiEngine::fromVar (const juce::var& v,
                                       const std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, juce::String&)>& createPlugin)
{
    removeAllTracks();
    juce::StringArray missing;
    // Sessions from before the tempo map counted 960 ticks a beat of the time signature (in 6/8 an eighth note).
    const double scale = v.hasProperty ("ticks") ? 1.0 : 4.0 / (double) tempo.getBeatUnit();
    const auto toQuarters = [scale] (juce::int64 t) { return scale == 1.0 ? t : (juce::int64) std::llround ((double) t * scale); };
    if (auto* tracks = v["tracks"].getArray())
        for (auto& t : *tracks)
        {
            const int k = t["slot"];
            if (! juce::isPositiveAndBelow (k, maxTracks) || isUsed (k))
                continue;
            initSlot (k, t["name"].toString());   // exactly this slot: the patch bay's cables point at it
            auto& s = *slots[(size_t) k];
            if (t.hasProperty ("colour"))
                s.colour = juce::Colour::fromString (t["colour"].toString());
            {
                const juce::ScopedLock sl (inputLock);
                s.input = t.hasProperty ("input") ? t["input"].toString() : juce::String (allInputs);
            }
            s.muted = (bool) t["muted"];
            s.soloed = (bool) t["soloed"];

            std::vector<Midi::Clip> clips;
            if (auto* list = t["clips"].getArray())
                for (auto& c : *list)
                {
                    Midi::Clip clip;
                    clip.name = c["name"].toString();
                    clip.start = toQuarters ((juce::int64) c["start"]);
                    clip.length = toQuarters ((juce::int64) c["length"]);
                    clip.notes = parseNotes (c["notes"].toString());
                    clip.controls = parseControls (c["controls"].toString());
                    if (scale != 1.0)
                    {
                        for (auto& n : clip.notes)
                        {
                            n.start = toQuarters (n.start);
                            n.length = juce::jmax ((juce::int64) 1, toQuarters (n.length));
                        }
                        for (auto& ctl : clip.controls)
                            ctl.tick = toQuarters (ctl.tick);
                    }
                    clips.push_back (std::move (clip));
                }
            setClips (k, std::move (clips));

            if (auto* list = t["restore"].getArray())
                for (auto& r : *list)
                    if (r.isArray() && r.size() == 5)
                        s.restore.push_back ({ (int) r[0], PatchMask ((std::uint64_t) (juce::int64) r[1], (std::uint64_t) (juce::int64) r[2]), (int) r[3], (int) r[4] });

            if (createPlugin && t.hasProperty ("instrument"))
                if (auto xml = juce::parseXML (t["instrument"].toString()))
                {
                    juce::PluginDescription d;
                    if (auto* desc = xml->getChildByName ("PLUGIN"); desc != nullptr && d.loadFromXml (*desc))
                    {
                        juce::String error;
                        if (auto p = createPlugin (d, error))
                        {
                            if (auto* state = xml->getChildByName ("STATE"))
                            {
                                juce::MemoryBlock mb;
                                if (mb.fromBase64Encoding (state->getAllSubText()))
                                    p->setStateInformation (mb.getData(), (int) mb.getSize());
                            }
                            setInstrument (k, std::move (p), d);
                        }
                        else
                            missing.add (d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String())
                                         + " on " + s.name);
                    }
                }
        }
    const int sel = v["selected"];
    setSelected (isUsed (sel) ? sel : (usedSlots().isEmpty() ? -1 : usedSlots().getFirst()));
    changed();
    return missing;
}

//==============================================================================
juce::int64 MidiEngine::barsLength (const Tempo& tempo, juce::int64 start, juce::int64 contentEnd)
{
    const auto lastBar = tempo.barAt ((double) juce::jmax (start, contentEnd - 1));
    return juce::jmax ((juce::int64) 1, (juce::int64) std::llround (lastBar.end()) - start);
}

namespace
{
    bool readSmf (const juce::File& file, juce::MidiFile& mf, juce::String& error)
    {
        juce::FileInputStream in (file);
        if (in.openedOk() && mf.readFrom (in))
            return true;
        error = file.getFileName() + " is not a MIDI file Oxide can read.";
        return false;
    }
}

Midi::Clip MidiEngine::readMidiFile (const juce::File& file, const Tempo& tempo, juce::String& error)
{
    Midi::Clip clip;
    clip.name = file.getFileNameWithoutExtension();
    juce::MidiFile mf;
    if (! readSmf (file, mf, error))
        return clip;

    // File ticks -> ours (960 a quarter note). SMPTE files go through seconds, on the tempo (map) in force.
    const short format = mf.getTimeFormat();
    const bool smpte = format <= 0;
    if (smpte)
        mf.convertTimestampTicksToSeconds();
    const auto toTicks = [&] (double t) { return (juce::int64) std::llround (smpte ? tempo.ticksAtSeconds (t) : t * Tempo::ticksPerQuarter / (double) format); };

    juce::MidiMessageSequence all;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        all.addSequence (*mf.getTrack (t), 0.0);
    all.updateMatchedPairs();

    juce::int64 last = 0;
    for (int i = 0; i < all.getNumEvents(); ++i)
    {
        const auto* e = all.getEventPointer (i);
        if (e->message.isController() || e->message.isProgramChange())
        {
            const auto tick = toTicks (e->message.getTimeStamp());
            if (e->message.isController())
                clip.controls.push_back ({ tick, e->message.getControllerNumber(), e->message.getControllerValue() });
            else
                clip.controls.push_back ({ tick, Midi::programChange, e->message.getProgramChangeNumber() });
            last = juce::jmax (last, tick + 1);
            continue;
        }
        if (! e->message.isNoteOn())
            continue;
        Midi::Note n;
        n.start = toTicks (e->message.getTimeStamp());
        const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : e->message.getTimeStamp() + (smpte ? 0.1 : format / 4.0);
        n.length = juce::jmax ((juce::int64) 1, toTicks (off) - n.start);
        n.pitch = e->message.getNoteNumber();
        n.velocity = juce::jlimit (1, 127, (int) e->message.getVelocity());
        clip.notes.push_back (n);
        last = juce::jmax (last, n.start + n.length);
    }
    if (clip.notes.empty() && clip.controls.empty())
    {
        error = file.getFileName() + " has no notes in it.";
        return clip;
    }
    std::sort (clip.notes.begin(), clip.notes.end(), [] (const Midi::Note& a, const Midi::Note& b) { return a.start < b.start; });
    clip.length = barsLength (tempo, 0, last);
    return clip;
}

std::vector<Tempo::Change> MidiEngine::readTempoMap (const juce::File& file, juce::String& error)
{
    juce::MidiFile mf;
    if (! readSmf (file, mf, error))
        return {};
    const short format = mf.getTimeFormat();
    if (format <= 0)
    {
        error = file.getFileName() + " counts time in SMPTE frames: it has no tempo map in bars.";
        return {};
    }

    juce::MidiMessageSequence meta;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (const auto* e : *mf.getTrack (t))
            if (e->message.isTempoMetaEvent() || e->message.isTimeSignatureMetaEvent())
                meta.addEvent (e->message);
    meta.sort();
    if (meta.getNumEvents() == 0)
    {
        error = file.getFileName() + " has no tempo map in it.";
        return {};
    }

    // Every change carries the full state (a MIDI file changes tempo and signature separately). Before the first
    // of either, MIDI files mean 120 and 4/4.
    std::vector<Tempo::Change> map;
    Tempo::Change now;
    if (meta.getEventTime (0) > 0.0)
        map.push_back (now);   // 120 4/4 from the start
    for (const auto* e : meta)
    {
        now.tick = e->message.getTimeStamp() * Tempo::ticksPerQuarter / (double) format;
        if (e->message.isTempoMetaEvent())
        {
            const double secondsPerQuarter = e->message.getTempoSecondsPerQuarterNote();
            if (secondsPerQuarter > 0.0)
                now.bpm = 60.0 / secondsPerQuarter;
        }
        else
            e->message.getTimeSignatureInfo (now.beats, now.unit);
        if (! map.empty() && std::abs (map.back().tick - now.tick) < 0.5)
            map.back() = now;   // tempo and signature at one tick: one change
        else
            map.push_back (now);
    }
    return map;
}
