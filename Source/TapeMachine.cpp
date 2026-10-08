#include "TapeMachine.h"

namespace
{
    constexpr double allocateAheadSeconds = 20.0;
}

TapeMachine::TapeMachine (ParamSet& params)
    : maxChunks ((size_t) std::ceil (Config::maxSampleRate * 60.0 * Config::maxTapeMinutes / chunkSize))
{
    recordLevel = &params.add ("tape.level", "Record level", -12.0f, 18.0f, 0.0f);
    for (int t = 0; t < numTracks; ++t)
        arm[(size_t) t] = &params.addToggle ("tape.arm" + juce::String (t + 1), "Arm " + juce::String (t + 1), false);

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
            ensureChunks (t, p - latency.load() - chunkSize, p + (juce::int64) (allocateAheadSeconds * sampleRate));
    position.store (p);
    locateRequest.store (p);
}

void TapeMachine::eraseAll()
{
    stop();
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
            ensureChunks (t, p - latency.load() - chunkSize, p + (juce::int64) (allocateAheadSeconds * sampleRate));

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
    {
        done = (int) juce::jmin ((juce::int64) n, -start);
    }

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

void TapeMachine::process (const float* const* tapeIn, float* const* tapeOut, int n) noexcept
{
    jassert (n <= Config::maxBlock);
    blocksProcessed.fetch_add (1);

    const auto req = locateRequest.exchange (-1);
    if (req >= 0)
        pos = req;

    const bool isPlay = playing.load();
    const bool rec = recordEnabled.load();
    const int lat = latency.load();

    // Record level, smoothed, shared by all tracks.
    const float levelT = Dsp::dbToGain (recordLevel->get());
    for (int i = 0; i < n; ++i)
        scratch[(size_t) i] = levelGain.next (levelT);

    bool anyRecording = false;
    for (int t = 0; t < numTracks; ++t)
    {
        float* out = tapeOut[t];
        const float* in = tapeIn[t];

        if (arm[(size_t) t]->on())
        {
            // Armed: monitor the input (and record it when rolling in record).
            for (int i = 0; i < n; ++i)
                out[i] = in != nullptr ? in[i] * scratch[(size_t) i] : 0.0f;

            if (isPlay && rec)
            {
                write (t, pos - lat, out, n);
                anyRecording = true;
            }
        }
        else if (isPlay)
        {
            readTrack (t, pos, out, n);
        }
        else
        {
            std::memset (out, 0, sizeof (float) * (size_t) n);
        }
    }

    if (isPlay)
    {
        if (anyRecording)
        {
            const auto end = pos + n - lat;
            auto len = length.load();
            while (end > len && ! length.compare_exchange_weak (len, end)) {}
        }

        pos += n;

        const bool endOfTape = pos >= (juce::int64) maxChunks * chunkSize - Config::maxBlock;
        if (endOfTape || (! rec && pos >= length.load()))
            playing.store (false);
    }

    position.store (pos);
}
