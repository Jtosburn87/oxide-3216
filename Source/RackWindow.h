#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "InsertsUI.h"

/** One insert slot drawn as a 1U piece of outboard gear screwed into the rack rails.
    Click the faceplate to open the plugin (or, on an empty slot, pick one); the list button on the
    left always opens the plugin list; BYP bypasses. */
class RackUnit : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    /** idle: true while the slot can't be used (the even aux of a linked pair). */
    RackUnit (InsertChain&, int slot, PluginContext&, Param* mainAssign, std::function<bool()> idle);

    void update();   // from the rack's timer: repaint when the slot changed

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    static constexpr int height = 40, earWidth = 22;

private:
    InsertChain& chain;
    const int slot;
    PluginContext& context;
    Param* mainAssign;
    std::function<bool()> idle;
    PluginListButton list;
    juce::TextButton bypass { "BYP" };

    struct Shown
    {
        juce::String name, maker;
        bool loaded = false, bypassed = false, refused = false, idle = false, running = false;
        bool operator== (const Shown& o) const
        {
            return name == o.name && maker == o.maker && loaded == o.loaded && bypassed == o.bypassed
                && refused == o.refused && idle == o.idle && running == o.running;
        }
    } shown;
    bool initialised = false;
    Shown read() const;

    void paintEar (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;
};

/** Every insert chain at once, as racks of outboard gear: Subs, Aux 1-4, Aux 5-8 and Master.
    The racks wrap onto more rows when the window is narrow. */
class RackView : public juce::Component,
                 private juce::Timer
{
public:
    RackView (AudioEngine&, PluginContext&);

    /** Lays the racks out for a given width and sets this component's size. */
    void layoutFor (int width);
    int widthForOneRow() const;

    void paint (juce::Graphics&) override;

private:
    struct ChainBlock
    {
        InsertChain* chain;
        std::function<juce::String()> label;
        std::function<bool()> idle;
        juce::Rectangle<int> header;
    };
    struct Rack
    {
        juce::String title;
        std::vector<ChainBlock> chains;
        juce::Rectangle<int> bounds;
    };

    AudioEngine& engine;
    std::vector<Rack> racks;
    juce::OwnedArray<RackUnit> units;
    juce::StringArray shownLabels;

public:
    static constexpr int rackWidth = 420, rail = 18, titleH = 34, headerH = 26, gap = 28, margin = 20;

private:
    int rackHeight (const Rack&) const;
    juce::StringArray labels() const;

    void timerCallback() override;
    void paintRack (juce::Graphics&, const Rack&) const;
};

/** The Rack window (Rack button on the console). Closing it only hides it; it keeps its size and place. */
class RackWindow : public juce::DocumentWindow
{
public:
    RackWindow (AudioEngine&, PluginContext&, juce::PropertiesFile& settings);
    ~RackWindow() override;

    void show();
    void closeButtonPressed() override;

private:
    class Content;
    PluginContext& context;
    juce::PropertiesFile& settings;
    void saveState();
};
