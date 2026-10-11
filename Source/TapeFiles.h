#pragma once

#include "TapeMachine.h"

/** Keeps a session folder's track files ("Track 01.wav" ...) in step with the tape, like real tape:
    whatever is recorded, imported or erased goes to disk by itself within half a second. A worker thread
    does the writing and only rewrites the changed parts (chunks of TapeMachine::chunkSize samples) of
    each file, so punching in on a long track writes a few hundred kilobytes, not the whole track.

    Files are mono 32-bit float WAVs. One whose layout can't be patched in place (another format, or
    chunks after the audio) is rewritten whole from the tape the first time it changes. */
class TapeFiles : private juce::Thread
{
public:
    explicit TapeFiles (TapeMachine&);
    ~TapeFiles() override;

    /** Where the files go; empty = nowhere (changes wait for a folder). Message thread.
        Call flush() first so nothing meant for the old folder is left over. */
    void setFolder (const juce::File&);

    /** Writes every pending change now. Returns an error message, or empty. Message thread. */
    juce::String flush();

    /** The last error the worker thread ran into (e.g. disk full), once. */
    juce::String takeError();

private:
    TapeMachine& tape;
    juce::CriticalSection lock;   // one writer at a time; guards everything below
    juce::File folder;
    juce::String error;

    struct FileInfo { bool known = false; juce::int64 dataOffset = 0, samples = 0; };
    std::array<FileInfo, TapeMachine::numTracks> info {};
    std::vector<float> buffer;

    void run() override;
    juce::String writePending();
    juce::String writeTrack (int track);
    bool openLayout (int track, const juce::File&);
    juce::String rewholeTrack (int track, const juce::File&);
    void readChunk (int track, juce::int64 start, int n);
};
