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
    limiterButton.setTooltip ("The talkback mic goes through the TB limiter. Patching TB Lim In on the patch bay breaks this: "
                              "the talkback then goes dry and the limiter works on what is patched in");
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

    note.setText ("Patch bay: TB In (patch the mic's interface input here) -> TB Limiter In -> TB Limiter Out -> TB Send "
                  "-> the auxes above, all normalled. Patch the mic straight into TB Send to bypass the limiter; patch anything "
                  "else into TB Limiter In to use the limiter on it (the talkback then goes dry).", juce::dontSendNotification);
    note.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    note.setColour (juce::Label::textColourId, legend.withAlpha (0.65f));
    note.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (note);

    setSize (440, 412);
    startTimerHz (30);
}

void TalkbackSettings::timerCallback()
{
    grMeter.setReduction (engine.takeTalkbackReduction());
}

void TalkbackSettings::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    routeBox = area.removeFromTop (98);
    {
        // Aux 1-4 over Aux 5-8.
        auto r = routeBox.reduced (10, 0).withTrimmedTop (22);
        const int perRow = 4, w = (r.getWidth() - (perRow - 1) * 6) / perRow;
        for (int i = 0; i < auxButtons.size(); ++i)
            auxButtons[i]->setBounds (r.getX() + (i % perRow) * (w + 6), r.getY() + (i / perRow) * 34, w, 28);
    }

    area.removeFromTop (12);
    limiterBox = area.removeFromTop (196);
    {
        // The switch, then the three knobs beside the gain reduction needle.
        auto r = limiterBox.reduced (10, 0).withTrimmedTop (22);
        limiterButton.setBounds (r.removeFromTop (28));
        r.removeFromTop (10);
        auto lower = r.removeFromTop (124);
        grMeter.setBounds (lower.removeFromRight (200).withSizeKeepingCentre (198, 120));
        auto knobs = lower.withSizeKeepingCentre (lower.getWidth(), 64);
        const int kw = knobs.getWidth() / 3;
        for (auto* k : { &inputKnob, &blendKnob, &makeupKnob })
            k->setBounds (knobs.removeFromLeft (kw).withSizeKeepingCentre (64, 60));
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
}
