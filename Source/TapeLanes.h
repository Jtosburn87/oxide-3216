#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"

/** The tape on the MIDI page: one lane per tape track, its whole waveform, and down the left the console channel
    the track plays into (name, colour, pan, solo, mute, fader) with the track's record arm. */
namespace TapeLanes
{
    /** The console strip a tape track's output reaches: its return strip while the normal is intact, otherwise
        the first strip a patch cable takes it to (a return strip, then an input strip). null: none. */
    struct Feed
    {
        ChannelStrip* strip = nullptr;
        bool isReturn = true;
        int index = -1;   // in its bank
        bool operator== (const Feed& o) const { return strip == o.strip; }
        bool operator!= (const Feed& o) const { return strip != o.strip; }
    };
    Feed feedOf (AudioEngine&, int track);

    /** The colour a waveform (and the number box) shows: the strip's scribble colour, white by default. */
    juce::Colour waveColour (const Feed&);

    /** A strip for the automation lanes: the number Automation knows it by, or -1. */
    int automationStrip (const Feed&);

    /** Tape lane heights: the default, and the limits of dragging a lane's bottom edge. */
    static constexpr int defaultHeight = 60, minHeight = 26, maxHeight = 400;
}

/** A peak overview of every tape track, kept up to date from the tape's change flags (message thread). Two levels:
    256 samples a bin, and 16384 for zoomed-out views. */
class TapePeaks
{
public:
    explicit TapePeaks (TapeMachine&);

    /** Reads the chunks that changed since the last call (at most `budget` of them; the rest next time). */
    void update (int budget = 48);

    /** The loudest sample (0..1) on a track in [from, to) tape samples. */
    float peak (int track, juce::int64 from, juce::int64 to) const noexcept;

    static constexpr int fineBits = 8, coarseBits = 14;

private:
    TapeMachine& tape;
    std::array<std::vector<std::uint8_t>, Config::numTracks> fine, coarse;
    std::vector<float> scratch;
    int nextTrack = 0;
    void readChunk (int track, int chunk);
};

/** One tape lane's header on the MIDI page. Its bottom edge drags to resize the lane. */
class TapeLaneHeader : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    TapeLaneHeader (AudioEngine&, int track);
    ~TapeLaneHeader() override;

    const int track;
    /** Dragging the bottom edge: the lane's new height. */
    std::function<void (int)> onResize;
    /** A colour was chosen (the session has changed). */
    std::function<void()> onChanged;
    /** The A button: show or hide the channel's automation under this lane. */
    std::function<void()> onToggleAutomation;
    /** The strip button: open the console channel's strip window. */
    std::function<void (const TapeLanes::Feed&)> onOpenStrip;

    /** Follows the patch (a different strip: its name, colour and controls). Call from a timer. */
    void refresh();
    void setAutomationOpen (bool);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    AudioEngine& engine;
    TapeLanes::Feed feed;
    int shownColour = -1;

    struct NumberBox : juce::Component, juce::SettableTooltipClient
    {
        juce::Colour colour, text;
        juce::String number;
        std::function<void()> onMenu;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onMenu) onMenu(); }
    } numberBox;

    juce::Label name;
    juce::TextButton openStrip { "Ch" }, automationButton { "A" };
    ArmButton arm;
    std::unique_ptr<ParamSlider> pan, fader;
    std::unique_ptr<ParamButton> solo, mute;
    juce::Label faderText;

    int dragFrom = -1, heightAtDown = 0;
    bool onEdge (juce::Point<int> p) const { return p.y >= getHeight() - 5; }
    void bindControls();
    void colourMenu();
};

/** The left end of a channel's three automation lanes (Fader, Pan, Mute) on the MIDI/WAV page: their names and the
    strip's automation mode. Right-click a lane: clear it. */
class AutoLaneHeader : public juce::Component
{
public:
    explicit AutoLaneHeader (Automation&);

    static constexpr int rowH = 34, height = 3 * rowH;
    static const char* targetName (int target);

    /** The strip shown (-1: this track reaches no channel). Call from a timer. */
    void setStrip (int strip, const juce::String& name);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Automation& automation;
    int strip = -1;
    Automation::Mode shownMode = Automation::Mode::off;
    juce::String stripName;
    juce::TextButton mode;
    void showModeMenu();
    void showMode();
};
