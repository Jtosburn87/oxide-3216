#include "Session.h"
#include "PluginManager.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr int formatVersion = 1;
    constexpr int ioBlock = 65536;

    void forEachChain (AudioEngine& e, const std::function<void (InsertChain&)>& fn)
    {
        for (auto* c : e.subInserts) fn (*c);
        for (auto* c : e.auxInserts) fn (*c);
        fn (e.masterInserts);
    }

    void forEachStrip (AudioEngine& e, const std::function<void (ChannelStrip&)>& fn)
    {
        for (auto* s : e.inputStrips)  fn (*s);
        for (auto* s : e.returnStrips) fn (*s);
    }

    juce::String defaultStripName (const ChannelStrip& s)
    {
        const bool input = s.idPrefix.startsWith ("ch");
        return (input ? "Input " : "Return ") + s.idPrefix.substring (2);
    }

    bool writeTrack (TapeMachine& tape, int track, juce::int64 length, const juce::File& file, juce::String& error)
    {
        file.deleteFile();
        auto stream = file.createOutputStream();
        if (stream == nullptr)
        {
            error = "Could not write " + file.getFullPathName();
            return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), tape.getSampleRate(), 1, 32, {}, 0));
        if (writer == nullptr)
        {
            error = "Could not create " + file.getFullPathName();
            return false;
        }
        stream.release();   // the writer owns it now

        std::vector<float> block ((size_t) ioBlock);
        for (juce::int64 done = 0; done < length;)
        {
            const int n = (int) juce::jmin ((juce::int64) ioBlock, length - done);
            tape.readTrack (track, done, block.data(), n);
            const float* channels[] = { block.data() };
            if (! writer->writeFromFloatArrays (channels, 1, n))
            {
                error = "Disk full or not writable: " + file.getFullPathName();
                return false;
            }
            done += n;
        }
        return true;
    }
}

juce::String Session::trackFileName (int track)
{
    return "Track " + juce::String (track + 1).paddedLeft ('0', 2) + ".wav";
}

//==============================================================================
Session::Result Session::save (AudioEngine& e, const juce::File& folder, TrackRevisions* written)
{
    Result r;
    if (! folder.createDirectory())
    {
        r.ok = false;
        r.error = "Could not create the folder " + folder.getFullPathName();
        return r;
    }

    auto& tape = e.tape;
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "ConsoleTape session");
    root->setProperty ("version", formatVersion);
    root->setProperty ("projectName", e.projectName.toString());
    root->setProperty ("savedAt", juce::Time::getCurrentTime().toISO8601 (true));

    auto* controls = new juce::DynamicObject();
    for (auto* p : e.params.all())
        controls->setProperty (p->id, p->get());
    root->setProperty ("controls", juce::var (controls));

    auto* names = new juce::DynamicObject();
    forEachStrip (e, [&] (ChannelStrip& s) { names->setProperty (s.idPrefix, s.name); });
    root->setProperty ("stripNames", juce::var (names));

    juce::Array<juce::var> cables;
    for (int d = 0; d < AudioEngine::Patch::numDestinations; ++d)
        for (int s = 0; s < AudioEngine::Patch::numSources; ++s)
            if ((e.getPatch (d) >> s) & 1u)
                cables.add (juce::Array<juce::var> { s, d });
    root->setProperty ("patch", cables);

    auto* inserts = new juce::DynamicObject();
    forEachChain (e, [&] (InsertChain& c)
    {
        if (c.numLoaded() > 0)
            inserts->setProperty (c.name, c.toXml()->toString (juce::XmlElement::TextFormat().singleLine()));
    });
    root->setProperty ("inserts", juce::var (inserts));

    // Tape: one WAV per track that holds audio; tracks unchanged since the last save here are kept.
    const auto length = tape.getLength();
    auto* t = new juce::DynamicObject();
    t->setProperty ("sampleRate", tape.getSampleRate());
    t->setProperty ("length", length);
    t->setProperty ("position", tape.getPosition());
    t->setProperty ("in", tape.getInPoint());
    t->setProperty ("out", tape.getOutPoint());
    t->setProperty ("loop", tape.loopOn.load());
    t->setProperty ("punch", tape.punchOn.load());

    juce::Array<juce::var> files;
    for (int track = 0; track < Config::numTracks; ++track)
    {
        const auto file = folder.getChildFile (trackFileName (track));
        if (! tape.trackHasAudio (track) || length <= 0)
        {
            file.deleteFile();
            files.add (juce::var());
            continue;
        }

        const auto revision = tape.getTrackRevision (track);
        const bool unchanged = written != nullptr && (*written)[(size_t) track] == revision && file.existsAsFile();
        if (! unchanged)
        {
            juce::String error;
            if (! writeTrack (tape, track, length, file, error))
            {
                r.ok = false;
                r.error = error;
                return r;
            }
            if (written != nullptr)
                (*written)[(size_t) track] = revision;
        }
        files.add (file.getFileName());
    }
    t->setProperty ("tracks", files);
    root->setProperty ("tape", juce::var (t));

    // Write the JSON last, via a temporary file, so a crash mid-save never leaves a broken session.json.
    const auto json = folder.getChildFile (fileName);
    juce::TemporaryFile temp (json);
    if (! temp.getFile().replaceWithText (juce::JSON::toString (juce::var (root))) || ! temp.overwriteTargetFileWithTemporary())
    {
        r.ok = false;
        r.error = "Could not write " + json.getFullPathName();
    }
    return r;
}

//==============================================================================
void Session::reset (AudioEngine& e)
{
    e.tape.stop();
    e.tape.setRecordEnabled (false);
    for (auto* p : e.params.all())
        p->reset();
    forEachStrip (e, [] (ChannelStrip& s) { s.name = defaultStripName (s); });
    e.clearPatch();
    forEachChain (e, [] (InsertChain& c) { c.clearAll(); });
    e.tape.eraseAll();
    e.tape.setInPoint (0);
    e.tape.setOutPoint (0);
    e.tape.loopOn = false;
    e.tape.punchOn = false;
    e.projectName = "Untitled song";
}

Session::Result Session::load (AudioEngine& e, PluginManager* plugins, const juce::File& folder, TrackRevisions* written)
{
    Result r;
    const auto json = juce::JSON::parse (folder.getChildFile (fileName));
    if (! json.isObject() || json["format"].toString() != "ConsoleTape session")
    {
        r.ok = false;
        r.error = folder.getChildFile (fileName).getFullPathName() + " is not a ConsoleTape session.";
        return r;
    }

    reset (e);
    e.projectName = json["projectName"].toString();

    if (auto* controls = json["controls"].getDynamicObject())
        for (auto& prop : controls->getProperties())
            if (auto* p = e.params.find (prop.name.toString()))
                p->set ((float) (double) prop.value);

    if (auto* names = json["stripNames"].getDynamicObject())
        forEachStrip (e, [&] (ChannelStrip& s)
        {
            if (names->hasProperty (s.idPrefix))
                s.name = names->getProperty (s.idPrefix).toString();
        });

    if (auto* cables = json["patch"].getArray())
        for (auto& c : *cables)
            if (c.isArray() && c.size() == 2)
            {
                const int s = c[0], d = c[1];
                if (juce::isPositiveAndBelow (s, AudioEngine::Patch::numSources))
                    e.setPatch (d, e.getPatch (d) | (std::uint64_t (1) << s));
            }

    auto* device = e.getDeviceManager().getCurrentAudioDevice();
    const double deviceRate = device != nullptr ? device->getCurrentSampleRate() : e.tape.getSampleRate();

    if (auto* inserts = json["inserts"].getDynamicObject())
    {
        forEachChain (e, [&] (InsertChain& c)
        {
            if (! inserts->hasProperty (c.name))
                return;
            if (plugins == nullptr)
            {
                r.warnings.add ("Plugins on " + c.name + " were not loaded.");
                return;
            }
            if (auto xml = juce::parseXML (inserts->getProperty (c.name).toString()))
                for (auto& missing : plugins->restore (c, *xml, deviceRate, Config::maxBlock))
                    r.warnings.add ("Missing plugin: " + missing);
        });
    }

    // Tape.
    const auto tapeInfo = json["tape"];
    const double sessionRate = tapeInfo["sampleRate"];
    if (sessionRate > 0 && std::abs (sessionRate - deviceRate) > 0.5)
        r.warnings.add ("This session was recorded at " + juce::String ((int) sessionRate) + " Hz but the audio interface runs at "
                        + juce::String ((int) deviceRate) + " Hz, so the tape plays at the wrong speed. Change the sample rate in Audio device.");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::int64 longest = 0;
    if (auto* files = tapeInfo["tracks"].getArray())
    {
        for (int track = 0; track < juce::jmin (files->size(), Config::numTracks); ++track)
        {
            const auto name = (*files)[track].toString();
            if (name.isEmpty())
                continue;
            const auto file = folder.getChildFile (name);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr)
            {
                r.warnings.add ("Missing or unreadable track file: " + name);
                continue;
            }

            std::vector<float> block ((size_t) ioBlock);
            const auto n = reader->lengthInSamples;
            for (juce::int64 done = 0; done < n;)
            {
                const int len = (int) juce::jmin ((juce::int64) ioBlock, n - done);
                float* channels[] = { block.data() };
                reader->read (channels, 1, done, len);
                e.tape.writeTrack (track, done, block.data(), len);
                done += len;
            }
            longest = juce::jmax (longest, n);
            if (written != nullptr)
                (*written)[(size_t) track] = e.tape.getTrackRevision (track);
        }
    }

    e.tape.setLength (juce::jmax ((juce::int64) tapeInfo["length"], longest));
    e.tape.setInPoint ((juce::int64) tapeInfo["in"]);
    e.tape.setOutPoint ((juce::int64) tapeInfo["out"]);
    e.tape.loopOn = (bool) tapeInfo["loop"];
    e.tape.punchOn = (bool) tapeInfo["punch"];
    e.tape.locate ((juce::int64) tapeInfo["position"]);
    return r;
}

//==============================================================================
Session::Result Session::exportMix (AudioEngine& e, const juce::File& file, double tailSeconds, const std::function<bool (double)>& progress)
{
    Result r;
    auto& tape = e.tape;
    const double sr = tape.getSampleRate();
    const auto length = tape.getLength();
    if (length <= 0)
    {
        r.ok = false;
        r.error = "The tape is empty: there is nothing to export.";
        return r;
    }

    file.deleteFile();
    auto stream = file.createOutputStream();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0) : nullptr);
    if (writer == nullptr)
    {
        r.ok = false;
        r.error = "Could not write " + file.getFullPathName();
        return r;
    }
    stream.release();

    // Play every track back (armed tracks too, nothing recorded, no loop, no test tone). No Params are touched,
    // so this is safe on a worker thread.
    const bool wasLoop = tape.loopOn.load(), wasTone = e.testToneOn.load();
    const auto wasPosition = tape.getPosition();
    tape.exporting = true;
    tape.loopOn = false;
    e.testToneOn = false;
    forEachChain (e, [] (InsertChain& c) { for (int s = 0; s < InsertChain::numSlots; ++s) if (auto* p = c.getProcessor (s)) p->setNonRealtime (true); });

    tape.locate (0);
    tape.play();

    const auto total = length + (juce::int64) (tailSeconds * sr);
    std::vector<float> left ((size_t) Config::maxBlock), right ((size_t) Config::maxBlock);
    bool cancelled = false;
    for (juce::int64 done = 0; done < total;)
    {
        const int n = (int) juce::jmin ((juce::int64) Config::maxBlock, total - done);
        e.renderOffline (left.data(), right.data(), n);
        const float* channels[] = { left.data(), right.data() };
        writer->writeFromFloatArrays (channels, 2, n);
        done += n;
        if (progress && (done % (Config::maxBlock * 64) == 0 || done == total) && ! progress ((double) done / (double) total))
        {
            cancelled = true;
            break;
        }
    }
    writer.reset();

    tape.stop();
    forEachChain (e, [] (InsertChain& c) { for (int s = 0; s < InsertChain::numSlots; ++s) if (auto* p = c.getProcessor (s)) p->setNonRealtime (false); });
    tape.exporting = false;
    tape.loopOn = wasLoop;
    e.testToneOn = wasTone;
    tape.locate (wasPosition);

    if (cancelled)
    {
        file.deleteFile();
        r.ok = false;
        r.error = "Export cancelled.";
    }
    return r;
}
