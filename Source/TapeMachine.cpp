#include "TapeMachine.h"

namespace
{
    constexpr double allocateAheadSeconds = 20.0;
    constexpr float saturationBias = 0.12f;   // asymmetry of the tape curve (from the prototype)
}

TapeMachine::TapeMachine (ParamSet& params)
    : maxChunks ((size_t) std::ceil (Config::maxSampleRate * 60.0 * Config::maxTapeMinutes / chunkSize))
{
    recordLevel = &params.add ("tape.level", "Record level", -12.0f, 18.0f, 0.0f);
    speed = &params.addStepped ("tape.speed", "Tape speed", 3, 1);   // 15 ips
    wowFlutter = &params.addToggle ("tape.wf", "Wow and flutter", true);
    for (int t = 0; t < numTracks; ++t)
    {
        arm[(size_t) t] = &params.addToggle ("tape.arm" + juce::String (t + 1), "Arm " + juce::String (t + 1), false);
        hiss[(size_t) t].state = 0x9e3779b9u * (unsigned) (t + 1);   // independent hiss per track
    }

    for (auto& table : chunks)
    {
        table = std::vector<std::atomic<float*>> (maxChunks);
        for (auto& c : table)
            c.store (nullptr);
    }

    startTimerHz (10);
}

TapeMachine::~TapeMachine()
{
    stopTimer();
    for (auto& table : chunks)
        for (auto& c : table)
            delete[] c.exchange (nullptr);
    for (auto& r : retired)
        delete[] r.chunk;
}

void TapeMachine::prepare (double sr, int roundTripLatency)
{
    sampleRate = sr;
    setLatency (roundTripLatency);
    levelGain.prepare (sr, Dsp::dbToGain (recordLevel->get()));

    // 4x oversampling for the saturation, with a whole-sample latency we can compensate exactly.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numTracks, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true);
    oversampler->initProcessing ((size_t) Config::maxBlock);
    oversamplerLatency = juce::roundToInt (oversampler->getLatencyInSamples());

    for (auto& f : filters)
    {
        f.bump.reset();
        f.shelf.reset();
        f.lowPass.reset();
    }
    designedSpeed = -1;
}

bool TapeMachine::isRecording (int track) const noexcept
{
    return playing.load() && recordEnabled.load() && juce::isPositiveAndBelow (track, numTracks) && arm[(size_t) track]->on();
}

//==============================================================================
void TapeMachine::play()  { playing.store (true); }
void TapeMachine::stop()  { playing.store (false); }
void TapeMachine::setRecordEnabled (bool on) { recordEnabled.store (on); }

void TapeMachine::locate (juce::int64 p)
{
    p = juce::jlimit ((juce::int64) 0, (juce::int64) maxChunks * chunkSize - 1, p);
    for (int t = 0; t < numTracks; ++t)
        if (arm[(size_t) t]->on())
            ensureChunks (t, p - recordDelay() - chunkSize, p + (juce::int64) (allocateAheadSeconds * sampleRate));
    position.store (p);
    locateRequest.store (p);
}

void TapeMachine::eraseAll()
{
    stop();
    for (auto& r : trackRevision)
        r.fetch_add (1);
    const auto stamp = blocksProcessed.load();
    for (auto& table : chunks)
        for (auto& c : table)
            if (auto* old = c.exchange (nullptr))
                retired.push_back ({ old, stamp });
    length.store (0);
    locate (0);
}

//==============================================================================
void TapeMachine::ensureChunks (int track, juce::int64 from, juce::int64 to)
{
    const auto first = (size_t) juce::jmax ((juce::int64) 0, from >> chunkBits);
    const auto last = (size_t) juce::jlimit ((juce::int64) 0, (juce::int64) maxChunks - 1, to >> chunkBits);
    auto& table = chunks[(size_t) track];

    for (size_t i = first; i <= last; ++i)
        if (table[i].load (std::memory_order_acquire) == nullptr)
            table[i].store (new float[chunkSize] (), std::memory_order_release);
}

void TapeMachine::service()
{
    const auto p = position.load();
    for (int t = 0; t < numTracks; ++t)
        if (arm[(size_t) t]->on())
            ensureChunks (t, p - recordDelay() - chunkSize, p + (juce::int64) (allocateAheadSeconds * sampleRate));

    // Free erased chunks once the audio thread has moved on (or is not running at all).
    const auto blocks = blocksProcessed.load();
    idleTicks = blocks == lastSeenBlocks ? idleTicks + 1 : 0;
    lastSeenBlocks = blocks;

    for (auto it = retired.begin(); it != retired.end();)
    {
        if (blocks >= it->stamp + 2 || idleTicks >= 5)
        {
            delete[] it->chunk;
            it = retired.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

//==============================================================================
void TapeMachine::readTrack (int track, juce::int64 start, float* dest, int n) const noexcept
{
    const auto& table = chunks[(size_t) track];
    int done = 0;
    while (done < n)
    {
        const juce::int64 p = start + done;
        const auto offset = (int) (p & (chunkSize - 1));
        const int len = juce::jmin (n - done, chunkSize - offset);
        const auto idx = p >> chunkBits;
        const float* c = (p >= 0 && idx < (juce::int64) maxChunks) ? table[(size_t) idx].load (std::memory_order_acquire) : nullptr;

        if (c != nullptr)
            std::memcpy (dest + done, c + offset, sizeof (float) * (size_t) len);
        else
            std::memset (dest + done, 0, sizeof (float) * (size_t) len);
        done += len;
    }
}

void TapeMachine::write (int track, juce::int64 start, const float* src, int n) noexcept
{
    auto& table = chunks[(size_t) track];
    int done = 0;

    if (start < 0)   // the first moments of a take belong before the start of the tape
        done = (int) juce::jmin ((juce::int64) n, -start);

    while (done < n)
    {
        const juce::int64 p = start + done;
        const auto offset = (int) (p & (chunkSize - 1));
        const int len = juce::jmin (n - done, chunkSize - offset);
        const auto idx = p >> chunkBits;
        float* c = idx < (juce::int64) maxChunks ? table[(size_t) idx].load (std::memory_order_acquire) : nullptr;

        if (c != nullptr)
            std::memcpy (c + offset, src + done, sizeof (float) * (size_t) len);
        else
            dropped.fetch_add (len);   // allocation fell behind; should never happen in practice
        done += len;
    }
}

//==============================================================================
void TapeMachine::designFilters (int speedIndex) noexcept
{
    const auto& s = speeds[speedIndex];
    Dsp::Biquad bump, shelf, lowPass;
    bump.setPeak (sampleRate, s.bumpHz, 1.0, s.bumpDb);
    shelf.setHighShelf (sampleRate, 8000.0, s.shelfDb);
    lowPass.setLowPass (sampleRate, juce::jmin ((double) s.lowPassHz, sampleRate * 0.45), 0.7071);

    for (auto& f : filters)
    {
        f.bump.copyCoefficients (bump);
        f.shelf.copyCoefficients (shelf);
        f.lowPass.copyCoefficients (lowPass);
    }
    designedSpeed = speedIndex;
    designedRate = sampleRate;
}

void TapeMachine::saturate (int n, const Speed& s) noexcept
{
    float* ptrs[numTracks];
    for (int t = 0; t < numTracks; ++t)
        ptrs[t] = recordBuf[(size_t) t].data();

    juce::dsp::AudioBlock<float> block (ptrs, (size_t) numTracks, (size_t) n);
    auto up = oversampler->processSamplesUp (block);

    // Normalised so small signals pass at unity; louder signals compress, slightly asymmetrically.
    const float k = s.saturation, tb = std::tanh (saturationBias), scale = k * (1.0f - tb * tb);
    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
    {
        float* d = up.getChannelPointer (ch);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float x = juce::jlimit (-1.0f, 1.0f, d[i]);
            d[i] = (std::tanh (k * x + saturationBias) - tb) / scale;
        }
    }

    oversampler->processSamplesDown (block);
}


//==============================================================================
int TapeMachine::wowBase (const Speed& s) const noexcept
{
    if (! wowFlutter->on())
        return 0;
    // Enough fixed delay that the wobble never needs to read ahead of the tape.
    const double wow = s.wowPercent * 0.01 * sampleRate / (juce::MathConstants<double>::twoPi * 0.55);
    const double flutter = s.flutterPercent * 0.01 * sampleRate / (juce::MathConstants<double>::twoPi * 6.8);
    return (int) std::ceil (wow + flutter) + 3;
}

int TapeMachine::recordDelay() const noexcept
{
    return latency.load() + oversamplerLatency + (characterOn.load() ? wowBase (currentSpeed()) : 0);
}

void TapeMachine::loadTrack (int track, const float* data, juce::int64 n)
{
    jassert (! isPlaying());
    const auto stamp = blocksProcessed.load();
    for (auto& c : chunks[(size_t) track])
        if (auto* old = c.exchange (nullptr))
            retired.push_back ({ old, stamp });

    if (n <= 0)
        return;
    ensureChunks (track, 0, n - 1);
    for (juce::int64 done = 0; done < n;)
    {
        const auto len = (int) juce::jmin ((juce::int64) chunkSize, n - done);
        std::memcpy (chunks[(size_t) track][(size_t) (done >> chunkBits)].load(), data + done, sizeof (float) * (size_t) len);
        done += len;
    }
    trackRevision[(size_t) track].fetch_add (1);
}

void TapeMachine::process (const float* const* tapeIn, float* const* tapeOut, int n) noexcept
{
    jassert (n <= Config::maxBlock);
    blocksProcessed.fetch_add (1);

    const auto req = locateRequest.exchange (-1);
    if (req >= 0)
        pos = req;

    const bool isPlay = playing.load();
    const bool rec = recordEnabled.load() && ! exporting.load();
    const bool character = characterOn.load() && oversampler != nullptr;

    const int speedIndex = juce::jlimit (0, 2, juce::roundToInt (speed->get()));
    const auto& sp = speeds[speedIndex];
    if (speedIndex != designedSpeed || sampleRate != designedRate)
        designFilters (speedIndex);

    // Record level, smoothed, shared by all tracks.
    const float levelT = Dsp::dbToGain (recordLevel->get());
    for (int i = 0; i < n; ++i)
        gainScratch[(size_t) i] = levelGain.next (levelT);

    bool anyArmed = false;
    for (int t = 0; t < numTracks; ++t)
    {
        const bool armed = arm[(size_t) t]->on() && ! exporting.load (std::memory_order_relaxed);
        anyArmed = anyArmed || armed;
        float* dst = recordBuf[(size_t) t].data();
        const float* in = tapeIn[t];
        if (armed && in != nullptr)
            for (int i = 0; i < n; ++i)
                dst[i] = in[i] * gainScratch[(size_t) i];
        else
            std::memset (dst, 0, sizeof (float) * (size_t) n);
    }

    if (character && anyArmed)
        saturate (n, sp);

    // Wow and flutter: one wobble for the whole transport (as on a real machine), smaller at higher speeds.
    const int base = character ? wowBase (sp) : 0;
    if (base > 0)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double wowAmp = sp.wowPercent * 0.01 * sampleRate / (twoPi * 0.55);
        const double flutterAmp = sp.flutterPercent * 0.01 * sampleRate / (twoPi * 6.8);
        const double incW = twoPi * 0.55 / sampleRate, incF1 = twoPi * 6.8 / sampleRate, incF2 = twoPi * 9.3 / sampleRate;
        for (int i = 0; i < n; ++i)
        {
            const double wobble = wowAmp * std::sin (wowPhase) + flutterAmp * (0.65 * std::sin (flutterPhase1) + 0.35 * std::sin (flutterPhase2));
            wowDelay[(size_t) i] = (float) (base - wobble);
            wowPhase += incW;
            flutterPhase1 += incF1;
            flutterPhase2 += incF2;
        }
        wowPhase = std::fmod (wowPhase, twoPi);
        flutterPhase1 = std::fmod (flutterPhase1, twoPi);
        flutterPhase2 = std::fmod (flutterPhase2, twoPi);
    }

    // Everything the musician hears from tape is late by this much: recordings are written that much earlier.
    const int delay = latency.load() + (character ? oversamplerLatency + base : 0);

    // Loop: split the block where the tape reaches the Out point and jump back to In.
    const bool loop = loopOn.load() && hasRegion();
    const auto loopIn = inPoint.load(), loopOut = outPoint.load();
    int done = 0;
    while (done < n)
    {
        int count = n - done;
        if (isPlay && loop && pos < loopOut)
            count = (int) juce::jmin ((juce::int64) count, loopOut - pos);

        processSegment (tapeOut, done, count, isPlay, rec, character, sp, delay);

        if (isPlay)
        {
            pos += count;
            if (loop && pos == loopOut)
                pos = loopIn;
        }
        done += count;
    }

    if (isPlay)
    {
        const bool endOfTape = pos >= (juce::int64) maxChunks * chunkSize - Config::maxBlock;
        const bool looping = loop && pos < loopOut;
        if (endOfTape || (! rec && ! looping && pos >= length.load()))
            playing.store (false);
    }

    position.store (pos);
}

void TapeMachine::processSegment (float* const* tapeOut, int offset, int count,
                                  bool isPlay, bool rec, bool character, const Speed& sp, int delay) noexcept
{
    const auto punchIn = inPoint.load(), punchOut = outPoint.load();
    const bool punch = punchOn.load() && punchOut > punchIn && isPlay && rec;
    const bool wobble = character && wowBase (sp) > 0;
    const float hissGain = Dsp::dbToGain (sp.hissDb);
    float* play = playScratch.data();
    juce::int64 recordedEnd = -1;

    for (int t = 0; t < numTracks; ++t)
    {
        float* out = tapeOut[t] + offset;
        const float* input = recordBuf[(size_t) t].data() + offset;
        const bool armed = arm[(size_t) t]->on() && ! exporting.load (std::memory_order_relaxed);

        // Playback (plus hiss while rolling): unarmed tracks, and armed tracks outside the punch region.
        const bool needPlay = isPlay && (! armed || punch);
        if (needPlay)
        {
            readTrack (t, pos, play, count);
            if (character)
            {
                auto& noise = hiss[(size_t) t];
                for (int i = 0; i < count; ++i)
                    play[i] += hissGain * noise.next();
            }
        }
        else
        {
            std::memset (play, 0, sizeof (float) * (size_t) count);
        }

        if (wobble)
        {
            // Modulated delay with linear interpolation; the line keeps running so it never clicks.
            auto& line = wowLine[(size_t) t];
            for (int i = 0; i < count; ++i)
            {
                const int w = (wowWrite + i) & (wowLineSize - 1);
                line[(size_t) w] = play[i];
                const float readPos = (float) (wowWrite + i) - wowDelay[(size_t) (offset + i)];
                const int i0 = (int) std::floor (readPos);
                const float frac = readPos - (float) i0;
                const float a = line[(size_t) (i0 & (wowLineSize - 1))], b = line[(size_t) ((i0 + 1) & (wowLineSize - 1))];
                play[i] = a + (b - a) * frac;
            }
        }

        if (! armed)
        {
            std::memcpy (out, play, sizeof (float) * (size_t) count);
        }
        else if (punch)
        {
            // Auto-input: hear the old take until the punch-in point, the input inside the region.
            for (int i = 0; i < count; ++i)
            {
                const auto heard = pos + i;
                out[i] = (heard >= punchIn && heard < punchOut) ? input[i] : play[i];
            }
        }
        else
        {
            std::memcpy (out, input, sizeof (float) * (size_t) count);
        }

        if (armed && isPlay && rec)
        {
            const auto w0 = pos - delay;
            auto a = w0, b = w0 + count;
            if (punch)
            {
                a = juce::jmax (a, punchIn);
                b = juce::jmin (b, punchOut);
            }
            if (b > a)
            {
                write (t, a, input + (a - w0), (int) (b - a));
                trackRevision[(size_t) t].fetch_add (1, std::memory_order_relaxed);
                recordedEnd = juce::jmax (recordedEnd, b);
            }
        }

        if (character)
        {
            auto& f = filters[(size_t) t];
            for (int i = 0; i < count; ++i)
                out[i] = f.lowPass.process (f.shelf.process (f.bump.process (out[i])));
        }
    }

    if (wobble)
        wowWrite = (wowWrite + count) & (wowLineSize - 1);

    if (recordedEnd > 0)
    {
        auto len = length.load();
        while (recordedEnd > len && ! length.compare_exchange_weak (len, recordedEnd)) {}
    }
}

bool TapeMachine::trackHasAudio (int track) const noexcept
{
    for (auto& c : chunks[(size_t) track])
        if (c.load (std::memory_order_acquire) != nullptr)
            return true;
    return false;
}

void TapeMachine::writeTrack (int track, juce::int64 start, const float* data, int n)
{
    if (n <= 0)
        return;
    ensureChunks (track, start, start + n - 1);
    write (track, start, data, n);
    trackRevision[(size_t) track].fetch_add (1);
}
