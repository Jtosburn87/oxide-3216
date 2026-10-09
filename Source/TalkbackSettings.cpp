#include "TalkbackSettings.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

juce::Component::SafePointer<juce::DialogWindow> TalkbackSettings::window;

void TalkbackSettings::show (AudioEngine& engine)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new TalkbackSettings (engine));
    o.dialogTitle = "Talkback Settings";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    window = o.launchAsync();
}

TalkbackSettings::TalkbackSettings (AudioEngine& e)
    : engine (e),
      dimButton (*e.tbDim, "Dim the monitors while talking"),
      limiterButton (*e.tbThroughLimiter, "Talkback through the TB limiter"),
      inputKnob (*e.tbLimInput, "Input", juce::Colour (0xffd6392d)),
      blendKnob (*e.tbLimBlend, "Blend", juce::Colour (0xffd9d6cc)),
      makeupKnob (*e.tbLimMakeup, "Makeup", juce::Colour (0xff7b8185))
{
    for (int k = 0; k < AudioEngine::numTalkbackAux; ++k)
    {
        auto* b = auxButtons.add (new ParamButton (*e.tbToAux[(size_t) k], e.auxNames[(size_t) (AudioEngine::firstTalkbackAux + k)]));
        b->setTooltip ("Talkback into aux " + juce::String (AudioEngine::firstTalkbackAux + k + 1) + "'s output");
        addAndMakeVisible (b);
    }
    dimButton.setTooltip ("Turn the monitor outs down 15 dB while Talk is on, so the talkback mic does not feed back");
    limiterButton.setTooltip ("The talkback mic goes through the TB limiter. Patching TB Lim In on the patch bay breaks this: "
                              "the talkback then goes dry and the limiter works on what is patched in");
    addAndMakeVisible (dimButton);
    addAndMakeVisible (limiterButton);

    const auto db = [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    inputKnob.setFormatter (db);
    inputKnob.setEndLabels ("0", "+30");
    blendKnob.setFormatter ([] (float pc) { return juce::String (juce::roundToInt (pc)) + " %"; });
    blendKnob.setEndLabels ("Dry", "Wet");
    makeupKnob.setFormatter (db);
    makeupKnob.setEndLabels ("-12", "+24");
    for (auto* k : { &inputKnob, &blendKnob, &makeupKnob })
    {
        k->setDialSize (40);
        addAndMakeVisible (k);
    }
    addAndMakeVisible (grMeter);

    note.setText ("Patch the talkback mic into Talkback In on the patch bay (from an interface input). "
                  "TB Lim In and Out are there too, to use the limiter on anything else.", juce::dontSendNotification);
    note.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    note.setColour (juce::Label::textColourId, legend.withAlpha (0.65f));
    note.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (note);

    setSize (440, 362);
    startTimerHz (30);
}

void TalkbackSettings::timerCallback()
{
    grMeter.setReduction (engine.takeTalkbackReduction());
}

void TalkbackSettings::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    routeBox = area.removeFromTop (110);
    {
        auto r = routeBox.reduced (10, 0).withTrimmedTop (22);
        auto row = r.removeFromTop (28);
        const int w = (row.getWidth() - 3 * 6) / 4;
        for (auto* b : auxButtons)
        {
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
        }
        r.removeFromTop (10);
        dimButton.setBounds (r.removeFromTop (28));
    }

    area.removeFromTop (12);
    limiterBox = area.removeFromTop (170);
    {
        auto r = limiterBox.reduced (10, 0).withTrimmedTop (22);
        limiterButton.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        auto knobs = r.removeFromTop (64);
        const int kw = knobs.getWidth() / 3;
        for (auto* k : { &inputKnob, &blendKnob, &makeupKnob })
            k->setBounds (knobs.removeFromLeft (kw).withSizeKeepingCentre (64, 60));
        r.removeFromTop (14);
        grMeter.setBounds (r.removeFromTop (12).withTrimmedLeft (110));
    }

    area.removeFromTop (10);
    note.setBounds (area);
}

void TalkbackSettings::paint (juce::Graphics& g)
{
    g.fillAll (room);
    for (auto [box, title] : { std::pair { routeBox, juce::String ("Talkback to") },
                               std::pair { limiterBox, juce::String ("TB limiter (after the SSL 4000 E listen mic compressor)") } })
    {
        g.setColour (faderArea);
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xff2a2d2f));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        g.drawText (title, box.reduced (10, 4).withHeight (16), juce::Justification::centredLeft, false);
    }
    g.setColour (legend.withAlpha (0.75f));
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    g.drawText ("Gain reduction", grMeter.getBounds().withX (limiterBox.getX() + 10).withWidth (96).expanded (0, 2),
                juce::Justification::centredLeft, false);
}
