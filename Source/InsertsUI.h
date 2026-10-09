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

/** Pop-up editor for one insert chain: four slots, each with plugin picker, bypass,
    open-window button and up/down order buttons; plus the return pan on an aux return. */
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
        juce::TextButton picker, bypass { "Byp" }, edit { "Edit" }, up, down;
    };

    InsertChain& chain;
    PluginContext& context;
    Param* mainAssign;
    std::array<Row, InsertChain::numSlots> rows;
    std::unique_ptr<Knob> returnKnob;

    void refresh();
    void choosePlugin (int slot);
    void openEditor (int slot);
};

/** Row of cells in the centre section: an FX button for each sub, aux return and the master,
    and a return pan knob for each aux. A Link button between each aux pair makes it one stereo aux,
    whose two cells merge into one (one stereo FX chain, one return balance). */
class InsertsSection : public juce::Component,
                       private juce::Timer
{
public:
    InsertsSection (AudioEngine&, PluginContext*);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Cell
    {
        InsertChain* chain;
        Param* returnPan;
        Param* mainAssign;
        juce::String title;
        std::unique_ptr<juce::TextButton> fx;
        std::unique_ptr<Knob> knob;
    };

    AudioEngine& engine;
    PluginContext* context;
    std::vector<Cell> cells;
    std::array<juce::TextButton, AudioEngine::numAuxPairs> linkButtons;
    std::array<bool, AudioEngine::numAuxPairs> shownLink {};

    juce::Rectangle<int> cellBounds (size_t index) const;
    void toggleLink (int pair);

    void timerCallback() override;
    void openChain (Cell&);
};
