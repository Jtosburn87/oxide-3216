#include "PluginManager.h"
#include <algorithm>

namespace
{
    constexpr int scanTimeoutMs = 60000;

    void addFormats (juce::AudioPluginFormatManager& fm)
    {
        fm.addFormat (new juce::VST3PluginFormat());   // first: the folder scan and --snapshot use format 0
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        fm.addFormat (new juce::AudioUnitPluginFormat());
       #endif
    }

    /** Scans one plugin file in a separate process, so a crashing plugin cannot take the console down. */
    class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            const juce::TemporaryFile temp ("scan.xml");
            juce::ChildProcess child;
            const juce::StringArray command { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                              "--scan-plugin", fileOrIdentifier, temp.getFile().getFullPathName() };
            if (! child.start (command, 0))
                return false;

            if (! child.waitForProcessToFinish (scanTimeoutMs))
            {
                child.kill();   // hung (or waiting on a dialog nobody can see)
                return false;
            }
            if (child.getExitCode() != 0)
                return false;   // crashed, or nothing loadable in the file

            if (auto xml = juce::parseXML (temp.getFile()))
            {
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e))
                        result.add (d.release());
                }
            }
            return ! result.isEmpty();
        }
    };
}

PluginManager::PluginManager (juce::PropertiesFile& s) : settings (s)
{
    addFormats (formats);
    if (auto xml = settings.getXmlValue ("pluginList"))
        known.recreateFromXml (*xml);
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
    known.addChangeListener (this);
}

/** Asks every known plugin file again, in a process of its own, so old entries learn their ARA flag; and every other
    plugin in the usual folders, so an ARA plugin that was never scanned shows up too. */
class PluginManager::AraScan : public juce::Thread
{
public:
    AraScan (PluginManager& o, std::function<void()> d) : juce::Thread ("ARA scan"), owner (o), done (std::move (d))
    {
        for (auto& t : owner.known.getTypes())
            if (t.pluginFormatName == "VST3" && ! t.hasARAExtension)
                files.addIfNotAlreadyThere (t.fileOrIdentifier);
        juce::VST3PluginFormat format;
        const auto known = owner.known.getTypes();
        for (auto& file : format.searchPathsForPlugins (format.getDefaultLocationsToSearch(), true, false))
            if (std::none_of (known.begin(), known.end(), [&file] (const juce::PluginDescription& d) { return d.fileOrIdentifier == file; }))
                files.addIfNotAlreadyThere (file);
        startThread (juce::Thread::Priority::low);
    }
    ~AraScan() override { stopThread (scanTimeoutMs + 2000); }

    void run() override
    {
        OutOfProcessScanner scanner;
        juce::VST3PluginFormat format;
        for (auto& file : files)
        {
            if (threadShouldExit())
                return;
            juce::OwnedArray<juce::PluginDescription> found;
            if (scanner.findPluginTypesFor (format, found, file))
                for (auto* d : found)
                    if (d->hasARAExtension)
                        ara.add (*d);
        }
        juce::MessageManager::callAsync ([this, safe = juce::WeakReference<PluginManager::AraScanOwner> (&owner.scanOwner)]
        {
            if (safe != nullptr)
                owner.araScanFinished (juce::Array<juce::PluginDescription> (ara), done);
        });
    }

private:
    PluginManager& owner;
    std::function<void()> done;
    juce::StringArray files;
    juce::Array<juce::PluginDescription> ara;
};

PluginManager::~PluginManager()
{
    araScan.reset();
    known.removeChangeListener (this);
}

juce::Array<juce::PluginDescription> PluginManager::araEffects() const
{
    juce::Array<juce::PluginDescription> list;
    for (auto& d : known.getTypes())
        if (d.hasARAExtension && ! d.isInstrument)
            list.add (d);
    return list;
}

bool PluginManager::needsAraScan() const
{
    return ! settings.getBoolValue ("araScanDone", false);
}

void PluginManager::startAraScan (std::function<void()> done)
{
    if (araScan == nullptr)
        araScan = std::make_unique<AraScan> (*this, std::move (done));
}

void PluginManager::araScanFinished (const juce::Array<juce::PluginDescription>& found, std::function<void()> done)
{
    for (auto& d : found)
    {
        if (auto old = known.getTypeForIdentifierString (d.createIdentifierString()))
            known.removeType (*old);
        known.addType (d);
    }
    settings.setValue ("araScanDone", true);
    settings.saveIfNeeded();
    araScan.reset();
    if (done)
        done();
}

void PluginManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = known.createXml())
    {
        settings.setValue ("pluginList", xml.get());
        settings.saveIfNeeded();
    }
}

juce::Array<juce::PluginDescription> PluginManager::effects() const
{
    juce::Array<juce::PluginDescription> list;
    for (auto& d : known.getTypes())
        if (! d.isInstrument)
            list.add (d);
    return list;
}

juce::Array<juce::PluginDescription> PluginManager::instruments() const
{
    juce::Array<juce::PluginDescription> list;
    for (auto& d : known.getTypes())
        if (d.isInstrument)
            list.add (d);
    return list;
}

juce::File PluginManager::deadMansPedalFile() const
{
    return settings.getFile().getSiblingFile ("PluginScanCrash.txt");
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::create (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
    return formats.createPluginInstance (d, sr, block, error);
}

juce::StringArray PluginManager::restore (InsertChain& chain, const juce::XmlElement& xml, double sr, int block)
{
    juce::StringArray missing;
    for (auto* slot : xml.getChildWithTagNameIterator ("SLOT"))
    {
        juce::PluginDescription d;
        auto* descXml = slot->getChildByName ("PLUGIN");
        if (descXml == nullptr || ! d.loadFromXml (*descXml))
            continue;

        juce::String error;
        auto instance = create (d, sr, block, error);
        if (instance == nullptr)
        {
            missing.add (d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String())
                         + " on " + chain.name);
            continue;
        }

        if (auto* state = slot->getChildByName ("STATE"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (state->getAllSubText()))
                instance->setStateInformation (mb.getData(), (int) mb.getSize());
        }

        const int index = slot->getIntAttribute ("index");
        chain.setSlot (index, std::move (instance), d);
        chain.setBypassed (index, slot->getBoolAttribute ("bypassed"));
    }
    return missing;
}

int PluginManager::runScanner (const juce::StringArray& args)
{
    const int i = args.indexOf ("--scan-plugin");
    if (i < 0 || args.size() < i + 3)
        return 2;

    const auto pluginFile = args[i + 1];
    const juce::File out (args[i + 2]);

    juce::AudioPluginFormatManager fm;
    addFormats (fm);

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : fm.getFormats())
        if (format->fileMightContainThisPluginType (pluginFile))
            format->findAllTypesForFile (found, pluginFile);

    juce::XmlElement xml ("FOUND");
    for (auto* d : found)
        if (auto e = d->createXml())
            xml.addChildElement (e.release());
    xml.writeTo (out);

    return found.isEmpty() ? 1 : 0;
}
