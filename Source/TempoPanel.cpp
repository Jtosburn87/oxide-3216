#include "TempoPanel.h"
#include "SceneWindow.h"
#include "ClickSettings.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    const juce::Colour segOn { 0xffff3b24 };
}

//==============================================================================
void SegmentDisplay::setText (const juce::String& t)
{
    if (t != text)
    {
        text = t;
        repaint();
    }
}

void SegmentDisplay::paintText (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& t, float dh)
{
    const float dw = dh * 0.56f, gap = dw * 0.3f;
    const auto widthOf = [&] (juce::juce_wchar c)
    {
        switch (c)
        {
            case '.': case ':': return dw * 0.35f;
            case '|':           return dw * 0.9f;
            case '/':           return dw * 0.8f;
            default:            return dw;
        }
    };
    float total = 0.0f;
    for (auto c : t)
        total += widthOf (c) + gap;
    total -= gap;

    float x = area.getCentreX() - total * 0.5f;
    const float y = area.getCentreY() - dh * 0.5f;
    for (auto c : t)
    {
        const float w = widthOf (c);
        g.setColour (segOn);
        if (c == '.')
            g.fillRect (juce::Rectangle<float> (dw * 0.17f, dw * 0.17f).withCentre ({ x + w * 0.5f, y + dh - dw * 0.09f }));
        else if (c == ':')
        {
            for (float fy : { 0.3f, 0.72f })
                g.fillRect (juce::Rectangle<float> (dw * 0.17f, dw * 0.17f).withCentre ({ x + w * 0.5f, y + dh * fy }));
        }
        else if (c == '|')
        {
            g.setColour (segOn.withAlpha (0.3f));   // dimmer and thinner than a 1
            g.fillRect (juce::Rectangle<float> (juce::jmax (1.0f, dw * 0.05f), dh * 1.05f).withCentre ({ x + w * 0.5f, y + dh * 0.5f }));
        }
        else if (c == '/')
            g.drawLine (x + w * 0.85f, y + dh * 0.05f, x + w * 0.15f, y + dh * 0.95f, dw * 0.15f);
        else
            SceneReadout::paintDigit (g, { x, y, dw, dh }, c);
        x += w + gap;
    }
}

void SegmentDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050607));
    g.fillRect (r);
    g.setColour (juce::Colour (0xff1d1f20));
    g.drawRect (r, 1.0f);
    paintText (g, r.reduced (6.0f, 0.0f), text, r.getHeight() * digitScale);
}

//==============================================================================
BpmReadout::BpmReadout (Tempo& t) : tempo (t)
{
    setTitle ("Tempo");
    setTooltip ("Tempo in beats a minute: drag up or down (Shift: fine), turn the mouse wheel, or double-click to type it");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    digitScale = 0.62f;
}

void BpmReadout::setBpm (float v)
{
    tempo.bpm->set (std::round (juce::jlimit (Tempo::minBpm, Tempo::maxBpm, v) * 10.0f) / 10.0f);
}

void BpmReadout::mouseDown (const juce::MouseEvent&)
{
    if (tempo.hasMap())
        return;
    dragStart = tempo.bpm->get();
}

void BpmReadout::mouseDrag (const juce::MouseEvent& e)
{
    if (tempo.hasMap())
        return;   // the tempo map sets the tempo
    const float perPixel = e.mods.isShiftDown() ? 0.05f : 0.5f;   // 1 aposition.setTooltip ("Where the tape is, in bar | beat | tick (960 ticks a quarter note).a minute for 2 pixels; fine: 0.1 for 2
    setBpm (dragStart - (float) e.getDistanceFromDragStartY() * perPixel);
}

void BpmReadout::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (tempo.hasMap())
        return;
    const float step = e.mods.isShiftDown() ? 0.1f : 1.0f;
    setBpm (tempo.bpm->get() + (w.deltaY > 0.0f ? step : -step));
}

void BpmReadout::mouseDoubleClick (const juce::MouseEvent&)
{
    if (tempo.hasMap())
        return;
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (ConsoleLookAndFeel::uiFont ((float) getHeight() * 0.5f, true));
    editor->setJustification (juce::Justification::centred);
    editor->setInputRestrictions (5, "0123456789.");
    editor->setText (format (tempo.bpm->get()), false);
    editor->selectAll();
    editor->onReturnKey = [this] { finishTyping (true); };
    editor->onEscapeKey = [this] { finishTyping (false); };
    editor->onFocusLost = [this] { finishTyping (true); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
}

void BpmReadout::finishTyping (bool keep)
{
    if (editor == nullptr || ! editor->isVisible())   // Return, then the focus leaving
        return;
    const auto typed = editor->getText().trim();
    if (keep && typed.isNotEmpty())
        setBpm (typed.getFloatValue());
    // Deleting the editor from inside its own callback is not safe: let it go once the callback is over.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<BpmReadout> (this)] { if (safe != nullptr) safe->editor = nullptr; });
    editor->setVisible (false);
}

void BpmReadout::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds().reduced (4));
}

//==============================================================================
SignatureReadout::SignatureReadout (Tempo& t) : tempo (t)
{
    setTitle ("Time signature");
    setTooltip ("Time signature: click to choose");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SignatureReadout::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()) || tempo.hasMap())
        return;
    juce::PopupMenu menu, beats, units;
    const int nowBeats = tempo.getBeatsPerBar(), nowUnit = tempo.getBeatUnit();
    static constexpr std::pair<int, int> common[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
    int id = 1;
    for (auto [b, u] : common)
        menu.addItem (id++, juce::String (b) + "/" + juce::String (u), true, b == nowBeats && u == nowUnit);
    for (int b = 1; b <= 16; ++b)
        beats.addItem (100 + b, juce::String (b), true, b == nowBeats);
    for (int u : { 2, 4, 8, 16 })
        units.addItem (200 + u, juce::String (u), true, u == nowUnit);
    menu.addSeparator();
    menu.addSubMenu ("Beats a bar", beats);
    menu.addSubMenu ("Beat unit", units);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<SignatureReadout> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& t = safe->tempo;
                            if (result < 100)
                                t.setSignature (common[result - 1].first, common[result - 1].second);
                            else if (result < 200)
                                t.setSignature (result - 100, t.getBeatUnit());
                            else
                                t.setSignature (t.getBeatsPerBar(), result - 200);
                        });
}

//==============================================================================
TempoPanel::TempoPanel (AudioEngine& e)
    : engine (e), tempo (e.tempo), bpm (e.tempo), signature (e.tempo),
      click (*e.tempo.clickOn, "Click"),
      clickLevel (*e.tempo.clickLevel, "Level", juce::Colour (0xffd6392d))
{
    for (juce::Component* c : { (juce::Component*) &bpm, (juce::Component*) &signature, (juce::Component*) &position,
                                (juce::Component*) &tap, (juce::Component*) &click, (juce::Component*) &clickLevel })
        addAndMakeVisible (c);

    position.setTitle ("Song position");
    position.setTooltip ("Where the tape is, in bar | beat | tick (960 ticks a beat). Bar 1 is the start of the tape.");
    tap.setTooltip ("Tap the beat (four taps or more) to set the tempo");
    tap.onClick = [this] { tapped(); };
    click.setTooltip ("Click on every beat while the tape plays (higher on the first beat of the bar). "
                      "On the main and monitor outs only: never recorded or exported.");
    clickLevel.setFormatter ([] (float db) { return juce::String (juce::roundToInt (db)) + " dB"; });
    clickLevel.setEndLabels ("-40", "0");
    clickSetup.setTooltip ("The click's strip: level, pan, EQ, sends to Aux 1-8 (headphone mixes) and the main send");
    clickSetup.setTitle ("Click setup");
    clickSetup.onClick = [this] { ClickSettings::show (engine); };
    addAndMakeVisible (clickSetup);
    startTimerHz (30);
    timerCallback();
}

void TempoPanel::tapped()
{
    if (tempo.hasMap())
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000.0)
        taps.clear();   // a pause starts again
    taps.push_back (now);
    if (taps.size() > 5)
        taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double beatMs = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        tempo.bpm->set (std::round ((float) (60000.0 / beatMs) * 10.0f) / 10.0f);
    }
}

void TempoPanel::timerCallback()
{
    // With a tempo map the readouts show the tempo and signature where the tape is now.
    const double now = tempo.ticksAt ((double) engine.tape.getPosition(), engine.tape.getSampleRate());
    bpm.setText (BpmReadout::format ((float) tempo.bpmAt (now)));
    signature.setText (Tempo::signatureText (tempo.signatureAt (now)));
    auto p = tempo.positionAtTicks (now);
    position.setText (juce::String::formatted ("%03d|%d|%03d", p.bar, p.beat, p.tick));

    const bool map = tempo.hasMap();
    if (map != showingMap)
    {
        showingMap = map;
        tap.setEnabled (! map);
        bpm.setTooltip (map ? "Tempo from the tempo map, where the tape is now (Session > Clear tempo map to set one tempo)"
                            : "Tempo in beats a minute: drag up or down (Shift: fine), turn the mouse wheel, or double-click to type it");
        bpm.setMouseCursor (map ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
        signature.setTooltip (map ? "Time signature from the tempo map, where the tape is now" : "Time signature: click to choose");
        signature.setMouseCursor (map ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void TempoPanel::resized()
{
    auto r = getLocalBounds();
    bpm.setBounds (r.removeFromTop (110));
    bpmLegend = r.removeFromTop (18);
    r.removeFromTop (14);

    auto row = r.removeFromTop (64);
    tap.setBounds (row.removeFromRight (96).reduced (0, 6));
    row.removeFromRight (12);
    signature.setBounds (row);
    sigLegend = r.removeFromTop (18).withWidth (signature.getWidth());
    r.removeFromTop (18);

    positionLegend = r.removeFromTop (18);
    position.setBounds (r.removeFromTop (48));
    r.removeFromTop (22);

    auto bottom = r.removeFromTop (Knob::preferredHeight);
    clickLevel.setBounds (bottom.removeFromRight (Knob::preferredWidth + 20).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
    bottom.removeFromRight (12);
    clickSetup.setBounds (bottom.removeFromRight (80).withSizeKeepingCentre (80, 30));
    bottom.removeFromRight (8);
    click.setBounds (bottom.withSizeKeepingCentre (bottom.getWidth(), 30));
}

void TempoPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xffaeb3b5));
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    if (showingMap)
    {
        g.setColour (juce::Colour (0xffe8a33d));
        g.drawText ("BPM  (TEMPO MAP: follows the song)", bpmLegend, juce::Justification::centred, false);
        g.drawText ("Time signature (tempo map)", sigLegend, juce::Justification::centred, false);
        g.setColour (juce::Colour (0xffaeb3b5));
    }
    else
    {
        g.drawText ("BPM  (drag, wheel, double-click to type)", bpmLegend, juce::Justification::centred, false);
        g.drawText ("Time signature (click)", sigLegend, juce::Justification::centred, false);
    }
    g.drawText ("Song position   bar | beat | tick", positionLegend, juce::Justification::centredLeft, false);
}
