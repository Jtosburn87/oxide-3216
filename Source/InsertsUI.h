#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Widgets.h"

class PluginManager;
class PluginWindows;

/** What the insert UI needs to load plugins and open their windows. */
struct PluginContext
{
    PluginManager& manager;
    PluginWindows& windows;
    std::function<double()> sampleRate;
    std::function<void()> changed;   // save settings
};

/** What a slot's controls do, shared by the console's insert pop-up and the rack window. */
namespace InsertSlot
{
    /** The plugin menu for a slot (pick a plugin, or remove the one there), shown at `target`.
        mainAssign: an aux's main assign, switched on when the first plugin goes onto its empty chain.
        A newly chosen plugin opens its window straight away. */
    void choose (InsertChain&, int slot, PluginContext&, Param* mainAssign, juce::Component& target);

    /** Opens (or brings to the front) the window of the slot's plugin. */
    void openWindow (InsertChain&, int slot, PluginContext&);

    /** Clicking a slot's name: an empty slot shows the plugin menu, a loaded one opens its window. */
    void click (InsertChain&, int slot, PluginContext&, Param* mainAssign, juce::Component& target);

}

/** The button left of a slot's name that opens the plugin list (drawn as three lines). */
class PluginListButton : public juce::TextButton
{
public:
    explicit PluginListButton (int slot = 0) { setSlot (slot); }
    void setSlot (int slot);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
};

/** Pop-up editor for one insert chain: four slots, each with a plugin list button, the plugin's name
    (click to open its window), bypass and up/down order buttons; plus the return pan on an aux return. */
class InsertChainEditor : public juce::Component
{
public:
    /** mainAssign: an aux's main assign, switched on when the first plugin goes onto its empty chain. */
    InsertChainEditor (InsertChain&, PluginContext&, Param* returnPan, Param* mainAssign = nullptr);
    ~InsertChainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label number;
        PluginListButton list;
        juce::TextButton name, bypass { "Byp" }, up, down;
    };

    InsertChain& chain;
    PluginContext& context;
    Param* mainAssign;
    std::array<Row, InsertChain::numSlots> rows;
    std::unique_ptr<Knob> returnKnob;

    void refresh();
};

/** Row of cells in the centre section, one over each sub, aux and the main fader: the bus's four insert
    slots as small rack boxes (plugin list button left of each), and a return pan knob under them on each aux.
    A Link button between each aux pair makes it one stereo aux, whose two cells merge into one (one stereo
    FX chain, one return balance). */
class InsertsSection : public juce::Component,
                       private juce::Timer
{
public:
    InsertsSection (AudioEngine&, PluginContext*);

    /** Width of one bus fader column, so each cell sits over its fader (the master cell takes the rest).
        0: cells share the width evenly. */
    void setColumnWidth (float w) { columnW = w; resized(); repaint(); }

    /** The SBC button (above the master slots) was clicked: open the stereo bus compressor's window. */
    std::function<void()> onOpenBusComp;

    /** The Sends on Fader button (under the master slots) was clicked: the desk offers the auxes. */
    std::function<void()> onSendsOnFader;
    /** What the Sends on Fader button shows: its text, lit while the faders are on a send. */
    void showSendsOnFader (const juce::String& text, bool on);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** One insert slot: the plugin's name, lit while it runs. Click opens the plugin (empty: the plugin menu),
        right-click gives Bypass / Replace / Remove. */
    class SlotBox : public juce::Component,
                    public juce::SettableTooltipClient
    {
    public:
        SlotBox (InsertsSection&, size_t cell, int slot);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void refresh();

    private:
        InsertsSection& owner;
        const size_t cell;
        const int slot;
        juce::String shownName;
        int shownState = -1;   // 0 empty, 1 running, 2 bypassed, 3 refused
    };

    struct Cell
    {
        InsertChain* chain;
        Param* returnPan;
        Param* mainAssign;
        juce::String title;
        std::array<std::unique_ptr<SlotBox>, InsertChain::numSlots> slots;
        std::array<std::unique_ptr<PluginListButton>, InsertChain::numSlots> lists;
        std::unique_ptr<Knob> knob;
    };

    AudioEngine& engine;
    PluginContext* context;
    std::vector<Cell> cells;
    std::array<juce::TextButton, AudioEngine::numAuxPairs> linkButtons;
    std::array<bool, AudioEngine::numAuxPairs> shownLink {};
    float columnW = 0.0f;
    juce::TextButton sbcButton { "SBC" };   // lit while the bus compressor is in
    juce::TextButton sofButton { "Sends on Fader" };   // the channel faders move an aux send instead (lit)
    float shownGr = 0.0f;                    // dB, on the bar under the master slots
    juce::Rectangle<int> grBar;

    int columnX (int index) const;
    juce::Rectangle<int> cellBounds (size_t index) const;
    void toggleLink (int pair);
    void slotChanged();

    void timerCallback() override;
};
