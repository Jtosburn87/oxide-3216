#include "TapeLanes.h"
#include "StripStyle.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

//==============================================================================
TapeLanes::Feed TapeLanes::feedOf (AudioEngine& e, int track)
{
    using P = AudioEngine::Patch;
    const int source = P::tapeOut + track;
    // The normal: tape out N into return strip N, unless a cable or an interface input is in that jack.
    if (juce::isPositiveAndBelow (track, e.returnStrips.size())
        && ! e.getPatch (P::returnIn + track).any() && e.getInterfaceInput (P::returnIn + track) < 0)
        return { e.returnStrips[track], true, track };
    for (int r = 0; r < e.returnStrips.size(); ++r)
        if (e.getPatch (P::returnIn + r).test (source))
            return { e.returnStrips[r], true, r };
    for (int c = 0; c < e.inputStrips.size(); ++c)
        if (e.getPatch (P::channelIn + c).test (source))
            return { e.inputStrips[c], false, c };
    return {};
}

juce::Colour TapeLanes::waveColour (const Feed& f)
{
    if (f.strip == nullptr)
        return StripStyle::stripColour (0);
    const int c = f.strip->scribbleColour;
    // Black would vanish on the tape: a dark grey that still shows.
    return c == StripStyle::numStripColours - 1 ? juce::Colour (0xff5a5d60) : StripStyle::stripColour (c);
}

//==============================================================================
TapePeaks::TapePeaks (TapeMachine& t) : tape (t), scratch ((size_t) TapeMachine::chunkSize)
{
}

void TapePeaks::update (int budget)
{
    const int maxChunks = tape.getMaxChunks();
    constexpr int fineBins = TapeMachine::chunkSize >> fineBits, coarseBins = TapeMachine::chunkSize >> coarseBits;
    for (int k = 0; k < Config::numTracks && budget > 0; ++k)
    {
        const int t = (nextTrack + k) % Config::numTracks;
        if (tape.takePeakCleared (t))
        {
            fine[(size_t) t].clear();
            coarse[(size_t) t].clear();
            // Whatever is on the track now (a loaded take) has its chunks flagged, so it is read again below.
        }
        for (int c = 0; c < maxChunks && budget > 0; ++c)
        {
            if (! tape.takePeakChanged (t, c))
                continue;
            auto& f = fine[(size_t) t];
            auto& co = coarse[(size_t) t];
            if ((int) f.size() < (c + 1) * fineBins)
            {
                f.resize ((size_t) ((c + 1) * fineBins), 0);
                co.resize ((size_t) ((c + 1) * coarseBins), 0);
            }
            readChunk (t, c);
            --budget;
        }
        if (budget <= 0)
            nextTrack = t;   // start here next time, so one busy track can't keep the others waiting
    }
}

void TapePeaks::readChunk (int track, int chunk)
{
    tape.readTrack (track, (juce::int64) chunk * TapeMachine::chunkSize, scratch.data(), TapeMachine::chunkSize);
    constexpr int fineLen = 1 << fineBits, coarseLen = 1 << coarseBits;
    auto* f = fine[(size_t) track].data() + (size_t) chunk * (TapeMachine::chunkSize >> fineBits);
    auto* co = coarse[(size_t) track].data() + (size_t) chunk * (TapeMachine::chunkSize >> coarseBits);
    std::uint8_t coarseMax = 0;
    for (int b = 0; b < (TapeMachine::chunkSize >> fineBits); ++b)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (scratch.data() + b * fineLen, fineLen);
        const float p = juce::jmin (1.0f, juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())));
        const auto v = (std::uint8_t) std::ceil (p * 255.0f);
        f[b] = v;
        coarseMax = juce::jmax (coarseMax, v);
        if (((b + 1) * fineLen) % coarseLen == 0)
        {
            co[((b + 1) * fineLen) / coarseLen - 1] = coarseMax;
            coarseMax = 0;
        }
    }
}

float TapePeaks::peak (int track, juce::int64 from, juce::int64 to) const noexcept
{
    if (to <= from || from < 0)
        return 0.0f;
    const bool useCoarse = to - from >= ((juce::int64) 4 << coarseBits);
    const auto& bins = useCoarse ? coarse[(size_t) track] : fine[(size_t) track];
    const int bits = useCoarse ? coarseBits : fineBits;
    const auto b0 = from >> bits;
    const auto b1 = juce::jmin ((juce::int64) bins.size(), ((to - 1) >> bits) + 1);
    std::uint8_t m = 0;
    for (auto b = b0; b < b1; ++b)
        m = juce::jmax (m, bins[(size_t) b]);
    return m / 255.0f;
}

//==============================================================================
void TapeLaneHeader::NumberBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colour);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    g.setColour (text);
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
    g.drawText (number, getLocalBounds(), juce::Justification::centred, false);
}

TapeLaneHeader::TapeLaneHeader (AudioEngine& e, int t)
    : track (t), engine (e), arm (e.tape, t)
{
    setMouseClickGrabsKeyboardFocus (false);   // a click on the header must not hand the keys to its pan knob
    numberBox.number = juce::String (t + 1);
    numberBox.setTooltip ("Tape track " + juce::String (t + 1) + ". Right-click: the colour of the channel it plays into (as in the mix view)");
    numberBox.onMenu = [this] { colourMenu(); };
    addAndMakeVisible (numberBox);

    name.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
    name.setColour (juce::Label::textColourId, legend);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    openStrip.setTooltip ("Open this channel's strip window (EQ, gate, compressor, sends...)");
    openStrip.onClick = [this] { if (feed.strip != nullptr && onOpenStrip) onOpenStrip (feed); };
    addAndMakeVisible (openStrip);

    automationButton.setTooltip ("Automation: show this channel's fader, pan and mute lanes under the track");
    automationButton.setClickingTogglesState (false);
    automationButton.setColour (juce::TextButton::buttonOnColourId, amber);
    automationButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1b1b1b));
    automationButton.onClick = [this] { if (onToggleAutomation) onToggleAutomation(); };
    addAndMakeVisible (automationButton);

    arm.setButtonText ("R");
    arm.setTooltip ("Record arm, as on the tape machine: grey off, yellow plays the tape until Record, red takes the input");
    addAndMakeVisible (arm);

    faderText.setFont (ConsoleLookAndFeel::uiFont (11.0f));
    faderText.setColour (juce::Label::textColourId, juce::Colour (0xffaeb3b5));
    faderText.setJustificationType (juce::Justification::centredRight);
    faderText.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (faderText);

    refresh();
}

TapeLaneHeader::~TapeLaneHeader() = default;

void TapeLaneHeader::setAutomationOpen (bool open)
{
    automationButton.setToggleState (open, juce::dontSendNotification);
}

int TapeLanes::automationStrip (const Feed& f)
{
    return f.strip != nullptr ? Automation::stripIndex (f.isReturn, f.index) : -1;
}

//==============================================================================
const char* AutoLaneHeader::targetName (int target)
{
    return target == Automation::fader ? "Fader" : target == Automation::pan ? "Pan" : "Mute";
}

AutoLaneHeader::AutoLaneHeader (Automation& a) : automation (a)
{
    setMouseClickGrabsKeyboardFocus (false);
    mode.setTooltip ("Automation mode of this channel. Off: the curves do nothing. Read: they move the controls. "
                     "Touch: move a control while the tape rolls to write it until you let go. "
                     "Latch: the same, until the tape stops.");
    mode.onClick = [this] { showModeMenu(); };
    addAndMakeVisible (mode);
}

void AutoLaneHeader::setStrip (int s, const juce::String& n)
{
    const auto m = s >= 0 ? automation.getMode (s) : Automation::Mode::off;
    if (s == strip && n == stripName && m == shownMode)
        return;
    strip = s;
    stripName = n;
    shownMode = m;
    showMode();
    mode.setVisible (strip >= 0);
    repaint();
}

void AutoLaneHeader::showMode()
{
    const auto m = shownMode;
    mode.setButtonText (m == Automation::Mode::off ? "Off" : m == Automation::Mode::read ? "Read" : m == Automation::Mode::touch ? "Touch" : "Latch");
    const bool writes = m == Automation::Mode::touch || m == Automation::Mode::latch;
    mode.setColour (juce::TextButton::buttonColourId, writes ? juce::Colour (0xffb5372c) : m == Automation::Mode::read ? juce::Colour (0xff2c6b3a) : juce::Colour (0xff3a3d40));
    mode.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

void AutoLaneHeader::showModeMenu()
{
    if (strip < 0)
        return;
    juce::PopupMenu menu;
    const char* names[] = { "Off", "Read", "Touch", "Latch" };
    for (int i = 0; i < 4; ++i)
        menu.addItem (i + 1, names[i], true, (int) automation.getMode (strip) == i);
    juce::Component::SafePointer<AutoLaneHeader> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mode), [self] (int result)
    {
        if (self != nullptr && result > 0 && self->strip >= 0)
        {
            self->automation.setMode (self->strip, (Automation::Mode) (result - 1));
            self->setStrip (self->strip, self->stripName);
        }
    });
}

void AutoLaneHeader::resized()
{
    mode.setBounds (getWidth() - 62, 6, 54, rowH - 12);
}

void AutoLaneHeader::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1f2123));
    for (int t = 0; t < Automation::numTargets; ++t)
    {
        const auto row = juce::Rectangle<int> (0, t * rowH, getWidth(), rowH);
        g.setColour (juce::Colour (0xff35383b));
        g.fillRect (row.withTop (row.getBottom() - 1));
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (12.0f, true));
        g.drawText (targetName (t), row.withTrimmedLeft (14).withWidth (60), juce::Justification::centredLeft, false);
    }
    g.setColour (juce::Colour (0xffaeb3b5));
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
    g.drawText (strip >= 0 ? stripName : juce::String ("Not on a channel"), juce::Rectangle<int> (70, 0, getWidth() - 136, rowH), juce::Justification::centredLeft, true);
}

void AutoLaneHeader::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || strip < 0)
        return;
    const int target = juce::jlimit (0, Automation::numTargets - 1, e.y / rowH);
    juce::PopupMenu menu;
    menu.addItem (1, "Clear " + juce::String (targetName (target)) + " automation", ! automation.getCurve (strip, (Automation::Target) target).empty());
    menu.addItem (2, "Clear all of this channel's automation", automation.hasCurves (strip));
    juce::Component::SafePointer<AutoLaneHeader> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [self, target] (int result)
    {
        if (self == nullptr || self->strip < 0)
            return;
        if (result == 1)      self->automation.clearCurve (self->strip, (Automation::Target) target);
        else if (result == 2) self->automation.clearStrip (self->strip);
    });
}

void TapeLaneHeader::bindControls()
{
    pan.reset();
    fader.reset();
    solo.reset();
    mute.reset();
    if (feed.strip == nullptr)
        return;
    auto& c = feed.strip->controls;

    pan = std::make_unique<ParamSlider> (*c.pan);
    pan->setSliderStyle (juce::Slider::RotaryVerticalDrag);
    pan->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    pan->setMouseDragSensitivity (160);
    pan->getProperties().set ("compact", true);
    pan->setTooltip ("Pan (the console channel's own). Double-click: centre");
    addAndMakeVisible (*pan);

    fader = std::make_unique<ParamSlider> (*c.fader);
    fader->setSliderStyle (juce::Slider::LinearHorizontal);
    fader->setColour (juce::Slider::trackColourId, juce::Colour (0xff6b7175));
    fader->setColour (juce::Slider::backgroundColourId, juce::Colour (0xff111213));
    fader->setColour (juce::Slider::thumbColourId, juce::Colour (0xffd6d9da));
    fader->setTooltip ("Fader (the console channel's own). Double-click: back to its default");
    fader->onValueChange = [this] { refresh(); };
    addAndMakeVisible (*fader);

    solo = std::make_unique<ParamButton> (*c.solo, "S");
    mute = std::make_unique<ParamButton> (*c.mute, "M");
    solo->setTooltip ("Solo the console channel");
    mute->setTooltip ("Mute the console channel");
    addAndMakeVisible (*solo);
    addAndMakeVisible (*mute);
    resized();
}

void TapeLaneHeader::refresh()
{
    const auto now = TapeLanes::feedOf (engine, track);
    if (now != feed)
    {
        feed = now;
        bindControls();
    }
    const auto text = feed.strip == nullptr ? "Tape " + juce::String (track + 1) + "  (not patched)"
                                            : feed.strip->name.isNotEmpty() ? feed.strip->name
                                            : (feed.isReturn ? "Return " : "Channel ") + juce::String (feed.index + 1);
    if (name.getText() != text)
        name.setText (text, juce::dontSendNotification);
    name.setTooltip (feed.strip == nullptr ? juce::String ("Tape out " + juce::String (track + 1) + " is not patched into a console channel")
                                           : "Plays into " + juce::String (feed.isReturn ? "return " : "input channel ") + juce::String (feed.index + 1));
    const int colour = feed.strip == nullptr ? 0 : feed.strip->scribbleColour;
    if (colour != shownColour)
    {
        shownColour = colour;
        numberBox.colour = StripStyle::stripColour (colour);
        numberBox.text = StripStyle::stripTextColour (colour);
        numberBox.repaint();
        repaint();
    }
    if (fader != nullptr)
    {
        const float pos = feed.strip->controls.fader->get();
        const auto t = pos <= 0.0f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (Dsp::faderDb (pos), 1) + " dB";
        if (faderText.getText() != t)
            faderText.setText (t, juce::dontSendNotification);
    }
    else
        faderText.setText ({}, juce::dontSendNotification);
}

void TapeLaneHeader::resized()
{
    // Short lanes keep the top row only (the fader goes).
    auto r = getLocalBounds().reduced (6, getHeight() < 40 ? 2 : 5).withTrimmedLeft (4).withTrimmedBottom (3);
    auto top = r.removeFromTop (juce::jmin (22, r.getHeight()));
    numberBox.setBounds (top.removeFromLeft (26));
    top.removeFromLeft (6);
    // R M S, as on the MIDI tracks.
    if (mute != nullptr)
    {
        solo->setBounds (top.removeFromRight (24));
        top.removeFromRight (3);
        mute->setBounds (top.removeFromRight (24));
        top.removeFromRight (3);
    }
    arm.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    if (pan != nullptr)
    {
        pan->setBounds (top.removeFromRight (top.getHeight() + 2));
        top.removeFromRight (4);
    }
    automationButton.setBounds (top.removeFromRight (20));
    top.removeFromRight (3);
    openStrip.setVisible (feed.strip != nullptr);
    openStrip.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    name.setBounds (top);

    r.removeFromTop (4);
    const bool showFader = fader != nullptr && r.getHeight() >= 16;
    if (fader != nullptr)
        fader->setVisible (showFader);
    faderText.setVisible (showFader);
    if (showFader)
    {
        auto row = r.removeFromTop (18);
        faderText.setBounds (row.removeFromRight (56));
        row.removeFromLeft (32);
        fader->setBounds (row);
    }
}

void TapeLaneHeader::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2c2f));
    // The guard band under the lane, as on the tape.
    g.setColour (juce::Colour (0xff3d3f42));
    g.fillRect (0, getHeight() - 3, getWidth(), 3);
    g.setColour (TapeLanes::waveColour (feed));
    g.fillRect (2, 4, 3, getHeight() - 10);
}

void TapeLaneHeader::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (onEdge (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void TapeLaneHeader::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = onEdge (e.getPosition()) ? e.getScreenY() : -1;
    heightAtDown = getHeight();
}

void TapeLaneHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom >= 0 && onResize)
        onResize (juce::jlimit (TapeLanes::minHeight, TapeLanes::maxHeight, heightAtDown + e.getScreenY() - dragFrom));
}

void TapeLaneHeader::mouseUp (const juce::MouseEvent&)
{
    dragFrom = -1;
}

void TapeLaneHeader::colourMenu()
{
    if (feed.strip == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Channel colour");
    for (int c = 0; c < StripStyle::numStripColours; ++c)
    {
        auto swatch = std::make_unique<juce::DrawableRectangle>();
        swatch->setRectangle (juce::Parallelogram<float> (juce::Rectangle<float> (0.0f, 0.0f, 16.0f, 16.0f)));
        swatch->setFill (StripStyle::stripColour (c));
        swatch->setStrokeFill (juce::Colours::black.withAlpha (0.6f));
        swatch->setStrokeThickness (1.0f);
        menu.addItem (juce::PopupMenu::Item (juce::String (StripStyle::stripColourName (c)) + (c == 0 ? "  (no group)" : ""))
                          .setID (c + 1).setImage (std::move (swatch)).setTicked (feed.strip->scribbleColour == c));
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&numberBox),
                        [safe = juce::Component::SafePointer<TapeLaneHeader> (this)] (int r)
                        {
                            if (safe == nullptr || r <= 0 || safe->feed.strip == nullptr)
                                return;
                            safe->feed.strip->scribbleColour = r - 1;
                            safe->refresh();
                            if (safe->onChanged)
                                safe->onChanged();
                        });
}
