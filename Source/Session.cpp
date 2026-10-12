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

namespace Session
{
    Result write (AudioEngine&, const juce::File& folder, TrackRevisions* written, bool tracks);
}

juce::File Session::bounceFolder (const juce::File& sessionFolder)
{
    return sessionFolder.getChildFile (bounceFolderName);
}

juce::String Session::trackFileName (int track)
{
    return "Track " + juce::String (track + 1).paddedLeft ('0', 2) + ".wav";
}

//==============================================================================
Session::Result Session::save (AudioEngine& e, const juce::File& folder, TrackRevisions* written)
{
    return write (e, folder, written, true);
}

Session::Result Session::saveSettings (AudioEngine& e, const juce::File& folder)
{
    return write (e, folder, nullptr, false);
}

Session::Result Session::write (AudioEngine& e, const juce::File& folder, TrackRevisions* written, bool tracks)
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
    root->setProperty ("format", "Oxide-3216 session");
    root->setProperty ("version", formatVersion);
    root->setProperty ("projectName", e.projectName.toString());
    root->setProperty ("savedAt", juce::Time::getCurrentTime().toISO8601 (true));

    auto* controls = new juce::DynamicObject();
    for (auto* p : e.params.all())
        controls->setProperty (p->id, p->get());
    root->setProperty ("controls", juce::var (controls));
    root->setProperty ("scenes", e.scenes.toVar());

    auto* names = new juce::DynamicObject();
    forEachStrip (e, [&] (ChannelStrip& s) { names->setProperty (s.idPrefix, s.name); });
    root->setProperty ("stripNames", juce::var (names));

    auto* busNames = new juce::DynamicObject();
    for (int k = 0; k < Config::numSubs; ++k) busNames->setProperty ("sub" + juce::String (k + 1), e.subNames[(size_t) k]);
    for (int k = 0; k < Config::numAux; ++k)  busNames->setProperty ("aux" + juce::String (k + 1), e.auxNames[(size_t) k]);
    root->setProperty ("busNames", juce::var (busNames));

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
    t->setProperty ("recordedIps", tape.getRecordedIps());
    juce::Array<juce::var> memories;
    for (int m = 0; m < TapeMachine::numMemories; ++m)
        memories.add (tape.getMemory (m));
    t->setProperty ("memories", memories);

    juce::Array<juce::var> files;
    for (int track = 0; track < Config::numTracks; ++track)
    {
        const auto file = folder.getChildFile (trackFileName (track));
        if (! tracks)   // TapeFiles keeps the track files up to date
        {
            files.add (tape.trackHasAudio (track) && length > 0 ? juce::var (file.getFileName()) : juce::var());
            continue;
        }
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
    e.resetBusNames();
    e.clearPatch();
    forEachChain (e, [] (InsertChain& c) { c.clearAll(); });
    e.tape.eraseAll();
    e.tape.setInPoint (0);
    e.tape.setOutPoint (0);
    e.tape.loopOn = false;
    e.tape.punchOn = false;
    for (int m = 0; m < TapeMachine::numMemories; ++m)
        e.tape.setMemory (m, -1);
    e.projectName = "Untitled song";
    e.scenes.clear();
}

Session::Result Session::load (AudioEngine& e, PluginManager* plugins, const juce::File& folder, TrackRevisions* written)
{
    Result r;
    const auto json = juce::JSON::parse (folder.getChildFile (fileName));
    const auto format = json["format"].toString();
    if (! json.isObject() || (format != "Oxide-3216 session" && format != "ConsoleTape session"))   // the app was called ConsoleTape before
    {
        r.ok = false;
        r.error = folder.getChildFile (fileName).getFullPathName() + " is not an Oxide-3216 session.";
        return r;
    }

    reset (e);
    e.projectName = json["projectName"].toString();

    // The console colour switch left the Options menu in 0.1.1, and the talkback dim switch left Talkback
    // Settings after 0.1.3: sessions saved with either off load with it on.
    if (auto* controls = json["controls"].getDynamicObject())
        for (auto& prop : controls->getProperties())
            if (auto* p = e.params.find (prop.name.toString()); p != nullptr && p != e.preampColour && p != e.tbDim)
                p->set ((float) (double) prop.value);

    // Sessions from before the compressor types (0.1.1) were made with the 76: they keep it.
    // 0.1.0 sessions gave the 76 Threshold and Make-up knobs instead of Input (0..10, 3 dB a step into a
    // fixed -14 dBFS threshold) and Output: back to the same threshold and overall gain. (Older sessions
    // have Input and Output already.)
    if (auto* controls = json["controls"].getDynamicObject())
        forEachStrip (e, [&] (ChannelStrip& s)
        {
            // Before 0.1.6 the preamp gain knob was called Trim (saved as "<strip>.trim"), with no trim after it.
            if (! controls->hasProperty (s.controls.gain->id) && controls->hasProperty (s.controls.trim->id))
            {
                s.controls.gain->set ((float) (double) controls->getProperty (s.controls.trim->id));
                s.controls.trim->set (0.0f);
            }
            if (! controls->hasProperty (s.comp.type->id))
                s.comp.type->set ((float) ChannelCompressor::fet76);
            // Gates default to out since 0.1.2; sessions from before the gate In switch had them in.
            if (! controls->hasProperty (s.controls.gateIn->id))
                s.controls.gateIn->set (1.0f);

            const auto thr = s.idPrefix + ".comp.thr", makeup = s.idPrefix + ".comp.makeup";
            if (! controls->hasProperty (thr) || controls->hasProperty (s.comp.fet.input->id))
                return;
            const float drive = juce::jlimit (0.0f, 30.0f, -14.0f - (float) (double) controls->getProperty (thr));
            s.comp.fet.input->set (drive / 3.0f);
            s.comp.fet.output->set ((float) (double) controls->getProperty (makeup) - drive);
        });

    if (auto* names = json["stripNames"].getDynamicObject())
        forEachStrip (e, [&] (ChannelStrip& s)
        {
            if (names->hasProperty (s.idPrefix))
                s.name = names->getProperty (s.idPrefix).toString();
        });

    if (auto* busNames = json["busNames"].getDynamicObject())
    {
        for (int k = 0; k < Config::numSubs; ++k)
            if (busNames->hasProperty ("sub" + juce::String (k + 1)))
                e.subNames[(size_t) k] = busNames->getProperty ("sub" + juce::String (k + 1)).toString();
        for (int k = 0; k < Config::numAux; ++k)
            if (busNames->hasProperty ("aux" + juce::String (k + 1)))
                e.auxNames[(size_t) k] = busNames->getProperty ("aux" + juce::String (k + 1)).toString();
    }

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

    // Sessions from before the aux main assigns: an aux return reached main whenever it had a plugin on it.
    if (auto* controls = json["controls"].getDynamicObject())
        for (int k = 0; k < Config::numAux; ++k)
            if (! controls->hasProperty (e.auxMain[(size_t) k]->id) && e.auxInserts[k]->numLoaded() > 0)
                e.auxMain[(size_t) k]->set (1.0f);

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

    // The speed the tape was recorded at. Sessions from before vari-speed were recorded at their speed setting,
    // which loading the tracks has already set (the controls, speed included, load first).
    if ((double) tapeInfo["recordedIps"] > 0.0)
        e.tape.setRecordedIps ((float) (double) tapeInfo["recordedIps"]);

    e.tape.setLength (juce::jmax ((juce::int64) tapeInfo["length"], longest));
    e.tape.setInPoint ((juce::int64) tapeInfo["in"]);
    e.tape.setOutPoint ((juce::int64) tapeInfo["out"]);
    e.tape.loopOn = (bool) tapeInfo["loop"];
    e.tape.punchOn = (bool) tapeInfo["punch"];
    if (auto* memories = tapeInfo["memories"].getArray())   // locate memories (sessions from 0.1.4 on)
        for (int m = 0; m < juce::jmin (memories->size(), TapeMachine::numMemories); ++m)
            e.tape.setMemory (m, (juce::int64) (*memories)[m]);
    e.scenes.fromVar (json["scenes"]);   // sessions from before scenes have none
    e.tape.locate ((juce::int64) tapeInfo["position"]);
    return r;
}

//==============================================================================
Session::Result Session::exportMix (AudioEngine& e, const juce::File& file, juce::int64 start, juce::int64 end, double tailSeconds,
                                     const std::function<bool (double)>& progress)
{
    Result r;
    auto& tape = e.tape;
    const double sr = tape.getSampleRate();
    if (tape.getLength() <= 0)
    {
        r.ok = false;
        r.error = "The tape is empty: there is nothing to export.";
        return r;
    }
    start = juce::jmax ((juce::int64) 0, start);
    if (end <= start)
    {
        r.ok = false;
        r.error = "The Out point must be after the In point.";
        return r;
    }

    file.getParentDirectory().createDirectory();
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

    // Play every track back (armed tracks too, nothing recorded, no loop, no test oscillator). No Params are touched,
    // so this is safe on a worker thread.
    const bool wasLoop = tape.loopOn.load(), wasMuted = e.oscillatorMuted.load();
    const auto wasPosition = tape.getPosition();
    tape.exporting = true;
    tape.loopOn = false;
    e.oscillatorMuted = true;
    forEachChain (e, [] (InsertChain& c) { for (int s = 0; s < InsertChain::numSlots; ++s) if (auto* p = c.getProcessor (s)) p->setNonRealtime (true); });

    tape.locate (start);
    tape.play();

    // The tape runs at its playback rate (speed switch and pitch against the recorded speed).
    const auto total = (juce::int64) std::ceil ((double) (end - start) / tape.getPlaybackRate()) + (juce::int64) (tailSeconds * sr);
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
    e.oscillatorMuted = wasMuted;
    tape.locate (wasPosition);

    if (cancelled)
    {
        file.deleteFile();
        r.ok = false;
        r.error = "Export cancelled.";
    }
    return r;
}
