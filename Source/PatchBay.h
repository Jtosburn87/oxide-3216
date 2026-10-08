#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"

/** The jack field: outputs on the top row, inputs on the bottom, cables drawn between them. */
class PatchBayField : public juce::Component
{
public:
    PatchBayField (AudioEngine&, std::function<void()> onChanged);
    ~PatchBayField() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh();

    static juce::Colour sourceColour (int source);

private:
    class Jack;
    using Patch = AudioEngine::Patch;

    AudioEngine& engine;
    std::function<void()> onChanged;
    juce::OwnedArray<Jack> sources, destinations;
    int selectedSource = -1;

    struct Group { juce::String title; juce::Rectangle<int> area; };
    std::vector<Group> groups;

    void jackClicked (bool isSource, int index);
    juce::Point<float> holeCentre (const Jack&) const;
    void layoutRow (juce::OwnedArray<Jack>&, const std::vector<std::pair<juce::String, int>>& groupSizes, int y);
};

/** Pop-up panel over the console holding the patch bay. */
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
    juce::Viewport viewport;
    juce::TextButton closeButton { "Close" }, clearButton { "Pull all cables" };
    juce::Label help;
    juce::Rectangle<int> frame;
};
