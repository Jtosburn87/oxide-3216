#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelStrip.h"
#include "Widgets.h"

/** A channel compressor's controls, for the type chosen (ChannelCompressor::type): the console strip's
    compressor section and the channel window's Compressor block both use one. Only the current type's
    controls show; the gain-reduction meter is at the bottom. Laid out for a 100..110 wide box, 154 tall
    (knob rows at 10 and 62, a button row at 116, the meter at 137). */
class CompressorPanel : public juce::Component
{
public:
    explicit CompressorPanel (ChannelCompressor&);

    static constexpr int preferredHeight = 154;

    /** Call from a timer: follows the type switch. */
    void update();
    void setReduction (float db) { grMeter.setReduction (db); }

    /** Every knob calls this on a double-click (the console strip opens the channel window). */
    void setKnobDoubleClick (std::function<void()>);

    /** The short name of the type showing, e.g. "76". */
    juce::String typeLabel() const { return ChannelCompressor::typeLabels[shownType]; }
    std::function<void()> onTypeChanged;

    void resized() override;

private:
    ChannelCompressor& comp;
    int shownType = -1;

    // 60
    Knob vcaThreshold, vcaRatio, vcaOutput;
    ParamButton vcaOverEasy;
    // 76
    Knob fetInput, fetOutput, fetAttack, fetRelease;
    juce::OwnedArray<RadioParamButton> fetRatios;
    ParamButton fetAll;
    // LA
    Knob optoPeak, optoGain;
    RadioParamButton optoComp, optoLimit;
    // G
    Knob sslThreshold, sslRatio, sslRelease;
    ParamButton sslFast;

    GrMeter grMeter;

    std::vector<juce::Component*> controlsFor (int type);
    std::vector<Knob*> knobs();
};
