#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MidiControl.h"
#include "ShuttlePanel.h"

/** A list of things MIDI can press: name, the note or controller on it, Learn and clear. Optional header rows
    (a category). The owner supplies the rows; call refresh() when they change. */
class MidiBindingList : public juce::Component,
                        private juce::ListBoxModel
{
public:
    struct Rows
    {
        std::function<int()> count;
        std::function<juce::String (int)> name;
        std::function<bool (int)> isHeader;        // optional
        std::function<MidiBinding (int)> binding;
        std::function<bool (int)> isLearning;
        std::function<void (int)> learn;           // toggles Learn on this row
        std::function<void (int)> clear;
    };

    explicit MidiBindingList (Rows);
    ~MidiBindingList() override;
    void refresh();
    void resized() override { list.setBounds (getLocalBounds()); }

private:
    class Row;
    Rows rows;
    juce::ListBox list;

    int getNumRows() override { return rows.count ? rows.count() : 0; }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
};

/** The Jog / shuttle window (right-click the console's Jog / shuttle box): a copy of the box with 99 locate
    memories six at a time, and its MIDI control (input, channel, the jog wheel's encoder, and Learn for every
    control). Closing it only hides it. */
class ShuttleWindow : public juce::DocumentWindow,
                      private juce::Timer
{
public:
    ShuttleWindow (TapeMachine&, ShuttleMidi&, juce::PropertiesFile& settings, std::function<void()> saveSettings);
    ~ShuttleWindow() override;

    void show();
    void closeButtonPressed() override;

private:
    class Content;
    Content* content = nullptr;
    ShuttleMidi& midi;
    juce::PropertiesFile& settings;
    std::function<void()> saveSettings;
    std::uint32_t shownRevision = ~0u;
    void timerCallback() override;
};

/** The MIDI tab of Options > Keyboard shortcuts: every command, a note or controller for each (from any MIDI input). */
class MidiShortcutsEditor : public juce::Component,
                            private juce::Timer
{
public:
    MidiShortcutsEditor (juce::ApplicationCommandManager&, MidiShortcuts&);
    ~MidiShortcutsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Item { juce::String text; int command = -1; };   // command -1: a category heading
    MidiShortcuts& shortcuts;
    std::vector<Item> items;
    MidiBindingList list;
    std::uint32_t shownRevision = ~0u;
    void timerCallback() override;
};
