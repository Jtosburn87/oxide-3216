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
    open-window button and up/down order buttons; plus the return level on an aux return. */
class InsertChainEditor : public juce::Component
{
public:
    InsertChainEditor (InsertChain&, PluginContext&, Param* returnLevel);
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
    std::array<Row, InsertChain::numSlots> rows;
    std::unique_ptr<Knob> returnKnob;

    void refresh();
    void choosePlugin (int slot);
    void openEditor (int slot);
};

/** Row of cells in the centre section: an FX button for each sub, aux return and the master,
    and a return level knob for each aux. */
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
        Param* returnLevel;
        juce::String title;
        std::unique_ptr<juce::TextButton> fx;
        std::unique_ptr<Knob> knob;
    };

    AudioEngine& engine;
    PluginContext* context;
    std::vector<Cell> cells;

    void timerCallback() override;
    void openChain (Cell&);
};
