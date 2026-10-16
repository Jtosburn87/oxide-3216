#pragma once

#include "AudioEngine.h"

class PluginManager;

/** Sessions are folders:
        session.json      every control, strip names, patch cables, plugins and their state,
                          the project name, tape length and locators
        Track 01.wav ...  one 32-bit float WAV per tape track that has audio

    Everything here runs on the message thread, except exportMix which may run on a worker
    thread while the audio device is not calling the engine. */
namespace Session
{
    struct Result
    {
        bool ok = true;
        juce::String error;
        juce::StringArray warnings;   // e.g. missing plugins, missing track files, sample-rate mismatch
    };

    /** Per-track revision last written to a folder, so unchanged tracks are not rewritten. */
    using TrackRevisions = std::array<std::uint32_t, Config::numTracks>;

    static constexpr const char* fileName = "session.json";

    /** Everything: session.json plus the track files. */
    Result save (AudioEngine&, const juce::File& folder, TrackRevisions* written = nullptr);
    /** session.json only (the app's TapeFiles keeps the track files up to date as the tape changes). */
    Result saveSettings (AudioEngine&, const juce::File& folder);
    Result load (AudioEngine&, PluginManager*, const juce::File& folder, TrackRevisions* written = nullptr);

    /** Back to an empty console: default controls, no cables, no plugins, blank tape. */
    void reset (AudioEngine&);

    /** Renders the main bus (post limiter) from start to end on the tape (in samples) plus a tail, to a
        24-bit stereo WAV, creating its folder if needed. progress gets 0..1 and returns false to cancel. */
    Result exportMix (AudioEngine&, const juce::File& wav, juce::int64 start, juce::int64 end, double tailSeconds,
                      const std::function<bool (double)>& progress);

    /** Export From Tape: each listed tape track (0-based), from start to end (tape samples), straight off the tape into its
        own mono 24-bit WAV in folder, named "<prefix> Track 01.wav" (plus the return strip's name if it was renamed).
        Reads under the tape's storage lock, so it runs on a worker thread while audio plays. progress: 0..1, false cancels.
        written gets the files made. */
    Result exportTapeTracks (AudioEngine&, const juce::File& folder, const juce::String& prefix, const juce::Array<int>& tracks,
                             juce::int64 start, juce::int64 end, const std::function<bool (double)>& progress,
                             juce::Array<juce::File>* written = nullptr);
    juce::String tapeExportName (AudioEngine&, const juce::String& prefix, int track);

    /** Exports go in this subfolder of the session folder. */
    static constexpr const char* bounceFolderName = "Bounced Audio";
    juce::File bounceFolder (const juce::File& sessionFolder);

    juce::String trackFileName (int track);
}
