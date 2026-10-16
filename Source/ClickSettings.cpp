#include "ClickSettings.h"
#include "ConsoleLookAndFeel.h"
#include "StripStyle.h"

using namespace ConsoleColours;
using namespace StripStyle;

juce::Component::SafePointer<juce::DialogWindow> ClickSettings::window;

void ClickSettings::show (AudioEngine& engine)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ClickSettings (engine));
    o.dialogTitle = "Click";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    window = o.launchAsync();
}

ClickSettings::ClickSettings (AudioEngine& e)
    : engine (e),
      clickOn (*e.tempo.clickOn, "Click"),
      mainSend (*e.tempo.clickMain, "Main"),
      eqIn (*e.tempo.clickEqIn, "In"),
      level (*e.tempo.clickLevel, "Level", juce::Colour (0xffd6392d)),
      pan (*e.tempo.clickPan, "Pan", juce::Colour (0xffd9d6cc))
{
    auto& t = e.tempo;
    clickOn.setTooltip ("Click on every beat while the tape plays");
    mainSend.setTooltip ("Send the click to the main and monitor outs (it is never recorded or exported). "
                         "Off: only the aux sends below carry it.");
    eqIn.setTooltip ("Click EQ in / out");
    level.setFormatter ([] (float db) { return juce::String (juce::roundToInt (db)) + " dB"; });
    level.setEndLabels ("-40", "0");
    pan.setFormatter ([] (float p) { return std::abs (p) < 0.02f ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (p) * 100.0f)); });
    pan.setEndLabels ("L", "R");
    for (juce::Component* c : { (juce::Component*) &clickOn, (juce::Component*) &mainSend, (juce::Component*) &eqIn,
                                (juce::Component*) &level, (juce::Component*) &pan })
        addAndMakeVisible (c);

    for (int b = 0; b < FourBandEq::numBands; ++b)
    {
        const auto& spec = FourBandEq::bands[b];
        auto* g = eqGain.add (new Knob (*t.clickEq.gain[b], spec.label, juce::Colour (spec.capColour)));
        g->setEndLabels ("-", "+");
        auto* f = eqFreq.add (new Knob (*t.clickEq.freq[b], formatHz (spec.defHz), juce::Colour (spec.capColour)));
        f->setFormatter ([] (float hz) { return formatHz (hz); });
        const auto ends = freqEnds (spec);
        f->setEndLabels (ends.first, ends.second);
        addAndMakeVisible (g);
        addAndMakeVisible (f);
    }

    for (int k = 0; k < Config::numAux; ++k)
    {
        auto* a = auxSends.add (new Knob (*t.clickAux[(size_t) k], e.auxNames[(size_t) k], auxCap));
        a->setFormatter ([name = e.auxNames[(size_t) k]] (float v)
        {
            const float g = ChannelStrip::auxGain (v);
            return g <= 0.0001f ? name + " off" : juce::String (juce::Decibels::gainToDecibels (g), 1) + " dB";
        });
        a->setEndLabels ("off", "+");
        addAndMakeVisible (a);
    }

    setSize (300, 640);
}

void ClickSettings::resized()
{
    auto area = getLocalBounds().reduced (12, 10);

    levelBox = area.removeFromTop (24 + Knob::preferredHeight + 10);
    {
        auto r = levelBox.withTrimmedTop (24).reduced (8, 0);
        clickOn.setBounds (r.removeFromLeft (80).withSizeKeepingCentre (80, 26));
        pan.setBounds (r.removeFromRight (r.getWidth() / 2).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        level.setBounds (r.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
    }
    area.removeFromTop (10);

    eqBox = area.removeFromTop (24 + FourBandEq::numBands * (Knob::preferredHeight + 6) + 4);
    eqIn.setBounds (eqBox.getRight() - 46, eqBox.getY() + 3, 38, 18);
    {
        auto r = eqBox.withTrimmedTop (24).reduced (8, 0);
        for (int b = 0; b < FourBandEq::numBands; ++b)
        {
            auto row = r.removeFromTop (Knob::preferredHeight + 6);
            eqGain[b]->setBounds (row.removeFromLeft (row.getWidth() / 2).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
            eqFreq[b]->setBounds (row.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        }
    }
    area.removeFromTop (10);

    auxBox = area.removeFromTop (24 + (Config::numAux / 2) * (Knob::preferredHeight + 4) + 4);
    {
        auto r = auxBox.withTrimmedTop (24).reduced (8, 0);
        for (int row = 0; row < Config::numAux / 2; ++row)
        {
            auto line = r.removeFromTop (Knob::preferredHeight + 4);
            auxSends[2 * row]->setBounds (line.removeFromLeft (line.getWidth() / 2).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
            auxSends[2 * row + 1]->setBounds (line.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
        }
    }
    area.removeFromTop (10);

    mainBox = area.removeFromTop (48);
    mainSend.setBounds (mainBox.withTrimmedTop (16).reduced (40, 2));
}

void ClickSettings::paint (juce::Graphics& g)
{
    g.fillAll (stripGrey);   // a channel strip's grey
    paintSection (g, "Click", levelBox, stripGrey);
    paintSection (g, "EQ", eqBox, stripGrey);
    paintSection (g, "Aux sends (after the returns: headphone mixes)", auxBox, stripGrey);
    paintSection (g, "Main send (main and monitor outs)", mainBox, stripGrey);
}
