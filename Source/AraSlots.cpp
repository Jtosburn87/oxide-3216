#include "AraSlots.h"
#include <thread>

#if JUCE_PLUGINHOST_ARA
 #include <ARA_API/ARAInterface.h>
 #include <ARA_Library/Dispatch/ARAHostDispatch.h>
#endif

namespace
{
    juce::String trackFile (int track)
    {
        return AraSlots::filePrefix + juce::String (track + 1).paddedLeft ('0', 2) + ".bin";
    }
}

bool AraSlots::isAvailable() noexcept
{
   #if JUCE_PLUGINHOST_ARA
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
#if JUCE_PLUGINHOST_ARA

struct AraSlotsChange
{
    static void notify (AraSlots& s, int track) noexcept { s.changed (track); }
};

namespace
{
    namespace HostModel = juce::ARAHostModel;

    /** The tape, as the plugin's audio reader sees it. Plugins read from their own threads. */
    class AudioAccess final : public ARA::Host::AudioAccessControllerInterface
    {
    public:
        AudioAccess (TapeMachine& t, int tr, std::shared_ptr<std::atomic<bool>> a) : tape (t), track (tr), alive (std::move (a)) {}

        ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef, bool use64) noexcept override
        {
            // 32-bit samples only (the source says so). Readers keep no state: the ref only has to be non-null.
            return use64 ? nullptr : reinterpret_cast<ARA::ARAAudioReaderHostRef> (this);
        }
        bool readAudioSamples (ARA::ARAAudioReaderHostRef, ARA::ARASamplePosition pos, ARA::ARASampleCount num, void* const* buffers) noexcept override
        {
            if (! alive->load())
                return false;   // quitting, and a document that was left behind (AraSlots::~AraSlots)
            auto* dest = static_cast<float*> (buffers[0]);
            const juce::ScopedLock sl (tape.getStorageLock());
            for (ARA::ARASampleCount done = 0; done < num;)
            {
                const int len = (int) juce::jmin (num - done, (ARA::ARASampleCount) (1 << 20));
                tape.readTrack (track, pos + done, dest + done, len);
                done += len;
            }
            return true;
        }
        void destroyAudioReader (ARA::ARAAudioReaderHostRef) noexcept override {}

    private:
        TapeMachine& tape;
        const int track;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    /** Archives go through memory: the document's id in front (see archive files below), then the plugin's bytes. */
    class Archiving final : public ARA::Host::ArchivingControllerInterface
    {
    public:
        struct Reader
        {
            const juce::MemoryBlock* data;
            juce::String id;
        };
        using ReadRef = HostModel::ConversionFunctions<Reader*, ARA::ARAArchiveReaderHostRef>;
        using WriteRef = HostModel::ConversionFunctions<juce::MemoryOutputStream*, ARA::ARAArchiveWriterHostRef>;

        ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef r) noexcept override { return (ARA::ARASize) ReadRef::fromHostRef (r)->data->getSize(); }
        bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef r, ARA::ARASize pos, ARA::ARASize len, ARA::ARAByte* buffer) noexcept override
        {
            const auto* mb = ReadRef::fromHostRef (r)->data;
            if (pos + len > mb->getSize())
                return false;
            std::memcpy (buffer, juce::addBytesToPointer (mb->getData(), pos), len);
            return true;
        }
        bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef w, ARA::ARASize pos, ARA::ARASize len, const ARA::ARAByte* buffer) noexcept override
        {
            auto* os = WriteRef::fromHostRef (w);
            return os->setPosition ((juce::int64) pos) && os->write (buffer, len);
        }
        void notifyDocumentArchivingProgress (float) noexcept override {}
        void notifyDocumentUnarchivingProgress (float) noexcept override {}
        ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef r) noexcept override
        {
            return ReadRef::fromHostRef (r)->id.toRawUTF8();
        }
    };

    /** The plugin's edits, so the session saves them. */
    class ModelUpdates final : public ARA::Host::ModelUpdateControllerInterface
    {
    public:
        ModelUpdates (AraSlots& o, int t, std::shared_ptr<std::atomic<bool>> a) : owner (o), track (t), analysing (std::move (a)) {}
        void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef, ARA::ARAAnalysisProgressState state, float) noexcept override
        {
            analysing->store (state != ARA::kARAAnalysisProgressCompleted);
        }
        void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override { AraSlotsChange::notify (owner, track); }
        void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override { AraSlotsChange::notify (owner, track); }
        void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override { AraSlotsChange::notify (owner, track); }

    private:
        AraSlots& owner;
        const int track;
        std::shared_ptr<std::atomic<bool>> analysing;
    };

    /** The plugin's own transport buttons work the tape. */
    class Playback final : public ARA::Host::PlaybackControllerInterface
    {
    public:
        Playback (TapeMachine& t, std::shared_ptr<std::atomic<bool>> a) : tape (t), alive (std::move (a)) {}
        void requestStartPlayback() noexcept override { later ([] (TapeMachine& t) { t.play(); }); }
        void requestStopPlayback() noexcept override  { later ([] (TapeMachine& t) { t.stop(); }); }
        void requestSetPlaybackPosition (ARA::ARATimePosition time) noexcept override
        {
            later ([time] (TapeMachine& t) { t.locate ((juce::int64) std::llround (juce::jmax (0.0, time) * t.getSampleRate())); });
        }
        void requestSetCycleRange (ARA::ARATimePosition start, ARA::ARATimeDuration length) noexcept override
        {
            later ([start, length] (TapeMachine& t)
            {
                t.setInPoint ((juce::int64) std::llround (start * t.getSampleRate()));
                t.setOutPoint ((juce::int64) std::llround ((start + length) * t.getSampleRate()));
            });
        }
        void requestEnableCycle (bool on) noexcept override { later ([on] (TapeMachine& t) { t.loopOn = on; }); }

    private:
        TapeMachine& tape;
        std::shared_ptr<std::atomic<bool>> alive;

        template <typename Fn>
        void later (Fn fn)
        {
            juce::MessageManager::callAsync ([&t = tape, a = alive, fn]
            {
                if (a->load())
                    fn (t);
            });
        }
    };

    /** Where the tape is, for the plugin's renderer. The tape's position is the playback time. */
    struct Head final : juce::AudioPlayHead
    {
        juce::int64 pos = 0;
        double sampleRate = 48000.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setTimeInSamples (pos);
            p.setTimeInSeconds ((double) pos / sampleRate);
            p.setIsPlaying (true);
            return p;
        }
    };

    /** A mono track: mono in and out if the plugin can, otherwise stereo (of which the left is used). */
    void negotiateLayout (juce::AudioPluginInstance& p)
    {
        for (const auto& set : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        {
            auto layout = p.getBusesLayout();
            if (! layout.inputBuses.isEmpty())
                layout.inputBuses.getReference (0) = set;
            if (! layout.outputBuses.isEmpty())
                layout.outputBuses.getReference (0) = set;
            if (p.setBusesLayout (layout))
                return;
        }
    }

    juce::MemoryBlock withId (const juce::String& id, const juce::MemoryBlock& archive)
    {
        juce::MemoryOutputStream os;
        os.writeString (id);
        os.write (archive.getData(), archive.getSize());
        return os.getMemoryBlock();
    }

    bool splitId (const juce::MemoryBlock& file, juce::String& id, juce::MemoryBlock& archive)
    {
        juce::MemoryInputStream in (file, false);
        id = in.readString();
        const auto rest = in.getNumBytesRemaining();
        if (id.isEmpty() || rest <= 0)
            return false;
        archive.setSize ((size_t) rest);
        return in.read (archive.getData(), (int) rest) == (int) rest;
    }

    bool archiveFits (const ARA::ARAFactory& f, const juce::String& id)
    {
        if (id == f.documentArchiveID)
            return true;
        for (ARA::ARASize i = 0; i < f.compatibleDocumentArchiveIDsCount; ++i)
            if (id == f.compatibleDocumentArchiveIDs[i])
                return true;
        return false;
    }
}

struct AraSlots::Host
{
    /** Goes last. The plugin's document has to outlive its instance, but the plugin's code goes when its last instance
        does (JUCE unloads it, whatever ARA objects are left): one idle instance of each plugin in use stays until
        then. */
    std::shared_ptr<juce::AudioPluginInstance> keeper;

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    juce::ARAFactoryWrapper factory;
    std::unique_ptr<juce::ARAHostDocumentController> document;
    std::unique_ptr<HostModel::MusicalContext> context;
    std::unique_ptr<HostModel::RegionSequence> sequence;
    std::unique_ptr<HostModel::AudioSource> source;
    std::unique_ptr<HostModel::AudioModification> modification;
    std::unique_ptr<HostModel::PlaybackRegion> region;
    std::unique_ptr<HostModel::PlaybackRendererInterface> renderer;
    std::string name, sourceId, modificationId;

    Head head;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int channels = 1;
    std::atomic<bool> ready { false };
    /** The plugin says it is reading or analysing the audio. */
    std::shared_ptr<std::atomic<bool>> analysing = std::make_shared<std::atomic<bool>> (false);
    juce::int64 samples = 0;
    std::uint32_t seenRevision = 0;
    double modelRate = 0.0;

    ARA::ARAAudioSourceProperties sourceProperties()
    {
        auto p = HostModel::AudioSource::getEmptyProperties();
        p.name = name.c_str();
        p.persistentID = sourceId.c_str();
        p.sampleCount = samples;
        p.sampleRate = modelRate;
        p.channelCount = 1;
        p.merits64BitSamples = false;
        return p;
    }

    ARA::ARAPlaybackRegionProperties regionProperties()
    {
        auto p = HostModel::PlaybackRegion::getEmptyProperties();
        p.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
        p.startInModificationTime = 0.0;
        p.durationInModificationTime = (double) samples / modelRate;
        p.startInPlaybackTime = 0.0;
        p.durationInPlaybackTime = p.durationInModificationTime;
        p.musicalContextRef = context->getPluginRef();
        p.regionSequenceRef = sequence->getPluginRef();
        p.name = name.c_str();
        return p;
    }

    ~Host()
    {
        stopPlugin();
        closeDocument();
    }

    /** The plugin stops playing and goes. Its document stays for now: see closeDocument. */
    void stopPlugin()
    {
        if (instance == nullptr)
            return;
        instance->releaseResources();
        if (renderer != nullptr && region != nullptr)
            renderer->remove (*region);
        renderer.reset();
        instance.reset();
    }

    /** Melodyne can hang for good if its document is destroyed on the message thread: that thread waits for a worker
        that needs it. So a document goes on a thread of its own, once the plugin has finished with the audio
        (AraSlots::finishRetiring). */
    void closeDocument()
    {
        if (document != nullptr)
        {
            if (source != nullptr)
                source->enableAudioSourceSamplesAccess (false);
            {
                const juce::ARAEditGuard guard (document->getDocumentController());
                region.reset();
                modification.reset();
                source.reset();
                sequence.reset();
                context.reset();
            }
        }
        document.reset();
    }
};

void AraSlots::load (int track, const CreatePlugin& create, const juce::PluginDescription& description,
                     const juce::MemoryBlock& state, const juce::MemoryBlock& archive, std::function<void (const juce::String&)> done)
{
    JUCE_ASSERT_MESSAGE_THREAD
    clear (track);

    const auto failed = [&done, &description] (const juce::String& error)
    {
        if (done)
            done (description.name + " could not be loaded: " + error);
    };
    juce::String error;
    auto instance = create (description, error);
    if (instance == nullptr)
        return failed (error);
    auto& keeperRef = keepers[description.createIdentifierString()];
    auto keeper = keeperRef.lock();
    if (keeper == nullptr)
    {
        keeper = create (description, error);
        if (keeper == nullptr)
            return failed (error);
        keeperRef = keeper;
    }

    auto host = std::make_shared<std::unique_ptr<Host>> (std::make_unique<Host>());
    (*host)->keeper = std::move (keeper);
    (*host)->instance = std::move (instance);
    (*host)->description = description;
    auto* plugin = (*host)->instance.get();
    const std::weak_ptr<std::atomic<bool>> stillHere = alive;

    juce::createARAFactoryAsync (*plugin, [this, track, host, state, archive, done, stillHere] (juce::ARAFactoryWrapper factory)
    {
        if (stillHere.expired())
            return;
        if (factory.get() == nullptr)
        {
            if (done)
                done ((*host)->description.name + " does not offer ARA.");
            return;
        }
        (*host)->factory = std::move (factory);
        finishLoad (track, std::move (*host), state, archive, done);
    });
}

void AraSlots::finishLoad (int track, std::unique_ptr<Host> h, const juce::MemoryBlock& state, const juce::MemoryBlock& archive,
                           const std::function<void (const juce::String&)>& done)
{
    const juce::String n = "Tape track " + juce::String (track + 1);
    h->name = n.toStdString();
    h->sourceId = "oxide-tape-track-" + std::to_string (track + 1);
    h->modificationId = h->sourceId + "-edit";
    h->modelRate = rate > 0.0 ? rate : tape.getSampleRate();
    h->head.sampleRate = h->modelRate;
    h->samples = juce::jmax ((juce::int64) h->modelRate, tape.getWrittenEnd (track));   // a blank track: a second of silence
    h->seenRevision = tape.getTrackRevision (track);

    h->document = juce::ARAHostDocumentController::create (h->factory, n,
                                                           std::make_unique<AudioAccess> (tape, track, alive),
                                                           std::make_unique<Archiving>(),
                                                           nullptr,
                                                           std::make_unique<ModelUpdates> (*this, track, h->analysing),
                                                           std::make_unique<Playback> (tape, alive));
    if (h->document == nullptr)
    {
        if (done)
            done (h->description.name + " could not start an ARA document.");
        return;
    }
    auto& dc = h->document->getDocumentController();

    bool restored = true;
    {
        const juce::ARAEditGuard guard (dc);
        auto cp = HostModel::MusicalContext::getEmptyProperties();
        cp.name = h->name.c_str();
        h->context = std::make_unique<HostModel::MusicalContext> (nullptr, dc, cp);

        auto sp = HostModel::RegionSequence::getEmptyProperties();
        sp.name = h->name.c_str();
        sp.orderIndex = track;
        sp.musicalContextRef = h->context->getPluginRef();
        h->sequence = std::make_unique<HostModel::RegionSequence> (nullptr, dc, sp);

        h->source = std::make_unique<HostModel::AudioSource> (reinterpret_cast<ARA::ARAAudioSourceHostRef> (h.get()), dc, h->sourceProperties());

        auto mp = HostModel::AudioModification::getEmptyProperties();
        mp.name = h->name.c_str();
        mp.persistentID = h->modificationId.c_str();
        h->modification = std::make_unique<HostModel::AudioModification> (nullptr, dc, *h->source, mp);

        h->region = std::make_unique<HostModel::PlaybackRegion> (nullptr, dc, *h->modification, h->regionProperties());

        juce::String id;
        juce::MemoryBlock bytes;
        if (! archive.isEmpty() && splitId (archive, id, bytes) && archiveFits (*h->factory.get(), id))
        {
            Archiving::Reader reader { &bytes, id };
            restored = dc.restoreObjectsFromArchive (Archiving::ReadRef::toHostRef (&reader), nullptr);
        }
    }
    h->source->enableAudioSourceSamplesAccess (true);

    const auto roles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
    auto extension = h->document->bindDocumentToPluginInstance (*h->instance, roles, roles);
    if (! extension.isValid() || ! extension.getPlaybackRendererInterface().isValid())
    {
        if (done)
            done (h->description.name + " could not take the track as an ARA plugin.");
        return;
    }
    h->renderer = std::make_unique<HostModel::PlaybackRendererInterface> (extension.getPlaybackRendererInterface());
    h->renderer->add (*h->region);

    negotiateLayout (*h->instance);
    h->channels = juce::jmax (1, h->instance->getTotalNumInputChannels(), h->instance->getTotalNumOutputChannels());
    h->buffer.setSize (h->channels, blockSize);
    h->instance->setPlayHead (&h->head);
    if (! state.isEmpty())
        h->instance->setStateInformation (state.getData(), (int) state.getSize());
    h->instance->prepareToPlay (h->modelRate, blockSize);
    h->ready = true;

    {
        const juce::SpinLock::ScopedLockType sl (slots[(size_t) track].lock);
        slots[(size_t) track].host = std::move (h);
    }
    changed (track);
    if (done)
        done (restored ? juce::String() : "The plugin's edits on track " + juce::String (track + 1) + " could not be read back.");
}

void AraSlots::clear (int track)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto& s = slots[(size_t) track];
    if (s.host == nullptr)
        return;
    if (beforeDelete)
        beforeDelete (s.host->instance.get());
    std::unique_ptr<Host> gone;
    {
        const juce::SpinLock::ScopedLockType sl (s.lock);
        gone = std::move (s.host);
    }
    gone->stopPlugin();
    retiring.push_back ({ std::move (gone), juce::Time::getMillisecondCounterHiRes() });
    s.bypassed = false;
    changed (track);
}

juce::AudioProcessor* AraSlots::getProcessor (int track) const
{
    const auto& h = slots[(size_t) track].host;
    return h != nullptr ? h->instance.get() : nullptr;
}

juce::PluginDescription AraSlots::getDescription (int track) const
{
    const auto& h = slots[(size_t) track].host;
    return h != nullptr ? h->description : juce::PluginDescription();
}

void AraSlots::prepareAll()
{
    for (int t = 0; t < numSlots; ++t)
        updateModel (t);
}

void AraSlots::updateModel (int track)
{
    auto& s = slots[(size_t) track];
    auto* h = s.host.get();
    if (h == nullptr)
        return;
    const double newRate = rate > 0.0 ? rate : h->modelRate;
    const auto length = juce::jmax ((juce::int64) newRate, tape.getWrittenEnd (track));
    const bool rateChanged = newRate != h->modelRate;
    h->seenRevision = tape.getTrackRevision (track);

    {
        const juce::SpinLock::ScopedLockType sl (s.lock);   // waits for a block in progress
        h->ready = false;
    }
    if (rateChanged)
    {
        h->instance->releaseResources();
        h->instance->prepareToPlay (newRate, blockSize);
    }
    h->modelRate = newRate;
    h->head.sampleRate = newRate;
    h->samples = length;

    auto& dc = h->document->getDocumentController();
    h->source->enableAudioSourceSamplesAccess (false);
    {
        const juce::ARAEditGuard guard (dc);
        h->source->update (h->sourceProperties());
        h->region->update (h->regionProperties());
        dc.updateAudioSourceContent (h->source->getPluginRef(), nullptr, ARA::kARAContentUpdateEverythingChanged);
    }
    h->source->enableAudioSourceSamplesAccess (true);
    h->ready = true;
    changed (track);
}

void AraSlots::prepare (double sampleRate, int maxBlockSize)
{
    blockSize = juce::jmax (blockSize, maxBlockSize);
    if (sampleRate == rate)
        return;
    rate = sampleRate;
    if (juce::MessageManager::existsAndIsCurrentThread())
        prepareAll();
    else
        juce::MessageManager::callAsync ([this, a = std::weak_ptr<std::atomic<bool>> (alive)]
        {
            if (! a.expired())
                prepareAll();
        });
}

void AraSlots::setNonRealtime (bool on)
{
    for (auto& s : slots)
        if (s.host != nullptr)
            s.host->instance->setNonRealtime (on);
}

void AraSlots::finishRetiring()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (auto it = retiring.begin(); it != retiring.end();)
    {
        it->host->document->getDocumentController().notifyModelUpdates();
        const bool finished = now - it->since >= documentDelayMs && ! it->host->analysing->load();
        if (finished)
        {
            // Off the message thread: Melodyne's workers need it while its document closes.
            closingCount()->fetch_add (1);
            std::thread ([host = std::shared_ptr<Host> (std::move (it->host)), c = closingCount()]
            {
                host->closeDocument();
                c->fetch_sub (1);
            }).detach();
            it = retiring.erase (it);
        }
        else
            ++it;
    }
}

void AraSlots::tick()
{
    finishRetiring();
    for (int t = 0; t < numSlots; ++t)
    {
        auto* h = slots[(size_t) t].host.get();
        if (h == nullptr)
            continue;
        h->document->getDocumentController().notifyModelUpdates();
        // A take is finished, or the tape was erased or loaded: the plugin has to read it again.
        const bool taking = tape.isPlaying() && tape.isRecording (t);
        if (! taking && tape.getTrackRevision (t) != h->seenRevision)
            updateModel (t);
    }
}

bool AraSlots::render (int track, juce::int64 position, float* dest, int numSamples) noexcept
{
    auto& s = slots[(size_t) track];
    if (s.bypassed.load (std::memory_order_relaxed))
        return false;
    const juce::SpinLock::ScopedTryLockType tl (s.lock);
    if (! tl.isLocked() || s.host == nullptr || ! s.host->ready.load (std::memory_order_relaxed))
        return false;

    auto& h = *s.host;
    const int bufferSize = h.buffer.getNumSamples();
    // The plugin's delay is covered by asking for the tape that far ahead.
    const int latency = h.instance->getLatencySamples();
    for (int done = 0; done < numSamples;)
    {
        const int count = juce::jmin (numSamples - done, bufferSize);
        h.buffer.clear();
        juce::AudioBuffer<float> block (h.buffer.getArrayOfWritePointers(), h.channels, count);
        h.head.pos = position + done + latency;
        h.instance->processBlock (block, h.midi);
        h.midi.clear();
        std::memcpy (dest + done, h.buffer.getReadPointer (0), sizeof (float) * (size_t) count);
        done += count;
    }
    return true;
}

juce::var AraSlots::save (const juce::File& folder)
{
    const bool newFolder = folder != savedTo;
    savedTo = folder;
    juce::Array<juce::var> list;
    for (int t = 0; t < numSlots; ++t)
    {
        auto& s = slots[(size_t) t];
        const auto file = folder.getChildFile (trackFile (t));
        auto* h = s.host.get();
        if (h == nullptr)
        {
            file.deleteFile();
            continue;
        }
        if (newFolder || s.dirty.exchange (false) || ! file.existsAsFile())
        {
            juce::MemoryOutputStream bytes;
            if (h->document->getDocumentController().storeObjectsToArchive (Archiving::WriteRef::toHostRef (&bytes), nullptr))
                file.replaceWithData (withId (h->factory.get()->documentArchiveID, bytes.getMemoryBlock()).getData(),
                                      withId (h->factory.get()->documentArchiveID, bytes.getMemoryBlock()).getSize());
        }
        juce::MemoryBlock state;
        h->instance->getStateInformation (state);
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("track", t + 1);
        if (auto xml = h->description.createXml())
            entry->setProperty ("plugin", xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()));
        entry->setProperty ("state", state.toBase64Encoding());
        entry->setProperty ("bypassed", s.bypassed.load());
        list.add (juce::var (entry));
    }
    return list;
}

juce::StringArray AraSlots::restore (const juce::var& v, const juce::File& folder, const CreatePlugin& create)
{
    juce::StringArray problems;
    clearAll();
    savedTo = folder;
    if (auto* list = v.getArray())
        for (auto& entry : *list)
        {
            const int track = (int) entry["track"] - 1;
            juce::PluginDescription d;
            auto xml = juce::parseXML (entry["plugin"].toString());
            if (! juce::isPositiveAndBelow (track, numSlots) || xml == nullptr || ! d.loadFromXml (*xml))
                continue;
            juce::MemoryBlock state, archive;
            state.fromBase64Encoding (entry["state"].toString());
            folder.getChildFile (trackFile (track)).loadFileAsData (archive);
            load (track, create, d, state, archive, [&problems, track] (const juce::String& e)
            {
                if (e.isNotEmpty())
                    problems.add (e + " (tape track " + juce::String (track + 1) + ")");
            });
            slots[(size_t) track].bypassed = (bool) entry["bypassed"];
            slots[(size_t) track].dirty = false;
        }
    return problems;
}

#else   // no ARA SDK in this build ===============================================

struct AraSlots::Host {};

void AraSlots::load (int, const CreatePlugin&, const juce::PluginDescription&, const juce::MemoryBlock&,
                     const juce::MemoryBlock&, std::function<void (const juce::String&)> done)
{
    if (done)
        done ("This build of Oxide-3216 has no ARA.");
}
void AraSlots::finishLoad (int, std::unique_ptr<Host>, const juce::MemoryBlock&, const juce::MemoryBlock&, const std::function<void (const juce::String&)>&) {}
void AraSlots::clear (int) {}
void AraSlots::finishRetiring() {}
juce::AudioProcessor* AraSlots::getProcessor (int) const { return nullptr; }
juce::PluginDescription AraSlots::getDescription (int) const { return {}; }
void AraSlots::prepareAll() {}
void AraSlots::updateModel (int) {}
void AraSlots::prepare (double, int) {}
void AraSlots::setNonRealtime (bool) {}
void AraSlots::tick() {}
bool AraSlots::render (int, juce::int64, float*, int) noexcept { return false; }
juce::var AraSlots::save (const juce::File&) { return juce::Array<juce::var>(); }
juce::StringArray AraSlots::restore (const juce::var&, const juce::File&, const CreatePlugin&) { return {}; }

#endif

//==============================================================================
AraSlots::AraSlots (TapeMachine& t)
    : tape (t), alive (std::make_shared<std::atomic<bool>> (true))
{
    tape.setPlaybackSource (this);
    startTimerHz (10);
}

AraSlots::~AraSlots()
{
    stopTimer();
    tape.setPlaybackSource (nullptr);
    alive->store (false);
    clearAll();
    // The documents that are left, once their plugins have finished. A plugin still busy after that is left behind:
    // quitting matters more, and destroying it would hang (Host::closeDocument). Nothing it does can reach the tape now.
    const auto giveUp = juce::Time::getMillisecondCounterHiRes() + quitWaitMs;
    while ((! retiring.empty() || closingCount()->load() > 0) && juce::MessageManager::existsAndIsCurrentThread() && juce::Time::getMillisecondCounterHiRes() < giveUp)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        finishRetiring();
    }
    for (auto& r : retiring)
        r.host.release();
}

void AraSlots::clearAll()
{
    for (int t = 0; t < numSlots; ++t)
        clear (t);
}

void AraSlots::setBypassed (int track, bool on)
{
    slots[(size_t) track].bypassed = on;
    changed (track);
}

void AraSlots::changed (int track) noexcept
{
    slots[(size_t) track].dirty = true;
    revision.fetch_add (1);
}
