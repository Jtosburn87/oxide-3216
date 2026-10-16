#include "Tempo.h"
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

double Tempo::samplesPerBeat (double sr) const noexcept
{
    // A beat of the time signature's unit: at 120 in 6/8 an eighth note lasts half a quarter.
    return sr * 60.0 / (double) juce::jlimit (minBpm, maxBpm, bpm->get()) * 4.0 / (double) getBeatUnit();
}

Tempo::Position Tempo::positionAt (juce::int64 samples, double sr) const noexcept
{
    const double beats = juce::jmax (0.0, (double) samples / samplesPerBeat (sr > 0.0 ? sr : 48000.0));
    const auto whole = (juce::int64) std::floor (beats);
    const int perBar = getBeatsPerBar();
    Position p;
    p.bar = (int) (whole / perBar) + 1;
    p.beat = (int) (whole % perBar) + 1;
    p.tick = juce::jlimit (0, ticksPerBeat - 1, (int) ((beats - (double) whole) * ticksPerBeat));
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
    juce::int64 nextBeat = -1;
    double spb = 1.0;
    if (smooth && clickOn->on())
    {
        spb = samplesPerBeat (tapeSampleRate);
        nextBeat = (juce::int64) std::ceil ((double) from / spb);
    }

    if (nextBeat < 0 && env < 1.0e-4f)
        return false;

    const float gain = juce::Decibels::decibelsToGain (clickLevel->get());
    const double span = (double) (to - from);
    const int perBar = getBeatsPerBar();
    for (int i = 0; i < n; ++i)
    {
        if (nextBeat >= 0)
        {
            const double at = ((double) nextBeat * spb - (double) from) / span * (double) n;   // output sample of the beat
            if (at < (double) n && (double) i >= at)
            {
                trigger (nextBeat % perBar == 0);
                ++nextBeat;
            }
        }
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
