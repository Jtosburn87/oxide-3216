#pragma once

#include "TapeMachine.h"
#include <juce_audio_formats/juce_audio_formats.h>

/** Importing an audio file onto the tape: a mono file takes one track, a stereo file two
    neighbouring tracks (left on the first). Files with more channels contribute their first two. */
namespace AudioImport
{
    struct Info
    {
        bool ok = false;
        juce::String error, formatName;
        int channels = 0;           // channels in the file
        double sampleRate = 0.0;
        juce::int64 length = 0;     // samples at the file's own rate

        int tracksNeeded() const noexcept { return channels >= 2 ? 2 : 1; }
    };

    /** File chooser pattern for every readable format. */
    juce::String wildcard();

    /** Reads the file's header. Any thread. */
    Info probe (const juce::File&);

    struct Audio
    {
        bool ok = false;
        juce::String error;
        std::vector<std::vector<float>> channels;   // one or two, at the tape's sample rate
        bool truncated = false;                     // did not fit on the tape

        juce::int64 length() const noexcept { return channels.empty() ? 0 : (juce::int64) channels[0].size(); }
    };

    /** Decodes the file at targetRate (converted if the file's rate differs), at most maxSamples long.
        progress gets 0..1 and returns false to cancel. Any thread. */
    Audio read (const juce::File&, double targetRate, juce::int64 maxSamples, const std::function<bool (double)>& progress);

    /** Writes the audio onto firstTrack (and a stereo file's right channel onto secondTrack: -1 = firstTrack + 1)
        from start, replacing what was there in that stretch, and lengthens the tape if needed. Message thread,
        transport stopped. */
    void place (TapeMachine&, const Audio&, int firstTrack, juce::int64 start, int secondTrack = -1);
}
