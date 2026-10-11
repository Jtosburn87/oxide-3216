#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Scenes.h"
#include "Widgets.h"

/** The console's scene number: two red 7-segment digits, as on an old digital desk. It shows the selected scene,
    flashing while that is not the scene the console is on (press Go to recall it); "--" until a scene is stored.
    The amber dot lights when a fader or knob has moved since the current scene was recalled or stored.
    Right-click calls onPopupMenu (the Scene Memory window). */
class SceneReadout : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    explicit SceneReadout (SceneMemory&);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onPopupMenu;

private:
    SceneMemory& scenes;
    bool flashOn = true, shownEdited = false;
    int shownSelected = -2, shownCurrent = -2;
    int ticks = 0;

    void timerCallback() override;
    static void paintDigit (juce::Graphics&, juce::Rectangle<float>, juce::juce_wchar);
};

/** The Scene Memory window, after a Yamaha digital console's: scenes 01-99 in a scrolling list (the green dot is
    the scene the console is on), Go to recall the selected scene, Store to keep the console's faders and knobs
    in it (asking first if it holds one), Prev / Next to select the previous or next stored scene, and its title. */
class SceneMemoryPanel : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::Timer
{
public:
    /** Opens the window (or brings it to the front), centred on the screen. */
    static void show (SceneMemory&);

    explicit SceneMemoryPanel (SceneMemory&);
    ~SceneMemoryPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SceneMemory& scenes;
    juce::ListBox list;
    juce::TextButton go { "Go" }, store { "Store" }, prev { juce::CharPointer_UTF8 ("\xe2\x97\x80 Prev") }, next { juce::CharPointer_UTF8 ("Next \xe2\x96\xb6") };
    Knob fadeKnob;                 // recall fade: Instant .. 5 s
    juce::TextEditor title;
    juce::Label status;
    std::uint32_t shownRevision = 0;
    juce::Rectangle<int> titleLabelArea;

    int getNumRows() override { return SceneMemory::numScenes; }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void storeSelected();
    void step (int direction);
    void refresh();
    void timerCallback() override;

    static juce::Component::SafePointer<juce::DialogWindow> window;
};
