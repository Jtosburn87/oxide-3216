#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"

/** The jack field, laid out like a rack: two pairs of rows, outputs above inputs.
    Pair 1: interface inputs and channel direct outs over the channel strip ins and tape ins.
    Pair 2: tape outs, return direct outs, aux, sub and main over the return strip ins.
    The test oscillator sits at the end of pair 1: an output like any other, with its controls beside it.
    Every normal runs straight down from an output to the input under it. */
class PatchBayField : public juce::Component,
                      private Param::Listener
{
public:
    PatchBayField (AudioEngine&, std::function<void()> onChanged);
    ~PatchBayField() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Puts down a picked-up output. Returns false if nothing was picked up. */
    bool cancelSelection();

    /** Re-reads the patch and the interface's inputs (the number of Interface in jacks follows the device). */
    void refresh();

    /** Every console input channel from the interface input with the same number. */
    void mapInterfaceOneToOne();

    static juce::Colour sourceColour (int source);
    static constexpr int maxInterfaceJacks = 32;

private:
    class Jack;
    using Patch = AudioEngine::Patch;
    enum class Kind { source, destination, interfaceIn };
    struct Selection { Kind kind; int index; };

    AudioEngine& engine;
    std::function<void()> onChanged;
    juce::OwnedArray<Jack> sources, destinations, interfaceIns;
    std::optional<Selection> selected;
    int numInterfaceJacks = -1;
    juce::StringArray interfaceNames;

    struct Group { juce::String title; juce::Rectangle<int> area; };
    std::vector<Group> groups;

    // Test oscillator controls.
    juce::OwnedArray<RadioParamButton> oscTypeButtons;
    Knob oscFreqKnob, oscLevelKnob;
    void paramChanged (Param&) override;   // the frequency knob only applies to the sine

    void rebuild (int interfaceJacks);
    void placeGroup (const juce::String& title, juce::OwnedArray<Jack>&, int first, int count, int x, int y);
    void jackClicked (Kind, int index);
    void pullCables (Kind, int index);
    bool isPlugged (Kind, int index) const;
    void changed();
    juce::Point<float> holeCentre (const Jack&) const;
};

/** Pop-up panel over the console holding the patch bay. The field scales down to fit the panel. */
class PatchBayPanel : public juce::Component
{
public:
    PatchBayPanel (AudioEngine&, std::function<void()> onChanged);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the frame to close
    bool keyPressed (const juce::KeyPress&) override;    // Escape closes
    void visibilityChanged() override;

    std::function<void()> onClose;

private:
    PatchBayField field;
    juce::TextButton closeButton { "Close" }, clearButton { "Pull all cables" }, mapButton { "Interface 1 to 1" };
    juce::Label help;
    juce::Rectangle<int> frame;
};
