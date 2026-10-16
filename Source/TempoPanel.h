#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"

/** Red 7-segment text in a black window, like the Scene readout: digits, '-', ' ', '.', ':', '|' and '/'. */
class SegmentDisplay : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    void setText (const juce::String&);
    const juce::String& getText() const noexcept { return text; }

    void paint (juce::Graphics&) override;

    /** Draws the text centred in an area, digits `digitHeight` tall. */
    static void paintText (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, float digitHeight);

protected:
    juce::String text;
    float digitScale = 0.66f;   // digit height as a share of the window's height
};

/** The tempo: drag up / down (Shift: fine), turn the mouse wheel, or double-click to type it. */
class BpmReadout : public SegmentDisplay
{
public:
    explicit BpmReadout (Tempo&);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

    static juce::String format (float bpm) { return juce::String (bpm, 1); }

private:
    Tempo& tempo;
    float dragStart = 120.0f;
    std::unique_ptr<juce::TextEditor> editor;
    void setBpm (float);
    void finishTyping (bool keep);
};

/** The time signature: click for a menu of common ones, and of beats a bar and beat units. */
class SignatureReadout : public SegmentDisplay
{
public:
    explicit SignatureReadout (Tempo&);
    void mouseUp (const juce::MouseEvent&) override;

private:
    Tempo& tempo;
};

/** The Tempo box in the centre section, where the bus compressor used to be: the tempo, the time signature,
    Tap, the song position (bar | beat | tick of the tape) and the click with its level. */
class TempoPanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit TempoPanel (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    Tempo& tempo;
    BpmReadout bpm;
    SignatureReadout signature;
    SegmentDisplay position;
    juce::TextButton tap { "Tap" };
    ParamButton click;
    Knob clickLevel;
    std::vector<double> taps;   // ms, the latest few

    juce::Rectangle<int> bpmLegend, sigLegend, positionLegend;

    void tapped();
    void timerCallback() override;
};
