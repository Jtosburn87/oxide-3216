#include "Tempo.h"
#include <algorithm>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

Tempo::Tempo (ParamSet& params) : clickEq (params, "click", 1)
{
    bpm = &params.add ("tempo.bpm", "Tempo", minBpm, maxBpm, 120.0f);
    beatsPerBar = &params.addStepped ("tempo.beats", "Beats a bar", 16, 3);   // 4
    beatUnit = &params.addStepped ("tempo.unit", "Beat unit", 4, 1);          // quarter notes
    clickOn = &params.addToggle ("click.on", "Click", false);
    clickLevel = &params.add ("click.level", "Click level", -40.0f, 0.0f, -12.0f);
    clickPan = &params.add ("click.pan", "Click pan", -1.0f, 1.0f, 0.0f);
    clickMain = &params.addToggle ("click.main", "Click to main", true);
    clickEqIn = &params.addToggle ("click.eq.in", "Click EQ in", true);
    for (int k = 0; k < Config::numAux; ++k)
        clickAux[(size_t) k] = &params.add ("click.aux" + juce::String (k + 1), "Click to aux " + juce::String (k + 1), 0.0f, 1.0f, 0.0f);
}

void Tempo::setSignature (int beats, int unit)
{
    beatsPerBar->set ((float) (juce::jlimit (1, 16, beats) - 1));
    int index = 0;
    while (index < 3 && (2 << index) < unit)
        ++index;
    beatUnit->set ((float) index);
}

//==============================================================================
namespace
{
    int nearestUnit (int unit)
    {
        int best = 4;
        for (int u : { 2, 4, 8, 16 })
            if (std::abs (u - unit) < std::abs (best - unit))
                best = u;
        return best;
    }
}

void Tempo::setMap (std::vector<Change> newChanges)
{
    std::stable_sort (newChanges.begin(), newChanges.end(), [] (const Change& a, const Change& b) { return a.tick < b.tick; });
    std::vector<Change> clean;
    for (auto c : newChanges)
    {
        c.tick = juce::jmax (0.0, c.tick);
        c.bpm = juce::jlimit ((double) minBpm, (double) maxBpm, c.bpm);
        c.beats = juce::jlimit (1, 16, c.beats);
        c.unit = nearestUnit (c.unit);
        if (clean.empty())
            c.tick = 0.0;
        else if (std::abs (c.tick - clean.back().tick) < 0.5)
        {
            clean.back() = { clean.back().tick, c.bpm, c.beats, c.unit };   // several at one tick: the last one counts
            continue;
        }
        if (! clean.empty() && c.bpm == clean.back().bpm && c.beats == clean.back().beats && c.unit == clean.back().unit)
            continue;   // no change at all
        clean.push_back (c);
    }

    changes = clean;
    ++mapRevision;
    if (clean.empty())
    {
        mapSegments.store (nullptr, std::memory_order_release);
        return;
    }
    bpm->set ((float) clean.front().bpm);
    setSignature (clean.front().beats, clean.front().unit);

    auto segs = std::make_unique<std::vector<Segment>>();
    for (auto& c : clean)
    {
        Segment s;
        s.tick = c.tick;
        s.bpm = c.bpm;
        s.beats = c.beats;
        s.unit = c.unit;
        if (! segs->empty())
        {
            const auto& p = segs->back();
            s.seconds = p.seconds + (s.tick - p.tick) / ticksPerQuarter * 60.0 / p.bpm;
            if (s.beats == p.beats && s.unit == p.unit)
            {
                s.barTick = p.barTick;
                s.barIndex = p.barIndex;
            }
            else
            {
                // A new signature starts a bar (a change part-way through a bar ends that bar there).
                const auto b = barIn (p, s.tick);
                s.barIndex = b.number - 1 + (s.tick - b.tick > 0.5 ? 1 : 0);
                s.barTick = s.tick;
            }
        }
        segs->push_back (s);
    }
    double next = 1.0e18;
    for (int i = (int) segs->size() - 1; i >= 0; --i)
    {
        auto& s = (*segs)[(size_t) i];
        s.nextSigTick = next;
        if (i > 0 && (s.beats != (*segs)[(size_t) i - 1].beats || s.unit != (*segs)[(size_t) i - 1].unit))
            next = s.tick;
    }
    mapSegments.store (segs.get(), std::memory_order_release);
    keptMaps.push_back (std::move (segs));
}

std::vector<Tempo::Change> Tempo::getChanges() const
{
    if (! changes.empty())
        return changes;
    return { { 0.0, (double) juce::jlimit (minBpm, maxBpm, bpm->get()), getBeatsPerBar(), getBeatUnit() } };
}

juce::var Tempo::mapToVar() const
{
    juce::Array<juce::var> list;
    for (auto& c : changes)
        list.add (juce::Array<juce::var> { c.tick, c.bpm, c.beats, c.unit });
    return list;
}

void Tempo::mapFromVar (const juce::var& v)
{
    std::vector<Change> list;
    if (auto* a = v.getArray())
        for (auto& c : *a)
            if (c.isArray() && c.size() == 4)
                list.push_back ({ (double) c[0], (double) c[1], (int) c[2], (int) c[3] });
    setMap (std::move (list));
}

//==============================================================================
Tempo::Segment Tempo::constantSegment() const noexcept
{
    Segment s;
    s.bpm = (double) juce::jlimit (minBpm, maxBpm, bpm->get());
    s.beats = getBeatsPerBar();
    s.unit = getBeatUnit();
    return s;
}

int Tempo::segmentAtTick (const Segment* s, int n, double ticks) noexcept
{
    const auto* it = std::upper_bound (s, s + n, ticks, [] (double t, const Segment& seg) { return t < seg.tick; });
    return juce::jmax (0, (int) (it - s) - 1);
}

int Tempo::segmentAtSeconds (const Segment* s, int n, double seconds) noexcept
{
    const auto* it = std::upper_bound (s, s + n, seconds, [] (double t, const Segment& seg) { return t < seg.seconds; });
    return juce::jmax (0, (int) (it - s) - 1);
}

Tempo::Bar Tempo::barIn (const Segment& seg, double ticks) noexcept
{
    const double barLength = seg.beats * beatTicks (seg.unit);
    const double k = std::floor ((ticks - seg.barTick) / barLength + 1.0e-9);
    Bar b;
    b.tick = seg.barTick + k * barLength;
    b.length = juce::jmin (barLength, seg.nextSigTick - b.tick);
    b.number = seg.barIndex + (int) k + 1;
    b.beats = seg.beats;
    b.unit = seg.unit;
    return b;
}

double Tempo::secondsAt (double ticks) const noexcept
{
    return withSegments ([ticks] (const Segment* s, int n)
    {
        const auto& seg = s[segmentAtTick (s, n, ticks)];
        return seg.seconds + (ticks - seg.tick) / ticksPerQuarter * 60.0 / seg.bpm;
    });
}

double Tempo::ticksAtSeconds (double seconds) const noexcept
{
    return withSegments ([seconds] (const Segment* s, int n)
    {
        const auto& seg = s[segmentAtSeconds (s, n, seconds)];
        return seg.tick + (seconds - seg.seconds) * seg.bpm / 60.0 * ticksPerQuarter;
    });
}

double Tempo::ticksAt (double samples, double sr) const noexcept
{
    return ticksAtSeconds (samples / (sr > 0.0 ? sr : 48000.0));
}

double Tempo::samplesAt (double ticks, double sr) const noexcept
{
    return secondsAt (ticks) * (sr > 0.0 ? sr : 48000.0);
}

double Tempo::bpmAt (double ticks) const noexcept
{
    return withSegments ([ticks] (const Segment* s, int n) { return s[segmentAtTick (s, n, ticks)].bpm; });
}

Tempo::Signature Tempo::signatureAt (double ticks) const noexcept
{
    return withSegments ([ticks] (const Segment* s, int n)
    {
        const auto& seg = s[segmentAtTick (s, n, ticks)];
        return Signature { seg.beats, seg.unit };
    });
}

Tempo::Bar Tempo::barAt (double ticks) const noexcept
{
    ticks = juce::jmax (0.0, ticks);
    return withSegments ([ticks] (const Segment* s, int n) { return barIn (s[segmentAtTick (s, n, ticks)], ticks); });
}

Tempo::Position Tempo::positionAtTicks (double ticks) const noexcept
{
    const auto b = barAt (ticks);
    const double beatLength = beatTicks (b.unit);
    const double within = juce::jmax (0.0, ticks - b.tick);
    const int beat = juce::jmin (b.beats - 1, (int) std::floor (within / beatLength + 1.0e-9));
    Position p;
    p.bar = b.number;
    p.beat = beat + 1;
    p.tick = juce::jlimit (0, (int) beatLength - 1, (int) (within - beat * beatLength));
    return p;
}

juce::String Tempo::format (Position p)
{
    return juce::String::formatted ("%03d | %d | %03d", p.bar, p.beat, p.tick);
}

//==============================================================================
void Tempo::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    clickEq.prepare (sampleRate);
    envDecay = (float) std::exp (-1.0 / (0.025 * sampleRate));   // 25 ms
    attack = 0.0f;
    env = 0.0f;
}

void Tempo::trigger (bool downbeat) noexcept
{
    phase = 0.0;
    phaseInc = juce::MathConstants<double>::twoPi * (downbeat ? 1600.0 : 1000.0) / sampleRate;
    env = 1.0f;
    attack = 0.0f;
}

bool Tempo::renderClick (float* out, int n, juce::int64 from, juce::int64 to, double tapeSampleRate, bool running) noexcept
{
    juce::FloatVectorOperations::clear (out, n);

    // Beats that fall in this block, while the tape runs forwards at a believable speed (not a locate or loop jump).
    const bool smooth = running && to > from && to - from <= (juce::int64) n * 4;
    struct Hit { double at; bool downbeat; };   // at: the output sample of the beat
    std::array<Hit, 16> hits;
    int numHits = 0;
    if (smooth && clickOn->on())
    {
        // A beat belongs to the block its tape sample falls in (from <= sample < to), so none is lost or doubled.
        const double span = (double) (to - from);
        const double t0 = ticksAt ((double) from, tapeSampleRate), t1 = ticksAt ((double) to, tapeSampleRate);
        auto bar = barAt (t0);
        double k = juce::jmax (0.0, std::ceil ((t0 - bar.tick) / beatTicks (bar.unit) - 1.0e-3));
        while (numHits < (int) hits.size())
        {
            double beat = bar.tick + k * beatTicks (bar.unit);
            if (beat >= bar.end() - 0.5)
            {
                bar = barAt (bar.end() + 0.001);
                k = 0.0;
                beat = bar.tick;
            }
            if (beat > t1 + 1.0)
                break;
            const double s = samplesAt (beat, tapeSampleRate);
            if (s >= (double) to)
                break;
            if (s >= (double) from)
                hits[(size_t) numHits++] = { (s - (double) from) / span * (double) n, k == 0.0 };
            k += 1.0;
        }
    }

    if (numHits == 0 && env < 1.0e-4f)
        return false;

    const float gain = juce::Decibels::decibelsToGain (clickLevel->get());
    int nextHit = 0;
    for (int i = 0; i < n; ++i)
    {
        while (nextHit < numHits && (double) i >= hits[(size_t) nextHit].at)
            trigger (hits[(size_t) nextHit++].downbeat);
        if (env < 1.0e-4f)
            continue;
        attack = juce::jmin (1.0f, attack + 0.2f);   // 5 samples in, no click on the click
        out[i] = gain * env * attack * (float) std::sin (phase);
        phase += phaseInc;
        env *= envDecay;
    }
    // The click's EQ.
    clickEq.update();
    if (clickEqIn->on() && ! clickEq.isFlat())
        for (int i = 0; i < n; ++i)
            out[i] = clickEq.process (0, out[i]);
    return true;
}

std::pair<float, float> Tempo::clickPanGains() const noexcept
{
    const float angle = (clickPan->get() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    return { std::cos (angle), std::sin (angle) };
}
