#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"

/** Session > Export tracks: Bounce Song (the main mix recorded in real time while the song plays, so it can be mixed
    as it goes) and Export From Tape (chosen tape tracks straight off the tape, one mono WAV each). Both save into the
    session's Bounced Audio folder. */
namespace ExportDialogs
{
    void bounceSong (AudioEngine&, const juce::File& bounceFolder, juce::PropertiesFile& settings);
    void exportFromTape (AudioEngine&, const juce::File& bounceFolder, juce::PropertiesFile& settings);
}
