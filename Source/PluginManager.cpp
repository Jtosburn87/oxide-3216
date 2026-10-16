#include "PluginManager.h"

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

PluginManager::~PluginManager()
{
    known.removeChangeListener (this);
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
