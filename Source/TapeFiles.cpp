#include "TapeFiles.h"
#include "Session.h"

namespace
{
    constexpr int headerBytes = 44;

    /** A plain mono 32-bit float WAV header (format 3, no extra chunks): the audio starts at byte 44. */
    void writeHeader (juce::OutputStream& out, int sampleRate, juce::int64 samples)
    {
        const auto bytes = (int) (samples * 4);
        out.write ("RIFF", 4);
        out.writeInt (headerBytes - 8 + bytes);
        out.write ("WAVE", 4);
        out.write ("fmt ", 4);
        out.writeInt (16);
        out.writeShort (3);   // IEEE float
        out.writeShort (1);
        out.writeInt (sampleRate);
        out.writeInt (sampleRate * 4);
        out.writeShort (4);
        out.writeShort (32);
        out.write ("data", 4);
        out.writeInt (bytes);
    }

    juce::String readId (juce::InputStream& in)
    {
        char id[4] {};
        return in.read (id, 4) == 4 ? juce::String (id, 4) : juce::String();
    }
}

TapeFiles::TapeFiles (TapeMachine& t) : juce::Thread ("Tape files"), tape (t)
{
    buffer.resize ((size_t) TapeMachine::chunkSize);
    startThread();
}

TapeFiles::~TapeFiles()
{
    stopThread (5000);
}

void TapeFiles::setFolder (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    folder = f;
    info = {};
}

juce::String TapeFiles::flush()
{
    const juce::ScopedLock sl (lock);
    return writePending();
}

juce::String TapeFiles::takeError()
{
    const juce::ScopedLock sl (lock);
    return std::exchange (error, {});
}

void TapeFiles::run()
{
    juce::String lastError;
    while (! threadShouldExit())
    {
        wait (500);
        const juce::ScopedLock sl (lock);
        const auto e = writePending();
        if (e.isNotEmpty() && e != lastError)   // report a lasting problem (disk full) once, not twice a second
            error = e;
        lastError = e;
    }
}

juce::String TapeFiles::writePending()
{
    if (folder == juce::File())
        return {};
    if (! folder.createDirectory())
        return "Could not create the folder " + folder.getFullPathName();

    juce::String firstError;
    for (int t = 0; t < TapeMachine::numTracks; ++t)
        if (const auto e = writeTrack (t); firstError.isEmpty())
            firstError = e;
    return firstError;
}

void TapeFiles::readChunk (int track, juce::int64 start, int n)
{
    const juce::ScopedLock sl (tape.getStorageLock());
    tape.readTrack (track, start, buffer.data(), n);
}

juce::String TapeFiles::writeTrack (int t)
{
    const auto file = folder.getChildFile (Session::trackFileName (t));
    auto& fi = info[(size_t) t];

    if (tape.takeTrackCleared (t))
    {
        file.deleteFile();
        fi = {};
    }

    const auto end = tape.getWrittenEnd (t);
    const int numChunks = (int) ((end + TapeMachine::chunkSize - 1) >> TapeMachine::chunkBits);
    std::vector<int> changed;
    for (int c = 0; c < numChunks; ++c)
        if (tape.takeChunkChanged (t, c))
            changed.push_back (c);
    if (changed.empty())
        return {};

    const auto putBack = [&] { for (auto c : changed) tape.markChunkChanged (t, c); };

    if (! fi.known && ! openLayout (t, file))
    {
        if (file.existsAsFile())   // a layout we can't patch: write it whole once, in our own layout
        {
            const auto e = rewholeTrack (t, file);
            if (e.isNotEmpty())
                putBack();
            return e;
        }

        juce::FileOutputStream out (file);
        if (out.failedToOpen())
        {
            putBack();
            return "Could not write " + file.getFullPathName();
        }
        writeHeader (out, (int) tape.getSampleRate(), 0);
        fi = { true, headerBytes, 0 };
    }

    juce::FileOutputStream out (file);   // an existing file is opened for writing in place, not emptied
    if (out.failedToOpen())
    {
        putBack();
        return "Could not write " + file.getFullPathName();
    }

    const auto oldSamples = fi.samples;
    bool ok = true;
    for (auto c : changed)
    {
        const auto s0 = (juce::int64) c << TapeMachine::chunkBits;
        const auto s1 = juce::jmin (s0 + TapeMachine::chunkSize, end);
        if (s1 <= s0)
            continue;

        if (s0 > fi.samples)   // the gap between the old end of the file and this chunk is silence
        {
            ok = ok && out.setPosition (fi.dataOffset + fi.samples * 4);
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (auto p = fi.samples; ok && p < s0;)
            {
                const auto n = (int) juce::jmin ((juce::int64) TapeMachine::chunkSize, s0 - p);
                ok = out.write (buffer.data(), (size_t) n * 4);
                p += n;
            }
        }

        readChunk (t, s0, (int) (s1 - s0));
        ok = ok && out.setPosition (fi.dataOffset + s0 * 4)
                && out.write (buffer.data(), (size_t) (s1 - s0) * 4);
        if (! ok)
            break;
        fi.samples = juce::jmax (fi.samples, s1);
    }

    if (ok && fi.samples != oldSamples)
    {
        ok = out.setPosition (fi.dataOffset - 4) && out.writeInt ((int) (fi.samples * 4))
          && out.setPosition (4) && out.writeInt ((int) (fi.dataOffset - 8 + fi.samples * 4));
    }
    out.flush();

    if (! ok || out.getStatus().failed())
    {
        putBack();
        fi = {};   // read the layout afresh next time
        return "Disk full or not writable: " + file.getFullPathName();
    }
    return {};
}

bool TapeFiles::openLayout (int t, const juce::File& file)
{
    juce::FileInputStream in (file);
    if (in.failedToOpen() || readId (in) != "RIFF")
        return false;
    in.readInt();
    if (readId (in) != "WAVE")
        return false;

    bool floatMono32 = false;
    while (! in.isExhausted())
    {
        const auto id = readId (in);
        const auto size = (juce::int64) (juce::uint32) in.readInt();
        const auto pos = in.getPosition();

        if (id == "fmt ")
        {
            int tag = (juce::uint16) in.readShort();
            const int channels = in.readShort();
            in.readInt();
            in.readInt();
            in.readShort();
            const int bits = in.readShort();
            if (tag == 0xfffe && size >= 40)   // WAVE_FORMAT_EXTENSIBLE: the real format leads the sub-format GUID
            {
                in.setPosition (pos + 24);
                tag = (juce::uint16) in.readShort();
            }
            floatMono32 = tag == 3 && channels == 1 && bits == 32;
        }
        else if (id == "data")
        {
            // Only patchable if the audio runs to the end of the file (so it can grow).
            if (! floatMono32 || pos + size != file.getSize())
                return false;
            info[(size_t) t] = { true, pos, size / 4 };
            return true;
        }
        if (id.isEmpty() || ! in.setPosition (pos + size + (size & 1)))
            break;
    }
    return false;
}

juce::String TapeFiles::rewholeTrack (int t, const juce::File& file)
{
    const auto end = tape.getWrittenEnd (t);
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (out.failedToOpen())
            return "Could not write " + file.getFullPathName();
        writeHeader (out, (int) tape.getSampleRate(), end);
        for (juce::int64 p = 0; p < end;)
        {
            const auto n = (int) juce::jmin ((juce::int64) TapeMachine::chunkSize, end - p);
            readChunk (t, p, n);
            if (! out.write (buffer.data(), (size_t) n * 4))
                return "Disk full or not writable: " + file.getFullPathName();
            p += n;
        }
        out.flush();
        if (out.getStatus().failed())
            return "Disk full or not writable: " + file.getFullPathName();
    }
    if (! temp.overwriteTargetFileWithTemporary())
        return "Could not replace " + file.getFullPathName();
    info[(size_t) t] = { true, headerBytes, end };
    return {};
}
