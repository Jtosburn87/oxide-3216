#include "InsertChain.h"

InsertChain::InsertChain (juce::String n) : name (std::move (n))
{
    sizeBuffer();
    midi.ensureSize (2048);
}

InsertChain::~InsertChain()
{
    clearAll();
}

void InsertChain::changed()
{
    if (onChange)
        onChange();
}

bool InsertChain::negotiateLayout (juce::AudioProcessor& p, bool stereoOnly)
{
    const auto trySet = [&p] (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        auto layout = p.getBusesLayout();
        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = i == 0 ? in : juce::AudioChannelSet::disabled();
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = i == 0 ? out : juce::AudioChannelSet::disabled();
        return p.setBusesLayout (layout);
    };

    if (! trySet (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())
        && ! stereoOnly
        && ! trySet (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())
        && ! trySet (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono()))
    {
        // Keep the plugin's own default layout (e.g. multichannel); we use its first two channels.
    }
    return p.getMainBusNumInputChannels() >= 2 && p.getMainBusNumOutputChannels() >= 2;
}

void InsertChain::setStereoOnly (bool on)
{
    if (on == stereoOnly)
        return;
    stereoOnly = on;

    // A plugin's layout can only change while it is not running: take each one out, renegotiate, put it back.
    for (auto& s : slots)
    {
        std::unique_ptr<juce::AudioProcessor> p;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            p = std::move (s.processor);
        }
        if (p == nullptr)
            continue;

        p->releaseResources();
        const bool stereo = negotiateLayout (*p, stereoOnly);
        p->setRateAndBufferSizeDetails (sampleRate, blockSize);
        p->prepareToPlay (sampleRate, blockSize);

        const juce::SpinLock::ScopedLockType sl (lock);
        s.inputs = p->getMainBusNumInputChannels();
        s.outputs = p->getMainBusNumOutputChannels();
        s.refused.store (stereoOnly && ! stereo);
        s.processor = std::move (p);
        sizeBuffer();
    }
    changed();
}

bool InsertChain::isStereoRefused (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) && slots[(size_t) slot].processor != nullptr && slots[(size_t) slot].refused.load();
}

void InsertChain::sizeBuffer()
{
    int channels = 2;
    for (auto& s : slots)
        if (s.processor != nullptr)
            channels = juce::jmax (channels, s.processor->getTotalNumInputChannels(), s.processor->getTotalNumOutputChannels());
    buffer.setSize (channels, juce::jmax (blockSize, Config::maxBlock), false, true, false);
}

void InsertChain::prepare (double sr, int maxBlockSize)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = sr;
    blockSize = maxBlockSize;
    for (auto& s : slots)
    {
        if (s.processor != nullptr)
        {
            s.processor->releaseResources();
            s.processor->setRateAndBufferSizeDetails (sr, maxBlockSize);
            s.processor->prepareToPlay (sr, maxBlockSize);
        }
    }
    sizeBuffer();
}

void InsertChain::destroy (std::unique_ptr<juce::AudioProcessor> p)
{
    if (p == nullptr)
        return;
    if (beforeDelete)
        beforeDelete (p.get());
    p->removeListener (this);
    p->releaseResources();
    p.reset();
}

void InsertChain::setSlot (int slot, std::unique_ptr<juce::AudioProcessor> p, const juce::PluginDescription& desc)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || p == nullptr)
        return;

    // Prepare outside the lock: this can take a while.
    const bool stereo = negotiateLayout (*p, stereoOnly);
    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);
    p->setNonRealtime (false);
    const int ins = p->getMainBusNumInputChannels(), outs = p->getMainBusNumOutputChannels();
    p->addListener (this);

    std::unique_ptr<juce::AudioProcessor> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) slot];
        old = std::move (s.processor);
        s.processor = std::move (p);
        s.description = desc;
        s.bypassed.store (false);
        s.refused.store (stereoOnly && ! stereo);
        s.inputs = ins;
        s.outputs = outs;
        sizeBuffer();
    }
    destroy (std::move (old));
    changed();
}

void InsertChain::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    std::unique_ptr<juce::AudioProcessor> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (slots[(size_t) slot].processor);
        slots[(size_t) slot].description = {};
        slots[(size_t) slot].bypassed.store (false);
        slots[(size_t) slot].refused.store (false);
    }
    destroy (std::move (old));
    changed();
}

void InsertChain::clearAll()
{
    for (int i = 0; i < numSlots; ++i)
        if (slots[(size_t) i].processor != nullptr)
            clearSlot (i);
}

void InsertChain::setBypassed (int slot, bool b)
{
    if (juce::isPositiveAndBelow (slot, numSlots))
    {
        slots[(size_t) slot].bypassed.store (b);
        changed();
    }
}

void InsertChain::moveSlot (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, numSlots) || ! juce::isPositiveAndBelow (to, numSlots) || from == to)
        return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& a = slots[(size_t) from];
        auto& b = slots[(size_t) to];
        std::swap (a.processor, b.processor);
        std::swap (a.description, b.description);
        std::swap (a.inputs, b.inputs);
        std::swap (a.outputs, b.outputs);
        const bool ab = a.bypassed.load();
        a.bypassed.store (b.bypassed.load());
        b.bypassed.store (ab);
        const bool ar = a.refused.load();
        a.refused.store (b.refused.load());
        b.refused.store (ar);
    }
    changed();
}

juce::AudioProcessor* InsertChain::getProcessor (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].processor.get() : nullptr;
}

const juce::PluginDescription& InsertChain::getDescription (int slot) const
{
    static const juce::PluginDescription none;
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].description : none;
}

bool InsertChain::isBypassed (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) && slots[(size_t) slot].bypassed.load();
}

int InsertChain::numLoaded() const
{
    int n = 0;
    for (auto& s : slots)
        n += s.processor != nullptr ? 1 : 0;
    return n;
}

std::unique_ptr<juce::XmlElement> InsertChain::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("INSERTS");
    xml->setAttribute ("name", name);
    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        if (s.processor == nullptr)
            continue;
        auto* e = xml->createNewChildElement ("SLOT");
        e->setAttribute ("index", i);
        e->setAttribute ("bypassed", s.bypassed.load());
        if (auto d = s.description.createXml())
            e->addChildElement (d.release());
        juce::MemoryBlock state;
        s.processor->getStateInformation (state);
        e->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
    }
    return xml;
}

bool InsertChain::process (float* left, float* right, int n) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked())
        return active.load (std::memory_order_relaxed);   // mid-change: pass audio through for this block

    int lat = 0;
    bool any = false;

    for (auto& s : slots)
    {
        auto* p = s.processor.get();
        if (p == nullptr || s.bypassed.load (std::memory_order_relaxed) || s.refused.load (std::memory_order_relaxed))
            continue;

        any = true;
        const int channels = juce::jmin (buffer.getNumChannels(),
                                         juce::jmax (2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()));
        buffer.setSize (channels, n, false, false, true);   // never reallocates: sized in advance
        buffer.clear();

        if (s.inputs >= 2)
        {
            buffer.copyFrom (0, 0, left, n);
            buffer.copyFrom (1, 0, right, n);
        }
        else if (s.inputs == 1)
        {
            buffer.copyFrom (0, 0, left, n, 0.5f);
            buffer.addFrom (0, 0, right, n, 0.5f);
        }

        midi.clear();
        p->processBlock (buffer, midi);

        if (s.outputs >= 2)
        {
            juce::FloatVectorOperations::copy (left, buffer.getReadPointer (0), n);
            juce::FloatVectorOperations::copy (right, buffer.getReadPointer (1), n);
        }
        else if (s.outputs == 1)
        {
            juce::FloatVectorOperations::copy (left, buffer.getReadPointer (0), n);
            juce::FloatVectorOperations::copy (right, buffer.getReadPointer (0), n);
        }

        lat += p->getLatencySamples();
    }

    latency.store (lat, std::memory_order_relaxed);
    active.store (any, std::memory_order_relaxed);
    return any;
}
