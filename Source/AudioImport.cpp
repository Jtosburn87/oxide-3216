#include "AudioImport.h"

namespace
{
    constexpr int ioBlock = 32768;

    void registerFormats (juce::AudioFormatManager& m) { m.registerBasicFormats(); }
}

juce::String AudioImport::wildcard()
{
    juce::AudioFormatManager m;
    registerFormats (m);
    return m.getWildcardForAllFormats();
}

AudioImport::Info AudioImport::probe (const juce::File& file)
{
    Info info;
    juce::AudioFormatManager m;
    registerFormats (m);
    std::unique_ptr<juce::AudioFormatReader> reader (m.createReaderFor (file));
    if (reader == nullptr)
    {
        info.error = "\"" + file.getFileName() + "\" is not an audio file Oxide-3216 can read (WAV, AIFF, FLAC, Ogg, MP3).";
        return info;
    }
    if (reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
    {
        info.error = "\"" + file.getFileName() + "\" holds no audio.";
        return info;
    }
    info.ok = true;
    info.formatName = reader->getFormatName();
    info.channels = (int) reader->numChannels;
    info.sampleRate = reader->sampleRate;
    info.length = reader->lengthInSamples;
    return info;
}

AudioImport::Audio AudioImport::read (const juce::File& file, double targetRate, juce::int64 maxSamples,
                                      const std::function<bool (double)>& progress)
{
    Audio a;
    juce::AudioFormatManager m;
    registerFormats (m);
    auto* reader = m.createReaderFor (file);
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
    {
        delete reader;
        a.error = "Could not read \"" + file.getFileName() + "\".";
        return a;
    }

    const int numCh = juce::jmin (2, (int) reader->numChannels);
    const double ratio = reader->sampleRate / targetRate;   // file samples per tape sample
    const bool convert = std::abs (reader->sampleRate - targetRate) > 0.01;
    auto length = convert ? (juce::int64) std::ceil ((double) reader->lengthInSamples / ratio) : reader->lengthInSamples;
    if (length > maxSamples)
    {
        length = juce::jmax ((juce::int64) 0, maxSamples);
        a.truncated = true;
    }

    try
    {
        a.channels.assign ((size_t) numCh, std::vector<float> ((size_t) length));
    }
    catch (const std::bad_alloc&)
    {
        delete reader;
        a.channels.clear();
        a.error = "Not enough memory to import \"" + file.getFileName() + "\".";
        return a;
    }

    // Same rate: copied straight. Otherwise through JUCE's resampler, which low-passes when converting down.
    juce::AudioFormatReaderSource source (reader, true);
    juce::ResamplingAudioSource resampler (&source, false, numCh);
    if (convert)
    {
        resampler.setResamplingRatio (ratio);
        resampler.prepareToPlay (ioBlock, targetRate);
    }

    juce::AudioBuffer<float> block (numCh, ioBlock);
    for (juce::int64 done = 0; done < length;)
    {
        const int len = (int) juce::jmin ((juce::int64) ioBlock, length - done);
        if (convert)
        {
            juce::AudioSourceChannelInfo info (&block, 0, len);
            resampler.getNextAudioBlock (info);
        }
        else
        {
            reader->read (&block, 0, len, done, true, numCh > 1);
        }
        for (int ch = 0; ch < numCh; ++ch)
            std::memcpy (a.channels[(size_t) ch].data() + done, block.getReadPointer (ch), sizeof (float) * (size_t) len);
        done += len;

        if (progress && ! progress ((double) done / (double) length))
        {
            a.channels.clear();
            a.error = "Import cancelled.";
            return a;
        }
    }

    if (convert)
        resampler.releaseResources();
    a.ok = true;
    return a;
}

void AudioImport::place (TapeMachine& tape, const Audio& a, int firstTrack, juce::int64 start)
{
    jassert (! tape.isPlaying());
    const auto n = a.length();
    for (size_t ch = 0; ch < a.channels.size(); ++ch)
    {
        const int track = firstTrack + (int) ch;
        if (! juce::isPositiveAndBelow (track, Config::numTracks))
            break;
        const float* data = a.channels[ch].data();
        for (juce::int64 done = 0; done < n;)
        {
            const int len = (int) juce::jmin ((juce::int64) ioBlock, n - done);
            tape.writeTrack (track, start + done, data + done, len);
            done += len;
        }
    }
    if (start + n > tape.getLength())
        tape.setLength (start + n);
}
