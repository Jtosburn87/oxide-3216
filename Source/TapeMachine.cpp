#include "TapeMachine.h"

namespace
{
    constexpr double allocateAheadSeconds = 20.0;
    constexpr float saturationBias = 0.12f;   // asymmetry of the tape curve (from the prototype)
    constexpr double rateGlideSeconds = 0.12; // speed changes settle in about a third of a second

    /** 4-point, 3rd-order Hermite interpolation between x0 and x1 (t = 0..1). */
    inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
}

TapeMachine::TapeMachine (ParamSet& params)
    : maxChunks ((size_t) std::ceil (Config::maxSampleRate * 60.0 * Config::maxTapeMinutes / chunkSize))
{
    recordLevel = &params.add ("tape.level", "Record level", -12.0f, 18.0f, 0.0f);
    speed = &params.addStepped ("tape.speed", "Tape speed", 3, 1);   // 15 ips
    pitch = &params.add ("tape.pitch", "Pitch", -maxPitchPercent, maxPitchPercent, 0.0f);
    wowFlutter = &params.addToggle ("tape.wf", "Wow and flutter", true);
    wowAmount = &params.add ("tape.wow", "Wow", 0.0f, maxWowFlutterPercent, 100.0f);
    flutterAmount = &params.add ("tape.flutter", "Flutter", 0.0f, maxWowFlutterPercent, 100.0f);
    for (int t = 0; t < numTracks; ++t)
    {
        arm[(size_t) t] = &params.addToggle ("tape.arm" + juce::String (t + 1), "Arm " + juce::String (t + 1), false);
        armPlay[(size_t) t] = &params.addToggle ("tape.armplay" + juce::String (t + 1), "Arm " + juce::String (t + 1) + " plays back", false);
        hiss[(size_t) t].state = 0x9e3779b9u * (unsigned) (t + 1);   // independent hiss per track
    }

    for (auto& table : chunks)
    {
        table = std::vector<std::atomic<float*>> (maxChunks);
        for (auto& c : table)
            c.store (nullptr);
    }
    for (auto& flags : chunkChanged)
    {
        flags = std::make_unique<std::atomic<bool>[]> (maxChunks);
        for (size_t i = 0; i < maxChunks; ++i)
            flags[i].store (false);
    }

    // Vari-speed buffers, made once here (never on the audio thread).
    for (auto& r : vs)
        r.stream.assign ((size_t) maxFetch + 4, 0.0f);
    fetchCount.assign ((size_t) Config::maxBlock, 0);
    outFrac.assign ((size_t) Config::maxBlock, 0.0f);
    fetchTime.assign ((size_t) maxFetch, 0.0f);
    fetchPos.assign ((size_t) maxFetch, 0);
    inScratch.assign ((size_t) Config::maxBlock + 4, 0.0f);
    recScratch.assign ((size_t) maxFetch, 0.0f);
    fadeScratch.assign ((size_t) maxFetch, 0.0f);
    memories.fill (-1);

    startTimerHz (10);
}

double TapeMachine::getPlaybackRate() const noexcept
{
    const double nominal = ipsOf (juce::jlimit (0, 2, juce::roundToInt (speed->get())));
    const double tag = recordedIps.load();
    const double varispeed = 1.0 + juce::jlimit (-maxPitchPercent, maxPitchPercent, pitch->get()) * 0.01;
    return nominal * varispeed / (tag > 0.0 ? tag : nominal);   // exactly 1 at the recorded speed, pitch centred
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
    prepared = true;
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
    rateNow = getPlaybackRate();
    vsPrimed = false;
    aaPlayCut = aaRecCut = 0.0f;
}

TapeMachine::ArmState TapeMachine::getArmState (int t) const noexcept
{
    if (! juce::isPositiveAndBelow (t, numTracks) || ! arm[(size_t) t]->on())
        return ArmState::off;
    return armPlay[(size_t) t]->on() ? ArmState::playback : ArmState::armed;
}

void TapeMachine::setArmState (int t, ArmState s)
{
    if (! juce::isPositiveAndBelow (t, numTracks))
        return;
    armPlay[(size_t) t]->set (s == ArmState::playback ? 1.0f : 0.0f);
    arm[(size_t) t]->set (s == ArmState::off ? 0.0f : 1.0f);
}

void TapeMachine::stepArmState (int t)
{
    const auto s = getArmState (t);
    setArmState (t, s == ArmState::off ? ArmState::playback : s == ArmState::playback ? ArmState::armed : ArmState::off);
}

bool TapeMachine::takesInput (int t) const noexcept
{
    return arm[(size_t) t]->on() && (! armPlay[(size_t) t]->on() || recordEnabled.load (std::memory_order_relaxed));
}

bool TapeMachine::isRecording (int track) const noexcept
{
    return playing.load() && recordEnabled.load() && juce::isPositiveAndBelow (track, numTracks) && arm[(size_t) track]->on();
}

//==============================================================================
void TapeMachine::play()
{
    windHeld.store (false);
    shuttleOn.store (false);
    jogOn.store (false);
    windTarget.store (-1);
    windRequest.store (0);
    windReset.store (true);
    playing.store (true);
}

void TapeMachine::stop()
{
    windHeld.store (false);
    playing.store (false);
    recordEnabled.store (false);   // Stop takes the master Record off; the tracks stay armed
    shuttleOn.store (false);
    jogOn.store (false);
    windTarget.store (-1);
    windRequest.store (0);   // a wind brakes to a stop
}

void TapeMachine::rewind()      { playing.store (false); shuttleOn.store (false); jogOn.store (false); windTarget.store (-1); windRequest.store (-1); }
void TapeMachine::fastForward() { playing.store (false); shuttleOn.store (false); jogOn.store (false); windTarget.store (-1); windRequest.store (1); }

void TapeMachine::setShuttle (float s)
{
    s = juce::jlimit (-shuttleMaxSpeed, shuttleMaxSpeed, s);
    if (s == 0.0f && ! shuttleOn.load())
        return;
    playing.store (false);
    jogOn.store (false);
    windTarget.store (-1);
    windRequest.store (0);
    shuttleTarget.store (s);
    shuttleOn.store (true);   // stays on at 0 (holding the tape still) until Play, Stop, a wind, a jog or a locate
}

void TapeMachine::jog (double samples)
{
    // A jog starts from wherever the tape is; further turns add to where it is heading.
    const double capacity = (double) ((juce::int64) maxChunks * chunkSize - 1);
    const double from = jogOn.load() ? jogTarget.load() : (double) position.load();
    playing.store (false);
    shuttleOn.store (false);
    windTarget.store (-1);
    windRequest.store (0);
    jogTarget.store (juce::jlimit (0.0, capacity, from + samples));
    if (! jogOn.exchange (true))
        windReset.store (true);   // a running wind stops; the jog takes the tape from here
}

void TapeMachine::windTo (juce::int64 target)
{
    windHeld.store (false);
    target = juce::jlimit ((juce::int64) 0, (juce::int64) maxChunks * chunkSize - 1, target);
    playing.store (false);
    shuttleOn.store (false);
    jogOn.store (false);
    const auto from = position.load();
    if (target == from && ! isWinding())
        return;
    for (int t = 0; t < numTracks; ++t)   // armed tracks can record from there
        if (arm[(size_t) t]->on())
            ensureChunks (t, target - (juce::int64) ((recordDelay() + 8) * maxRate) - chunkSize,
                          target + (juce::int64) (allocateAheadSeconds * sampleRate * juce::jmax (1.0, getPlaybackRate())));
    windTarget.store (target);
    windRequest.store (target < from ? -1 : 1);
}
void TapeMachine::setRecordEnabled (bool on) { recordEnabled.store (on); }

void TapeMachine::locate (juce::int64 p)
{
    windHeld.store (false);
    p = juce::jlimit ((juce::int64) 0, (juce::int64) maxChunks * chunkSize - 1, p);
    for (int t = 0; t < numTracks; ++t)
        if (arm[(size_t) t]->on())
            ensureChunks (t, p - (juce::int64) ((recordDelay() + 8) * maxRate) - chunkSize,   // vari-speed records up to maxRate x the delay back
                          p + (juce::int64) (allocateAheadSeconds * sampleRate * juce::jmax (1.0, getPlaybackRate())));
    shuttleOn.store (false);
    jogOn.store (false);
    windTarget.store (-1);
    windRequest.store (0);
    windReset.store (true);
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
    for (int t = 0; t < numTracks; ++t)
        clearTrackFile (t);
    length.store (0);
    recordedIps.store (0.0f);   // a blank tape takes the speed of its next recording
    locate (0);
}

void TapeMachine::eraseSection (int track, juce::int64 from, juce::int64 to)
{
    from = juce::jmax ((juce::int64) 0, from);
    to = juce::jmin (to, getCapacity());
    if (! juce::isPositiveAndBelow (track, numTracks) || to <= from)
        return;

    // Raised-cosine fades inside the section's ends: the old audio fades out, and back in at the end.
    const auto fade = juce::jmin ((juce::int64) std::round (0.005 * sampleRate), (to - from) / 2);
    auto& table = chunks[(size_t) track];
    for (auto p = from; p < to;)
    {
        const auto offset = (int) (p & (chunkSize - 1));
        const auto len = (int) juce::jmin (to - p, (juce::int64) (chunkSize - offset));
        if (float* c = table[(size_t) (p >> chunkBits)].load (std::memory_order_acquire))
        {
            for (int i = 0; i < len; ++i)
            {
                const auto q = p + i;
                const auto edge = juce::jmin (q - from, to - 1 - q);
                const float keep = edge >= fade ? 0.0f
                                 : 0.5f + 0.5f * std::cos (((float) edge + 0.5f) / (float) fade * juce::MathConstants<float>::pi);
                c[offset + i] *= keep;
            }
        }
        p += len;
    }
    trackRevision[(size_t) track].fetch_add (1);
    for (auto c = from >> chunkBits; c <= (to - 1) >> chunkBits; ++c)
        markChunkChanged (track, (int) c);
}

void TapeMachine::eraseTrack (int track)
{
    if (! juce::isPositiveAndBelow (track, numTracks))
        return;
    const auto stamp = blocksProcessed.load();
    for (auto& c : chunks[(size_t) track])
        if (auto* old = c.exchange (nullptr))
            retired.push_back ({ old, stamp });
    trackRevision[(size_t) track].fetch_add (1);
    clearTrackFile (track);

    bool anyLeft = false;
    for (int t = 0; t < numTracks; ++t)
        anyLeft = anyLeft || trackHasAudio (t);
    if (! anyLeft)
    {
        length.store (0);
        recordedIps.store (0.0f);
    }
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
            ensureChunks (t, p - (juce::int64) ((recordDelay() + 8) * maxRate) - chunkSize,   // vari-speed records up to maxRate x the delay back
                          p + (juce::int64) (allocateAheadSeconds * sampleRate * juce::jmax (1.0, getPlaybackRate())));

    // Free erased chunks once the audio thread has moved on (or is not running at all).
    const auto blocks = blocksProcessed.load();
    idleTicks = blocks == lastSeenBlocks ? idleTicks + 1 : 0;
    lastSeenBlocks = blocks;

    const juce::ScopedLock sl (storageLock);   // TapeFiles may be reading a chunk on its thread
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

    // The end first, then the chunk flags, so TapeFiles never sees a changed chunk without its extent.
    auto end = writtenEnd[(size_t) track].load (std::memory_order_relaxed);
    const auto newEnd = juce::jmin (start + n, getCapacity());
    while (newEnd > end && ! writtenEnd[(size_t) track].compare_exchange_weak (end, newEnd, std::memory_order_release)) {}

    while (done < n)
    {
        const juce::int64 p = start + done;
        const auto offset = (int) (p & (chunkSize - 1));
        const int len = juce::jmin (n - done, chunkSize - offset);
        const auto idx = p >> chunkBits;
        float* c = idx < (juce::int64) maxChunks ? table[(size_t) idx].load (std::memory_order_acquire) : nullptr;

        if (c != nullptr)
        {
            std::memcpy (c + offset, src + done, sizeof (float) * (size_t) len);
            chunkChanged[(size_t) track][(size_t) idx].store (true, std::memory_order_release);
        }
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
    // Enough fixed delay that the wobble never needs to read ahead of the tape, with the Wow and Flutter knobs all
    // the way up: turning them never moves the delay (no click, and recordings stay lined up).
    const double most = maxWowFlutterPercent * 0.01;
    const double wow = s.wowPercent * 0.01 * most * sampleRate / (juce::MathConstants<double>::twoPi * 0.55);
    const double flutter = s.flutterPercent * 0.01 * most * sampleRate / (juce::MathConstants<double>::twoPi * 6.8);
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
    clearTrackFile (track);

    if (n <= 0)
        return;
    if (recordedIps.load() <= 0.0f)
        recordedIps.store (ipsOf (juce::jlimit (0, 2, juce::roundToInt (speed->get()))));
    ensureChunks (track, 0, n - 1);
    writtenEnd[(size_t) track].store (n);
    for (juce::int64 done = 0; done < n;)
    {
        const auto len = (int) juce::jmin ((juce::int64) chunkSize, n - done);
        std::memcpy (chunks[(size_t) track][(size_t) (done >> chunkBits)].load(), data + done, sizeof (float) * (size_t) len);
        markChunkChanged (track, (int) (done >> chunkBits));
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
    {
        pos = req;
        vsPrimed = false;
        moving = false;
    }
    if (windReset.exchange (false))
    {
        windVel = 0.0;
        windSpeed.store (0.0f);
    }

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
        const bool armed = takesInput (t) && ! exporting.load (std::memory_order_relaxed);
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
        // The Wow / Flutter knobs glide (about 50 ms), so turning them is smooth.
        const double wowTarget = wowAmount->get() * 0.01, flutterTarget = flutterAmount->get() * 0.01;
        const double glide = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
        for (int i = 0; i < n; ++i)
        {
            wowScale += (wowTarget - wowScale) * glide;
            flutterScale += (flutterTarget - flutterScale) * glide;
            const double wobble = wowAmp * wowScale * std::sin (wowPhase)
                                + flutterAmp * flutterScale * (0.65 * std::sin (flutterPhase1) + 0.35 * std::sin (flutterPhase2));
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

    // Speed and pitch. A blank tape takes the speed it is first recorded at. The rate glides to a new
    // setting the way a capstan motor gets up to speed.
    if (isPlay && rec && anyArmed && recordedIps.load() <= 0.0f)
        recordedIps.store (ipsOf (speedIndex));
    const double target = getPlaybackRate();
    const double r0 = rateNow;
    double r1 = r0 + (target - r0) * (1.0 - std::exp (-(double) n / (sampleRate * rateGlideSeconds)));
    if (std::abs (r1 - target) < 1.0e-5)
        r1 = target;
    rateNow = r1 = juce::jlimit (1.0 / maxRate, maxRate, r1);

    const bool loop = loopOn.load() && hasRegion();
    const auto loopIn = inPoint.load(), loopOut = outPoint.load();
    if (isPlay && ! (r0 == 1.0 && r1 == 1.0))
    {
        processVarispeed (tapeOut, n, rec, character, sp, delay, r0, r1);
    }
    else
    {
        // The tape at its recorded speed: one tape sample per output sample.
        // Loop: split the block where the tape reaches the Out point and jump back to In.
        vsPrimed = false;
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
    }

    if (isPlay)
    {
        moving = false;
        const bool endOfTape = pos >= (juce::int64) maxChunks * chunkSize - Config::maxBlock;
        const bool looping = loop && pos < loopOut;
        if (endOfTape || (! rec && ! looping && pos >= juce::jmax (length.load(), playOnUntil.load())))
            playing.store (false);
        scrubGain = 0.0f;
    }
    else
    {
        // Shuttle and jog can be heard (Scrub); Rew and F Fwd stay silent, with the tape off the heads.
        const double from = moving ? windPos : (double) pos;
        const bool manual = shuttleOn.load() || jogOn.load();
        wind (n);
        const double to = moving ? windPos : (double) pos;
        if (! exporting.load (std::memory_order_relaxed))
            scrub (tapeOut, n, from, to, manual && scrubOn.load());
    }

    // Tape out meters (the meter bridge).
    for (int t = 0; t < numTracks; ++t)
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
            pk = juce::jmax (pk, std::abs (tapeOut[t][i]));
        auto current = outPeak[(size_t) t].load (std::memory_order_relaxed);
        while (pk > current && ! outPeak[(size_t) t].compare_exchange_weak (current, pk, std::memory_order_relaxed)) {}
    }

    position.store (pos);
}

void TapeMachine::wind (int n) noexcept
{
    const bool shuttle = shuttleOn.load(), jogging = jogOn.load();
    const int dir = windRequest.load();
    if (! shuttle && ! jogging && dir == 0 && windVel == 0.0)
    {
        moving = false;
        windSpeed.store (0.0f);
        return;
    }
    if (! moving)
        windPos = (double) pos;
    moving = true;
    const double dt = n / sampleRate;
    const double lastSample = (double) ((juce::int64) maxChunks * chunkSize - 1);

    if (shuttle)
    {
        // The shuttle ring: get to its speed quickly (full speed in half a second), stop at either end.
        const double target = shuttleTarget.load(), accel = shuttleMaxSpeed / 0.5;
        windVel = target > windVel ? juce::jmin (target, windVel + accel * dt) : juce::jmax (target, windVel - accel * dt);
        const double end = juce::jmax ((double) length.load(), juce::jlimit (0.0, lastSample, windPos));
        windPos = juce::jlimit (0.0, end, windPos + windVel * n);
        if ((windPos <= 0.0 && windVel < 0.0) || (windPos >= end && windVel > 0.0))
            windVel = 0.0;
        pos = (juce::int64) windPos;
        windSpeed.store ((float) windVel);
        return;
    }

    if (jogging)
    {
        // The jog wheel: the tape follows its target smoothly (about 40 ms behind), at most 16x play speed.
        const double target = jogTarget.load(), maxStep = 16.0 * n;
        const double step = juce::jlimit (-maxStep, maxStep, (target - windPos) * (1.0 - std::exp (-dt / 0.04)));
        windPos = std::abs (target - windPos) < 0.5 ? target : windPos + step;
        windVel = 0.0;
        pos = (juce::int64) windPos;
        windSpeed.store ((float) (step / n));
        return;
    }

    // Speed up towards full winding speed (faster while the key is held down), or brake (Stop, or a change of direction).
    const double top = windHeld.load() ? windHeldSpeed : windMaxSpeed;
    const double accel = top / windRampSeconds, brake = windMaxSpeed / windBrakeSeconds;
    if (dir != 0 && (windVel == 0.0 || (windVel > 0.0) == (dir > 0)))
    {
        if (std::abs (windVel) > top)   // let go after holding: settle back to full winding speed
            windVel = windVel > 0.0 ? juce::jmax (top, windVel - brake * dt) : juce::jmin (-top, windVel + brake * dt);
        else
            windVel = juce::jlimit (-top, top, windVel + dir * accel * dt);
    }
    else
        windVel = windVel > 0.0 ? juce::jmax (0.0, windVel - brake * dt) : juce::jmin (0.0, windVel + brake * dt);

    // Rewind ends at the start, fast forward at the end of the recording, an autolocate (windTo) at its target.
    // Never faster than the speed that can still brake in time, so the tape arrives there gently.
    const auto target = windTarget.load();
    if (target >= 0 && dir != 0 && windVel != 0.0 && (windVel > 0.0) != (dir > 0))
    {
        // An autolocate the other way: brake first (above), then head for the target.
        windPos = juce::jmax (0.0, windPos + windVel * n);
        pos = (juce::int64) windPos;
        windSpeed.store ((float) windVel);
        return;
    }
    const double end = target >= 0 && dir != 0 ? (double) target : windVel < 0.0 ? 0.0 : (double) length.load();
    const double before = windPos;
    const bool alreadyThere = windVel < 0.0 ? before <= end : before >= end;
    if (! alreadyThere)
    {
        const double arrival = std::sqrt (2.0 * brake * std::abs (end - before) / sampleRate) + 0.5;
        windVel = juce::jlimit (-arrival, arrival, windVel);
        windPos += windVel * n;
    }

    const bool arrived = alreadyThere || (windVel < 0.0 ? windPos <= end : windPos >= end);
    if (arrived && windVel != 0.0)
    {
        if (! alreadyThere)
            windPos = end;
        windVel = 0.0;
        auto expected = dir;
        if (windRequest.compare_exchange_strong (expected, 0))   // unless a new request came in meanwhile
        {
            auto expectedTarget = target;
            windTarget.compare_exchange_strong (expectedTarget, -1);
        }
    }

    pos = (juce::int64) windPos;
    windSpeed.store ((float) windVel);
}

float TapeMachine::sampleAt (int track, juce::int64 p) const noexcept
{
    const auto idx = p >> chunkBits;
    if (p < 0 || idx >= (juce::int64) maxChunks)
        return 0.0f;
    const float* c = chunks[(size_t) track][(size_t) idx].load (std::memory_order_acquire);
    return c != nullptr ? c[p & (chunkSize - 1)] : 0.0f;
}

void TapeMachine::scrub (float* const* tapeOut, int n, double from, double to, bool audible) noexcept
{
    // The tape moves from `from` to `to` over this block: play what passes the heads (unarmed tracks; armed
    // ones keep monitoring their input). The level fades in and out over 5 ms so starting and stopping never
    // click, and two one-pole low-passes take the edge off when it runs faster than play speed.
    if (! audible && scrubGain == 0.0f)
        return;
    const double step = (to - from) / n;
    const double rate = std::abs (step);
    const float a = rate > 1.0 ? (float) std::exp (-juce::MathConstants<double>::twoPi * 0.45 / rate) : 0.0f;
    const float fade = (float) (1.0 / (0.005 * sampleRate));

    for (int t = 0; t < numTracks; ++t)
    {
        if (takesInput (t))
            continue;
        auto& lp = scrubLowPass[(size_t) t];
        float g = scrubGain;
        for (int i = 0; i < n; ++i)
        {
            const double p = from + step * (i + 1);
            const auto i0 = (juce::int64) std::floor (p);
            const float frac = (float) (p - (double) i0);
            const float x0 = sampleAt (t, i0);
            const float x = x0 + frac * (sampleAt (t, i0 + 1) - x0);
            lp[0] += (1.0f - a) * (x - lp[0]);
            lp[1] += (1.0f - a) * (lp[0] - lp[1]);
            g = audible ? juce::jmin (1.0f, g + fade) : juce::jmax (0.0f, g - fade);
            tapeOut[t][i] += g * lp[1];
        }
    }
    scrubGain = audible ? juce::jmin (1.0f, scrubGain + fade * (float) n) : juce::jmax (0.0f, scrubGain - fade * (float) n);
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
        const bool armed = takesInput (t) && ! exporting.load (std::memory_order_relaxed);

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
            wobblePlayback (t, play, offset, count);

        if (! armed)
        {
            std::memcpy (out, play, sizeof (float) * (size_t) count);
        }
        else if (punch)
        {
            // Auto-input: hear the old take until the punch-in point, the input inside the region,
            // crossfading at both ends exactly as the recording does.
            for (int i = 0; i < count; ++i)
            {
                const float g = punchGain (pos + i, punchIn, punchOut);
                out[i] = input[i] * g + play[i] * (1.0f - g);
            }
        }
        else
        {
            std::memcpy (out, input, sizeof (float) * (size_t) count);
        }

        if (armed && isPlay && rec)
            recordRun (t, pos - delay, input, count, punch, punchIn, punchOut, recordedEnd);

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

void TapeMachine::wobblePlayback (int t, float* play, int offset, int count) noexcept
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

void TapeMachine::recordRun (int t, juce::int64 w0, const float* src, int count, bool punch,
                             juce::int64 punchIn, juce::int64 punchOut, juce::int64& recordedEnd) noexcept
{
    auto a = w0, b = w0 + count;
    if (punch)
    {
        a = juce::jmax (a, punchIn);
        b = juce::jmin (b, punchOut);
    }
    if (b <= a)
        return;

    if (punch)
    {
        // Smooth punch: crossfade from the old take into the new one after the In point,
        // and back to the old take before the Out point.
        const int len = (int) (b - a);
        float* mixed = fadeScratch.data();
        readTrack (t, a, mixed, len);
        const float* fresh = src + (a - w0);
        for (int i = 0; i < len; ++i)
        {
            const float g = punchGain (a + i, punchIn, punchOut);
            mixed[i] = fresh[i] * g + mixed[i] * (1.0f - g);
        }
        write (t, a, mixed, len);
    }
    else
    {
        write (t, a, src + (a - w0), (int) (b - a));
    }
    trackRevision[(size_t) t].fetch_add (1, std::memory_order_relaxed);
    recordedEnd = juce::jmax (recordedEnd, b);
}

//==============================================================================
void TapeMachine::designAntiAlias (double r0, double r1) noexcept
{
    // Playing faster squeezes the tape's top end above the output's Nyquist, so it is filtered off on the tape
    // side first; recording at a slower rate than the tape's own does the same to the input.
    const float playCut = (float) (0.45 * sampleRate / juce::jmax (1.0, juce::jmax (r0, r1)));
    const float recCut = (float) (0.45 * sampleRate * juce::jmin (1.0, juce::jmin (r0, r1)));
    const auto redesign = [this] (float cut, float& designed, Dsp::Biquad Resampler::* first, Dsp::Biquad Resampler::* second)
    {
        if (std::abs (cut - designed) <= designed * 0.005f)
            return;
        designed = cut;
        Dsp::Biquad a, b;   // 4th-order Butterworth
        a.setLowPass (sampleRate, cut, 0.5412);
        b.setLowPass (sampleRate, cut, 1.3066);
        for (auto& v : vs)
        {
            (v.*first).copyCoefficients (a);
            (v.*second).copyCoefficients (b);
        }
    };
    redesign (playCut, aaPlayCut, &Resampler::play1, &Resampler::play2);
    redesign (recCut, aaRecCut, &Resampler::rec1, &Resampler::rec2);
}

void TapeMachine::primeVarispeed() noexcept
{
    // Start from the tape just behind the head, through the filter (warmed up on the 64 samples before),
    // so going into vari-speed in the middle of a song does not click.
    constexpr int warm = 64;
    float* scratch = recScratch.data();
    for (int t = 0; t < numTracks; ++t)
    {
        auto& v = vs[(size_t) t];
        v.play1.reset();
        v.play2.reset();
        v.rec1.reset();
        v.rec2.reset();
        v.inHistory.fill (0.0f);
        readTrack (t, pos - warm - 4, scratch, warm + 4);
        for (int i = 0; i < warm + 4; ++i)
            scratch[i] = v.play2.process (v.play1.process (scratch[i]));
        std::copy (scratch + warm, scratch + warm + 4, v.stream.begin());
    }
    vsPhase = 0.0;
    vsPrimed = true;
}

void TapeMachine::processVarispeed (float* const* tapeOut, int n, bool rec, bool character, const Speed& sp, int delay,
                                    double r0, double r1) noexcept
{
    designAntiAlias (r0, r1);
    if (! vsPrimed)
        primeVarispeed();

    // The tape passes r of its samples per output sample (r ramps across the block when the speed changes).
    // Note when the head crosses each tape sample, and where the output samples fall between them.
    int K = 0;
    double f = vsPhase;
    for (int i = 0; i < n; ++i)
    {
        const double r = r0 + (r1 - r0) * (double) (i + 1) / (double) n;
        f += r;
        int c = 0;
        while (f >= 1.0 && K < maxFetch)
        {
            f -= 1.0;
            fetchTime[(size_t) K++] = (float) ((double) i - f / r);   // in output samples, from the block start
            ++c;
        }
        fetchCount[(size_t) i] = c;
        outFrac[(size_t) i] = (float) juce::jmin (f, 0.999999);
    }
    vsPhase = juce::jmin (f, 0.999999);

    // Which tape samples those are (the loop jumps from Out back to In).
    const auto startPos = pos;
    const bool loop = loopOn.load() && hasRegion();
    const auto loopIn = inPoint.load(), loopOut = outPoint.load();
    for (int k = 0; k < K; ++k)
    {
        fetchPos[(size_t) k] = pos;
        ++pos;
        if (loop && pos == loopOut)
            pos = loopIn;
    }
    const auto runLength = [this, K] (int k)
    {
        int len = 1;
        while (k + len < K && fetchPos[(size_t) (k + len)] == fetchPos[(size_t) (k + len - 1)] + 1)
            ++len;
        return len;
    };

    const auto punchIn = inPoint.load(), punchOut = outPoint.load();
    const bool punch = punchOn.load() && punchOut > punchIn && rec;
    const bool wobble = character && wowBase (sp) > 0;
    const float hissGain = Dsp::dbToGain (sp.hissDb);
    float* play = playScratch.data();
    juce::int64 recordedEnd = -1;

    // Output is heard two tape samples behind the newest one fetched (so all four interpolation points exist).
    // A take is written back by the round trip (in tape samples at this rate) plus that lag, and the input is
    // read two samples late for the same reason.
    const auto recordShift = (juce::int64) std::llround (r0 * (double) (delay + 2)) + 2;

    for (int t = 0; t < numTracks; ++t)
    {
        auto& v = vs[(size_t) t];
        float* out = tapeOut[t];
        const float* input = recordBuf[(size_t) t].data();
        const bool armed = takesInput (t) && ! exporting.load (std::memory_order_relaxed);
        const bool needPlay = ! armed || punch;
        float* stream = v.stream.data();   // [0, 4): history; [4, 4 + K): this block's tape samples

        if (needPlay)
        {
            for (int k = 0; k < K;)
            {
                const int len = runLength (k);
                readTrack (t, fetchPos[(size_t) k], stream + 4 + k, len);
                k += len;
            }
            for (int k = 0; k < K; ++k)
                stream[4 + k] = v.play2.process (v.play1.process (stream[4 + k]));
        }
        else
        {
            std::fill (stream + 4, stream + 4 + K, 0.0f);
        }

        int newest = 3;
        for (int i = 0; i < n; ++i)
        {
            newest += fetchCount[(size_t) i];
            play[i] = hermite (stream[newest - 3], stream[newest - 2], stream[newest - 1], stream[newest], outFrac[(size_t) i]);
        }

        if (needPlay && character)
        {
            auto& noise = hiss[(size_t) t];
            for (int i = 0; i < n; ++i)
                play[i] += hissGain * noise.next();
        }
        if (wobble)
            wobblePlayback (t, play, 0, n);

        if (! armed)
        {
            std::memcpy (out, play, sizeof (float) * (size_t) n);
        }
        else if (punch)
        {
            // Auto-input, at the tape position being heard.
            int j = 3;
            for (int i = 0; i < n; ++i)
            {
                j += fetchCount[(size_t) i];
                const int heard = j - 2;
                const auto p = heard >= 4 ? fetchPos[(size_t) (heard - 4)] : startPos - (4 - heard);
                const float g = punchGain (p, punchIn, punchOut);
                out[i] = input[i] * g + play[i] * (1.0f - g);
            }
        }
        else
        {
            std::memcpy (out, input, sizeof (float) * (size_t) n);
        }

        if (armed && rec)
        {
            float* in = inScratch.data();   // [0, 4): history; [4, 4 + n): this block's input
            std::copy (v.inHistory.begin(), v.inHistory.end(), in);
            for (int i = 0; i < n; ++i)
                in[4 + i] = v.rec2.process (v.rec1.process (input[i]));
            std::copy (in + n, in + n + 4, v.inHistory.begin());

            float* vals = recScratch.data();
            for (int k = 0; k < K; ++k)
            {
                const float x = fetchTime[(size_t) k] + 2.0f;   // the input two samples before the crossing
                const int i0 = juce::jlimit (1, n + 1, (int) x);
                vals[k] = hermite (in[i0 - 1], in[i0], in[i0 + 1], in[i0 + 2], juce::jlimit (0.0f, 1.0f, x - (float) i0));
            }
            for (int k = 0; k < K;)
            {
                const int len = runLength (k);
                recordRun (t, fetchPos[(size_t) k] - recordShift, vals + k, len, punch, punchIn, punchOut, recordedEnd);
                k += len;
            }
        }

        std::copy (stream + K, stream + K + 4, stream);   // history for the next block

        if (character)
        {
            auto& flt = filters[(size_t) t];
            for (int i = 0; i < n; ++i)
                out[i] = flt.lowPass.process (flt.shelf.process (flt.bump.process (out[i])));
        }
    }

    if (wobble)
        wowWrite = (wowWrite + n) & (wowLineSize - 1);

    if (recordedEnd > 0)
    {
        auto len = length.load();
        while (recordedEnd > len && ! length.compare_exchange_weak (len, recordedEnd)) {}
    }
}

void TapeMachine::clearTrackFile (int track) noexcept
{
    writtenEnd[(size_t) track].store (0);
    trackCleared[(size_t) track].store (true);
}

bool TapeMachine::takeChunkChanged (int track, int chunk) noexcept
{
    auto& flag = chunkChanged[(size_t) track][(size_t) chunk];
    return flag.load (std::memory_order_relaxed) && flag.exchange (false, std::memory_order_acquire);
}

void TapeMachine::markChunkChanged (int track, int chunk) noexcept
{
    if (juce::isPositiveAndBelow (chunk, (int) maxChunks))
        chunkChanged[(size_t) track][(size_t) chunk].store (true, std::memory_order_release);
}

void TapeMachine::markFilesInStep() noexcept
{
    for (int t = 0; t < numTracks; ++t)
    {
        trackCleared[(size_t) t].store (false);
        for (size_t i = 0; i < maxChunks; ++i)
            chunkChanged[(size_t) t][i].store (false);
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
    if (recordedIps.load() <= 0.0f)
        recordedIps.store (ipsOf (juce::jlimit (0, 2, juce::roundToInt (speed->get()))));   // as if recorded at this speed
    ensureChunks (track, start, start + n - 1);
    write (track, start, data, n);
    trackRevision[(size_t) track].fetch_add (1);
}

float TapeMachine::punchGain (juce::int64 p, juce::int64 punchIn, juce::int64 punchOut) const noexcept
{
    if (p < punchIn || p >= punchOut)
        return 0.0f;

    // Fade length: punchFadeMs, but never more than half the region.
    const auto fade = juce::jmin ((juce::int64) std::round (punchFadeMs * 0.001 * sampleRate), (punchOut - punchIn) / 2);
    if (fade <= 0)
        return 1.0f;

    const auto edge = juce::jmin (p - punchIn, punchOut - 1 - p);   // samples from the nearer end of the region
    if (edge >= fade)
        return 1.0f;

    // Raised cosine: the old and new gains always add up to one.
    const float x = ((float) edge + 0.5f) / (float) fade;
    return 0.5f - 0.5f * std::cos (x * juce::MathConstants<float>::pi);
}
