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

AudioSettingsComponent::AudioSettingsComponent (AudioEngine& e, std::function<void()> changed)
    : engine (e),
      onChanged (std::move (changed)),
      selector (e.getDeviceManager(), 0, AudioEngine::maxDeviceChannels, 0, AudioEngine::maxDeviceChannels,
                false, false, false, false)
{
    addAndMakeVisible (selector);

    inputsTitle.setText ("Input channels: which interface input feeds each console channel", juce::dontSendNotification);
    outputsTitle.setText ("Main outputs", juce::dontSendNotification);
    for (auto* l : { &inputsTitle, &outputsTitle })
    {
        l->setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
        addAndMakeVisible (*l);
    }
    latencyLabel.setFont (ConsoleLookAndFeel::uiFont (13.0f));
    latencyLabel.setColour (juce::Label::textColourId, ConsoleColours::legend.withAlpha (0.75f));
    addAndMakeVisible (latencyLabel);

    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
    {
        auto* label = inputLabels.add (new juce::Label ({}, "Input " + juce::String (c + 1)));
        label->setFont (ConsoleLookAndFeel::uiFont (14.0f));
        addAndMakeVisible (label);

        auto* box = inputBoxes.add (new juce::ComboBox ("Source for input " + juce::String (c + 1)));
        box->setTitle ("Source for input channel " + juce::String (c + 1));
        box->onChange = [this, c, box]
        {
            engine.setInputSource (c, channelForId (box->getSelectedId()));
            if (onChanged) onChanged();
        };
        addAndMakeVisible (box);
    }

    for (int side = 0; side < 2; ++side)
    {
        mainLabels[side].setText (side == 0 ? "Main L" : "Main R", juce::dontSendNotification);
        mainLabels[side].setFont (ConsoleLookAndFeel::uiFont (14.0f));
        addAndMakeVisible (mainLabels[side]);

        mainBoxes[side].setTitle (side == 0 ? "Main left output" : "Main right output");
        mainBoxes[side].onChange = [this, side]
        {
            engine.setMainOutput (side, channelForId (mainBoxes[side].getSelectedId()));
            if (onChanged) onChanged();
        };
        addAndMakeVisible (mainBoxes[side]);
    }

    autoMapButton.setTooltip ("Input channel 1 from interface input 1, channel 2 from input 2, and so on");
    autoMapButton.onClick = [this] { autoMap(); };
    addAndMakeVisible (autoMapButton);

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
        refreshChannelLists();
        if (onChanged) onChanged();
    };
    addAndMakeVisible (offsetSlider);

    engine.getDeviceManager().addChangeListener (this);
    refreshChannelLists();

    setSize (1040, 620);
}

AudioSettingsComponent::~AudioSettingsComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
}

void AudioSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshChannelLists();
    if (onChanged) onChanged();
}

void AudioSettingsComponent::refreshChannelLists()
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
        const int buffer = device->getCurrentBufferSizeSamples();
        const int inLat = device->getInputLatencyInSamples();
        const int outLat = device->getOutputLatencyInSamples();
        status = engine.getDeviceManager().getCurrentAudioDeviceType() + ", " + device->getName() + ": "
               + juce::String ((int) sr) + " Hz, " + juce::String (buffer) + " samples.  Reported latency in "
               + juce::String (inLat) + " + out " + juce::String (outLat) + " samples ("
               + juce::String (sr > 0 ? 1000.0 * (inLat + outLat) / sr : 0.0, 1) + " ms round trip).";
        if (engine.getRecordOffset() != 0)
            status << "  Recording compensation: " << (inLat + outLat + engine.getRecordOffset()) << " samples.";
    }

    latencyLabel.setText (status, juce::dontSendNotification);

    for (int c = 0; c < inputBoxes.size(); ++c)
        fillChannelBox (*inputBoxes[c], inNames, activeIn, engine.getInputSource (c));
    for (int side = 0; side < 2; ++side)
        fillChannelBox (mainBoxes[side], outNames, activeOut, engine.getMainOutput (side));
}

void AudioSettingsComponent::autoMap()
{
    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
        engine.setInputSource (c, c);
    refreshChannelLists();
    if (onChanged) onChanged();
}

void AudioSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (ConsoleColours::room);
    g.setColour (ConsoleColours::edge);
    g.drawVerticalLine (500, 10.0f, (float) getHeight() - 10.0f);
}

void AudioSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    selector.setBounds (area.removeFromLeft (480));
    area.removeFromLeft (20);

    latencyLabel.setBounds (area.removeFromBottom (40));

    inputsTitle.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);

    const int rowH = 28, colGap = 16;
    auto grid = area.removeFromTop (rowH * 8);
    const int colW = (grid.getWidth() - colGap) / 2;
    for (int c = 0; c < inputBoxes.size(); ++c)
    {
        const int col = c / 8, row = c % 8;
        auto cell = juce::Rectangle<int> (grid.getX() + col * (colW + colGap), grid.getY() + row * rowH, colW, rowH).reduced (0, 2);
        inputLabels[c]->setBounds (cell.removeFromLeft (64));
        inputBoxes[c]->setBounds (cell);
    }

    area.removeFromTop (8);
    autoMapButton.setBounds (area.removeFromTop (26).removeFromLeft (120));
    area.removeFromTop (18);

    outputsTitle.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);
    for (int side = 0; side < 2; ++side)
    {
        auto row = area.removeFromTop (rowH).reduced (0, 2).removeFromLeft (colW);
        mainLabels[side].setBounds (row.removeFromLeft (64));
        mainBoxes[side].setBounds (row);
    }

    area.removeFromTop (18);
    offsetTitle.setBounds (area.removeFromTop (26));
    offsetSlider.setBounds (area.removeFromTop (28).removeFromLeft (160));
    area.removeFromTop (4);
    offsetHelp.setBounds (area.removeFromTop (40));
}
