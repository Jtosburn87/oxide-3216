#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ConsoleLookAndFeel.h"
#include "ChannelStrip.h"
#include "Eq.h"
#include "Widgets.h"

/** Look of a channel strip's controls, shared by the console strips and the channel window. */
namespace StripStyle
{
    using namespace ConsoleColours;

    inline const juce::Colour muteRed  { 0xffd0473c };
    inline const juce::Colour gateCap  { 0xff3d6b50 };
    inline const juce::Colour compCap  { 0xffc9c6bb };
    inline const juce::Colour auxCap   { 0xffd9d6cc };

    inline juce::String formatHz (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (f));
    }

    /** Range printed at the ends of a frequency knob's sweep: Hz for LF, kHz for the others. */
    inline std::pair<juce::String, juce::String> freqEnds (const FourBandEq::BandSpec& s)
    {
        if (s.maxHz <= 1000.0f)
            return { juce::String (juce::roundToInt (s.minHz)), juce::String (juce::roundToInt (s.maxHz)) };
        const auto k = [] (float hz) { auto t = juce::String (hz / 1000.0f, 1); return t.endsWith (".0") ? t.dropLastCharacters (2) : t; };
        return { k (s.minHz), k (s.maxHz) };
    }

    /** titleIndent: room left of the title (for an In button on the title line). */
    inline void paintSection (juce::Graphics& g, const juce::String& title, juce::Rectangle<int> box, juce::Colour background,
                              int titleIndent = 0)
    {
        g.setColour (juce::Colour (0xff8e9598));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 2.0f, 1.0f);

        g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
        const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 6;
        const auto label = juce::Rectangle<int> (box.getX() + 6 + titleIndent, box.getY() - 6, w, 12);
        g.setColour (background);
        g.fillRect (label);
        g.setColour (legend);
        g.drawText (title, label, juce::Justification::centred, false);
    }

    inline void setupScribble (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::backgroundColourId, paper);
        l.setColour (juce::Label::textColourId, juce::Colour (0xff111111));
        l.setColour (juce::Label::outlineColourId, juce::Colours::black.withAlpha (0.35f));
        l.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
        l.setColour (juce::TextEditor::textColourId, juce::Colour (0xff111111));
        l.setColour (juce::Label::textWhenEditingColourId, juce::Colour (0xff111111));   // the editor takes its text colour from this
        l.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::white);
        l.setColour (juce::TextEditor::highlightColourId, amber.withAlpha (0.5f));
        l.setColour (juce::TextEditor::focusedOutlineColourId, focus);
    }

    // Scribble strip colours (ChannelStrip::colour). White is the default and groups nothing; the others group
    // the bank's faders that share them.
    inline constexpr int numStripColours = 9;
    inline const char* stripColourName (int c)
    {
        static const char* const names[numStripColours] = { "White", "Red", "Orange", "Yellow", "Green", "Blue", "Indigo", "Violet", "Black" };
        return names[juce::jlimit (0, numStripColours - 1, c)];
    }
    inline juce::Colour stripColour (int c)
    {
        static const juce::uint32 colours[numStripColours] = { 0xffe9e6da, 0xffd63a2f, 0xffef8a2e, 0xfff2d43a, 0xff3fa34d,
                                                               0xff2f6fd6, 0xff4b3a9e, 0xff8e44c4, 0xff151515 };
        return juce::Colour (colours[juce::jlimit (0, numStripColours - 1, c)]);
    }

    /** WCAG contrast ratio of two colours (1 = none, 21 = black on white). */
    inline float contrast (juce::Colour a, juce::Colour b)
    {
        const auto lum = [] (juce::Colour c)
        {
            const auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
        };
        const float la = lum (a), lb = lum (b);
        return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
    }

    /** The name's colour on a scribble strip colour: its opposite on the painter's wheel (red / green,
        orange / blue, yellow / violet, indigo / yellow-orange, white / black), made as dark (on a light strip) or as
        light (on a dark one) as it takes to read clearly; black or white if even that is not enough. */
    inline juce::Colour stripTextColour (int c)
    {
        static const juce::uint32 opposite[numStripColours] = { 0xff111111, 0xff3fa34d, 0xff2f6fd6, 0xff8e44c4, 0xffd63a2f,
                                                                0xffef8a2e, 0xfff2b33a, 0xfff2d43a, 0xffffffff };
        const auto bg = stripColour (c);
        const auto op = juce::Colour (opposite[juce::jlimit (0, numStripColours - 1, c)]);
        const bool lightStrip = contrast (bg, juce::Colours::black) > contrast (bg, juce::Colours::white);
        constexpr float readable = 4.5f;   // WCAG AA (mid colours such as green can't reach more with any text colour)
        for (int step = 0; step <= 20; ++step)
        {
            const float t = (float) step / 20.0f;
            const auto candidate = op.interpolatedWith (lightStrip ? juce::Colours::black : juce::Colours::white, t);
            if (contrast (bg, candidate) >= readable)
                return candidate;
        }
        return lightStrip ? juce::Colours::black : juce::Colours::white;
    }

    /** A scribble strip in its colour. */
    inline void colourScribble (juce::Label& l, int c)
    {
        l.setColour (juce::Label::backgroundColourId, stripColour (c));
        l.setColour (juce::Label::textColourId, stripTextColour (c));
    }

    inline void redButton (juce::Button& b)
    {
        b.setColour (juce::TextButton::buttonOnColourId, muteRed);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    inline juce::Rectangle<int> knobAt (int x, int y)      { return { x, y, Knob::preferredWidth, Knob::preferredHeight }; }
    inline juce::Rectangle<int> smallKnobAt (int x, int y) { return { x, y, Knob::smallWidth, Knob::smallHeight }; }

    /** The preamp controls of the Input section: phase, gain, trim and the gain / trim link. */
    inline void setupPreamp (ChannelStrip& s, Knob& gain, Knob& trim, ParamButton& phase, ParamButton& link)
    {
        gain.setEndLabels ("-20", "+30");
        gain.setOnUserSet ([&s] (float db) { s.turnGain (db); });
        trim.setEndLabels ("-30", "+20");
        phase.setButtonText (juce::CharPointer_UTF8 ("\xc3\x98"));   // the polarity sign, a 0 with a line through it
        phase.setTooltip ("Phase: flips the channel's polarity");
        link.setTooltip ("Gain / trim link: while it is lit, turning the gain turns the trim the other way, "
                         "so the preamp is driven harder or cleaner at the same level");
    }
}
