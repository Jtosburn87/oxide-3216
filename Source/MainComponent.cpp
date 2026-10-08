#include "MainComponent.h"
#include "AudioSettingsComponent.h"

using namespace ConsoleColours;

namespace
{
    constexpr int topBarH = 48, cheekW = 30, stripW = 66, mainW = 200;

    // Fader law from the prototype: 0..100, unity at 80, +15 dB at the top.
    float faderToGain (double v) { return v <= 0.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) ((v - 80.0) * 0.75)); }
}

//==============================================================================
void LedMeter::setLevel (float linearPeak)
{
    const float db = juce::Decibels::gainToDecibels (linearPeak, -100.0f);
    shownDb = db > shownDb ? db : juce::jmax (db, shownDb - 1.5f);   // fall about 45 dB/s at 30 fps

    if (db >= holdDb) { holdDb = db; holdFrames = 45; }
    else if (--holdFrames <= 0) holdDb = juce::jmax (db, holdDb - 1.5f);

    repaint();
}

void LedMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (r);

    // Segments: green to -18, amber to -6, red above, matching the prototype's 62% / 84% split over -60..0 dB.
    constexpr int segments = 30;
    const float segH = r.getHeight() / (float) segments;
    const auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float lit = norm (shownDb) * segments;
    const int holdSeg = (int) (norm (holdDb) * segments) - 1;

    for (int s = 0; s < segments; ++s)
    {
        const float frac = (float) s / segments;
        auto colour = frac < 0.62f ? meterGreen : frac < 0.84f ? meterAmber : meterRed;
        const bool on = (float) s < lit || (s == holdSeg && holdDb > -60.0f);
        g.setColour (on ? colour.brighter (0.25f) : colour.withMultipliedBrightness (0.22f));
        g.fillRect (r.getX(), r.getBottom() - (float) (s + 1) * segH + 1.0f, r.getWidth(), segH - 1.0f);
    }
}

//==============================================================================
MainComponent::MainComponent (AudioEngine& e, std::function<void()> save)
    : engine (e), saveSettings (std::move (save))
{
    audioButton.onClick = [this] { openAudioSettings(); };
    toneButton.setClickingTogglesState (true);
    toneButton.setTooltip ("Sends a 440 Hz tone at -18 dBFS to the main outputs");
    toneButton.onClick = [this] { engine.testToneOn.store (toneButton.getToggleState()); };
    for (auto* b : { &audioButton, &toneButton })
        addAndMakeVisible (b);

    statusLabel.setFont (ConsoleLookAndFeel::uiFont (14.0f));
    statusLabel.setColour (juce::Label::textColourId, bridgeText);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
    {
        addAndMakeVisible (inputMeters[(size_t) c]);

        auto& name = inputNames[(size_t) c];
        name.setText ("Input " + juce::String (c + 1), juce::dontSendNotification);
        name.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::backgroundColourId, paper);
        name.setColour (juce::Label::textColourId, juce::Colour (0xff111111));
        addAndMakeVisible (name);

        auto& src = inputSources[(size_t) c];
        src.setFont (ConsoleLookAndFeel::uiFont (12.0f));
        src.setJustificationType (juce::Justification::centred);
        src.setColour (juce::Label::textColourId, legend.withAlpha (0.7f));
        addAndMakeVisible (src);
    }

    for (auto& m : mainMeters)
        addAndMakeVisible (m);

    mainFader.setSliderStyle (juce::Slider::LinearVertical);
    mainFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    mainFader.setRange (0.0, 100.0);
    mainFader.setValue (80.0, juce::dontSendNotification);
    mainFader.setDoubleClickReturnValue (true, 80.0);
    mainFader.setTitle ("Main fader");
    mainFader.textFromValueFunction = [] (double v)
    {
        return v <= 0.0 ? juce::String ("-inf dB") : juce::String ((v - 80.0) * 0.75, 1) + " dB";
    };
    mainFader.valueFromTextFunction = [] (const juce::String& t)
    {
        return t.trim().startsWith ("-inf") ? 0.0 : juce::jlimit (0.0, 100.0, t.getDoubleValue() / 0.75 + 80.0);
    };
    mainFader.onValueChange = [this] { engine.mainGain.store (faderToGain (mainFader.getValue())); };
    mainFader.updateText();
    addAndMakeVisible (mainFader);

    mainLabel.setText ("Main", juce::dontSendNotification);
    mainLabel.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
    mainLabel.setJustificationType (juce::Justification::centred);
    mainLabel.setColour (juce::Label::backgroundColourId, paper);
    mainLabel.setColour (juce::Label::textColourId, juce::Colour (0xff111111));
    addAndMakeVisible (mainLabel);

    engine.getDeviceManager().addChangeListener (this);
    refreshLabels();
    startTimerHz (30);

    setSize (cheekW * 2 + stripW * AudioEngine::numConsoleInputs + mainW, 640);
}

MainComponent::~MainComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
    if (settingsWindow != nullptr)
        delete settingsWindow.getComponent();
}

void MainComponent::openAudioSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AudioSettingsComponent (engine, [this] { refreshLabels(); if (saveSettings) saveSettings(); }));
    o.dialogTitle = "Audio device";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    settingsWindow = o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshLabels();
}

void MainComponent::refreshLabels()
{
    juce::StringArray inNames;
    auto& dm = engine.getDeviceManager();

    if (auto* device = dm.getCurrentAudioDevice())
    {
        inNames = device->getInputChannelNames();
        deviceText = dm.getCurrentAudioDeviceType() + "  |  " + device->getName() + "  |  "
                   + juce::String ((int) device->getCurrentSampleRate()) + " Hz  |  "
                   + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
    }
    else
    {
        deviceText = "No audio device. Click Audio device to choose one.";
    }
    statusLabel.setText (deviceText, juce::dontSendNotification);

    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
    {
        const int src = engine.getInputSource (c);
        inputSources[(size_t) c].setText (src < 0 ? "--" : "in " + juce::String (src + 1), juce::dontSendNotification);
        inputSources[(size_t) c].setTooltip (juce::isPositiveAndBelow (src, inNames.size()) ? inNames[src] : juce::String());
    }
}

void MainComponent::timerCallback()
{
    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
        inputMeters[(size_t) c].setLevel (engine.takeInputPeak (c));
    for (int side = 0; side < 2; ++side)
        mainMeters[(size_t) side].setLevel (engine.takeMainPeak (side));

    // Twice a second: is audio running, CPU load, dropouts.
    if (++timerTicks % 15 == 0)
    {
        auto& dm = engine.getDeviceManager();
        if (dm.getCurrentAudioDevice() == nullptr)
            return;

        const auto count = engine.getCallbackCount();
        const bool running = count != lastCallbackCount;
        lastCallbackCount = count;

        const int xruns = dm.getXRunCount();
        statusLabel.setText (deviceText + "  |  "
                                 + (running ? "CPU " + juce::String (juce::roundToInt (dm.getCpuUsage() * 100.0)) + "%"
                                            : juce::String ("AUDIO STOPPED"))
                                 + (xruns > 0 ? "  |  dropouts " + juce::String (xruns) : juce::String()),
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, running ? bridgeText : meterRed.brighter (0.4f));
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bridge);

    // Top bar with name plate.
    auto bar = getLocalBounds().removeFromTop (topBarH);
    g.setColour (juce::Colour (0xff3a3f42));
    g.drawHorizontalLine (topBarH - 1, 0.0f, (float) getWidth());
    auto plateR = bar.reduced (14, 8).removeFromLeft (200).toFloat();
    g.setColour (plate);
    g.fillRect (plateR);
    g.setColour (edge);
    g.drawRect (plateR, 1.0f);
    g.setColour (juce::Colour (0xffe4e6e7));
    g.setFont (ConsoleLookAndFeel::uiFont (20.0f, true));
    g.drawText ("Console 32", plateR.withTrimmedLeft (12).withWidth (110), juce::Justification::centredLeft);
    g.setFont (ConsoleLookAndFeel::uiFont (15.0f));
    g.setColour (juce::Colour (0xffe4e6e7).withAlpha (0.8f));
    g.drawText ("Tape 16", plateR.withTrimmedLeft (122), juce::Justification::centredLeft);

    // End cheeks.
    auto body = getLocalBounds().withTrimmedTop (topBarH);
    for (auto cheek : { body.removeFromLeft (cheekW), body.removeFromRight (cheekW) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222324), (float) cheek.getX(), 0.0f,
                                                 juce::Colour (0xff060607), (float) cheek.getX() + cheekW * 0.45f, 0.0f, false));
        g.fillRect (cheek);
        g.setColour (juce::Colour (0xff3b3e41));
        g.drawVerticalLine (cheek.getX(), (float) cheek.getY(), (float) cheek.getBottom());
        g.drawVerticalLine (cheek.getRight() - 1, (float) cheek.getY(), (float) cheek.getBottom());
    }

    // Strip separators.
    g.setColour (juce::Colour (0xff2c2f31));
    for (int c = 0; c <= AudioEngine::numConsoleInputs; ++c)
        g.drawVerticalLine (cheekW + c * stripW, (float) body.getY(), (float) body.getBottom());
}

void MainComponent::resized()
{
    auto bar = getLocalBounds().removeFromTop (topBarH).reduced (14, 9);
    bar.removeFromLeft (214);
    audioButton.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (8);
    toneButton.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (8);
    statusLabel.setBounds (bar);

    auto body = getLocalBounds().withTrimmedTop (topBarH).reduced (0, 10);
    body.removeFromLeft (cheekW);
    body.removeFromRight (cheekW);

    for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
    {
        auto strip = body.removeFromLeft (stripW).reduced (6, 0);
        inputNames[(size_t) c].setBounds (strip.removeFromBottom (22));
        strip.removeFromBottom (4);
        inputSources[(size_t) c].setBounds (strip.removeFromBottom (18));
        strip.removeFromBottom (4);
        inputMeters[(size_t) c].setBounds (strip.withSizeKeepingCentre (10, strip.getHeight()));
    }

    auto main = body.reduced (14, 0);
    mainLabel.setBounds (main.removeFromBottom (22));
    main.removeFromBottom (8);
    auto meters = main.removeFromLeft (34);
    mainMeters[0].setBounds (meters.removeFromLeft (12));
    meters.removeFromLeft (6);
    mainMeters[1].setBounds (meters.removeFromLeft (12));
    main.removeFromLeft (10);
    mainFader.setBounds (main.withSizeKeepingCentre (juce::jmin (main.getWidth(), 80), main.getHeight()));
}
