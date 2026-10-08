#include "AudioSettingsComponent.h"
#include "ConsoleLookAndFeel.h"

namespace
{
    // ComboBox item IDs: 1 = not connected, deviceChannel + 2 otherwise.
    constexpr int noneId = 1;
    int idForChannel (int ch)  { return ch < 0 ? noneId : ch + 2; }
    int channelForId (int id)  { return id <= noneId ? -1 : id - 2; }

    void fillChannelBox (juce::ComboBox& box, const juce::StringArray& names, const juce::BigInteger& active, int selected)
    {
        box.clear (juce::dontSendNotification);
        box.addItem ("(none)", noneId);
        for (int ch = 0; ch < names.size(); ++ch)
            box.addItem (juce::String (ch + 1) + ": " + names[ch] + (active[ch] ? "" : "  (channel off)"), idForChannel (ch));

        // Keep a saved mapping visible even if the current device has fewer channels.
        if (selected >= names.size())
            box.addItem (juce::String (selected + 1) + ": (not on this device)", idForChannel (selected));

        box.setSelectedId (idForChannel (selected), juce::dontSendNotification);
    }
}

//==============================================================================
AudioSettingsComponent::MappingPage::MappingPage (juce::String text, std::vector<Row> r, int cols)
    : rows (std::move (r)), columns (cols)
{
    heading.setText (text, juce::dontSendNotification);
    heading.setFont (ConsoleLookAndFeel::uiFont (14.0f));
    heading.setColour (juce::Label::textColourId, ConsoleColours::legend.withAlpha (0.8f));
    addAndMakeVisible (heading);

    for (auto& row : rows)
    {
        auto* l = labels.add (new juce::Label ({}, row.label));
        l->setFont (ConsoleLookAndFeel::uiFont (14.0f));
        addAndMakeVisible (l);

        auto* box = boxes.add (new juce::ComboBox (row.label));
        box->setTitle (row.label);
        box->onChange = [this, box, set = row.set]
        {
            set (channelForId (box->getSelectedId()));
            if (onChanged) onChanged();
        };
        addAndMakeVisible (box);
    }
}

void AudioSettingsComponent::MappingPage::refresh (const juce::StringArray& names, const juce::BigInteger& active)
{
    for (size_t i = 0; i < rows.size(); ++i)
        fillChannelBox (*boxes[(int) i], names, active, rows[i].get());
}

void AudioSettingsComponent::MappingPage::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    heading.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    const int rowH = 28, gap = 16;
    const int perColumn = (int) (rows.size() + (size_t) columns - 1) / columns;
    const int colW = (area.getWidth() - gap * (columns - 1)) / columns;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int col = (int) i / perColumn, row = (int) i % perColumn;
        auto cell = juce::Rectangle<int> (area.getX() + col * (colW + gap), area.getY() + row * rowH, colW, rowH).reduced (0, 2);
        labels[(int) i]->setBounds (cell.removeFromLeft (64));
        boxes[(int) i]->setBounds (cell);
    }

    if (extra != nullptr)
        extra->setBounds (area.getX(), area.getY() + perColumn * rowH + 8, 120, 26);
}

//==============================================================================
AudioSettingsComponent::AudioSettingsComponent (AudioEngine& e, std::function<void()> changedCallback)
    : engine (e),
      onChanged (std::move (changedCallback)),
      selector (e.getDeviceManager(), 0, AudioEngine::maxDeviceChannels, 0, AudioEngine::maxDeviceChannels,
                false, false, false, false)
{
    addAndMakeVisible (selector);

    std::vector<MappingPage::Row> inRows;
    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
        inRows.push_back ({ "Input " + juce::String (c + 1),
                            [this, c] { return engine.getInputSource (c); },
                            [this, c] (int ch) { engine.setInputSource (c, ch); } });
    inputsPage = std::make_unique<MappingPage> ("Which interface input feeds each console input channel", std::move (inRows), 2);
    inputsPage->onChanged = [this] { changed(); };
    autoMapButton.setTooltip ("Input channel 1 from interface input 1, channel 2 from input 2, and so on");
    autoMapButton.onClick = [this]
    {
        for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
            engine.setInputSource (c, c);
        refresh();
        changed();
    };
    inputsPage->addAndMakeVisible (autoMapButton);
    inputsPage->extra = &autoMapButton;

    std::vector<MappingPage::Row> outRows;
    outRows.push_back ({ "Main L", [this] { return engine.getMainOutput (0); }, [this] (int ch) { engine.setMainOutput (0, ch); } });
    outRows.push_back ({ "Main R", [this] { return engine.getMainOutput (1); }, [this] (int ch) { engine.setMainOutput (1, ch); } });
    for (int k = 0; k < Config::numAux; ++k)
        outRows.push_back ({ "Aux " + juce::String (k + 1),
                             [this, k] { return engine.getAuxOutput (k); },
                             [this, k] (int ch) { engine.setAuxOutput (k, ch); } });
    for (int k = 0; k < Config::numSubs; ++k)
        for (int side = 0; side < 2; ++side)
            outRows.push_back ({ "Sub " + juce::String (k + 1) + (side == 0 ? " L" : " R"),
                                 [this, k, side] { return engine.getSubOutput (k, side); },
                                 [this, k, side] (int ch) { engine.setSubOutput (k, side, ch); } });
    outputsPage = std::make_unique<MappingPage> ("Interface outputs for main, and optionally for each aux master and sub", std::move (outRows), 2);
    outputsPage->onChanged = [this] { changed(); };

    const auto tabColour = ConsoleColours::room;
    tabs.addTab ("Inputs", tabColour, inputsPage.get(), false);
    tabs.addTab ("Outputs", tabColour, outputsPage.get(), false);
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    latencyLabel.setFont (ConsoleLookAndFeel::uiFont (13.0f));
    latencyLabel.setColour (juce::Label::textColourId, ConsoleColours::legend.withAlpha (0.75f));
    addAndMakeVisible (latencyLabel);

    offsetTitle.setText ("Recording offset", juce::dontSendNotification);
    offsetTitle.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
    addAndMakeVisible (offsetTitle);
    offsetHelp.setText ("Overdubs are shifted by the latency the driver reports. If they still land early or late, "
                        "correct it here (samples, + moves recordings earlier).", juce::dontSendNotification);
    offsetHelp.setFont (ConsoleLookAndFeel::uiFont (13.0f));
    offsetHelp.setColour (juce::Label::textColourId, ConsoleColours::legend.withAlpha (0.75f));
    offsetHelp.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (offsetHelp);
    offsetSlider.setSliderStyle (juce::Slider::IncDecButtons);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 24);
    offsetSlider.setRange (-4800.0, 4800.0, 1.0);
    offsetSlider.setValue (engine.getRecordOffset(), juce::dontSendNotification);
    offsetSlider.setTitle ("Recording offset in samples");
    offsetSlider.onValueChange = [this]
    {
        engine.setRecordOffset ((int) offsetSlider.getValue());
        refresh();
        changed();
    };
    addAndMakeVisible (offsetSlider);

    engine.getDeviceManager().addChangeListener (this);
    refresh();

    setSize (1040, 640);
}

AudioSettingsComponent::~AudioSettingsComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
}

void AudioSettingsComponent::changed()
{
    if (onChanged) onChanged();
}

void AudioSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
    changed();
}

void AudioSettingsComponent::refresh()
{
    juce::StringArray inNames, outNames;
    juce::BigInteger activeIn, activeOut;
    juce::String status = "No audio device open.";

    if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
    {
        inNames = device->getInputChannelNames();
        outNames = device->getOutputChannelNames();
        activeIn = device->getActiveInputChannels();
        activeOut = device->getActiveOutputChannels();

        const double sr = device->getCurrentSampleRate();
        const int inLat = device->getInputLatencyInSamples(), outLat = device->getOutputLatencyInSamples();
        status = engine.getDeviceManager().getCurrentAudioDeviceType() + ", " + device->getName() + ": "
               + juce::String ((int) sr) + " Hz, " + juce::String (device->getCurrentBufferSizeSamples()) + " samples.  Reported latency in "
               + juce::String (inLat) + " + out " + juce::String (outLat) + " samples ("
               + juce::String (sr > 0 ? 1000.0 * (inLat + outLat) / sr : 0.0, 1) + " ms round trip).";
        if (engine.getRecordOffset() != 0)
            status << "  Recording compensation: " << (inLat + outLat + engine.getRecordOffset()) << " samples.";
    }

    latencyLabel.setText (status, juce::dontSendNotification);
    inputsPage->refresh (inNames, activeIn);
    outputsPage->refresh (outNames, activeOut);
}

void AudioSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (ConsoleColours::room);
    g.setColour (ConsoleColours::edge);
    g.fillRect (500, 10, 1, getHeight() - 20);
}

void AudioSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    selector.setBounds (area.removeFromLeft (480));
    area.removeFromLeft (20);

    latencyLabel.setBounds (area.removeFromBottom (40));
    auto offset = area.removeFromBottom (100);
    offsetTitle.setBounds (offset.removeFromTop (26));
    offsetSlider.setBounds (offset.removeFromTop (28).removeFromLeft (160));
    offset.removeFromTop (4);
    offsetHelp.setBounds (offset);

    area.removeFromBottom (10);
    tabs.setBounds (area);
}
