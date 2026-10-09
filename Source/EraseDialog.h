#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TapeMachine.h"

/** The Erase window (Erase key on the tape machine): pick one or more tracks, then erase them whole or
    only a section between an In and an Out time. Asks once more before anything is erased. */
class EraseDialog : public juce::Component
{
public:
    /** Opens the window (or brings it to the front), centred on the screen at full size: centring it on the
        desk would give it the desk's zoom. */
    static void show (TapeMachine&);

    explicit EraseDialog (TapeMachine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TapeMachine& tape;
    std::array<juce::TextButton, TapeMachine::numTracks> trackButtons;
    juce::TextButton allButton { "All" }, noneButton { "None" }, armedButton { "Armed" };
    juce::ToggleButton wholeButton { "Whole track" }, sectionButton { "Section" };
    juce::Label inLabel { {}, "In" }, outLabel { {}, "Out" }, hint;
    juce::TextEditor inField, outField;
    juce::TextButton locatorsButton { "Use the In / Out locators" };
    juce::TextButton eraseButton { "Erase..." }, cancelButton { "Cancel" };
    juce::int64 inPos = 0, outPos = 0;   // tape samples; the fields show them in mm:ss.ss

    static juce::Component::SafePointer<juce::DialogWindow> window;

    std::vector<int> selectedTracks() const;
    void setRange (juce::int64 in, juce::int64 out);
    bool rangeValid() const { return outPos > inPos; }
    void refresh();
    void confirm();
    void close();

    juce::String formatTime (juce::int64 samples) const;
    bool parseTime (const juce::String&, juce::int64& samples) const;
};
