#include "InsertsUI.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "ConsoleLookAndFeel.h"
#include "Dsp.h"

using namespace ConsoleColours;

namespace
{
    juce::String faderDbText (float v)
    {
        return v <= 0.0f ? juce::String ("off") : juce::String (Dsp::faderDb (v), 1);
    }
}

//==============================================================================
InsertChainEditor::InsertChainEditor (InsertChain& c, PluginContext& ctx, Param* returnLevel)
    : chain (c), context (ctx)
{
    for (int i = 0; i < InsertChain::numSlots; ++i)
    {
        auto& r = rows[(size_t) i];
        r.number.setText (juce::String (i + 1), juce::dontSendNotification);
        r.number.setFont (ConsoleLookAndFeel::uiFont (14.0f, true));
        r.number.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (r.number);

        r.picker.setTooltip ("Choose a plugin for slot " + juce::String (i + 1));
        r.picker.onClick = [this, i] { choosePlugin (i); };
        r.bypass.setTooltip ("Bypass this plugin");
        r.bypass.onClick = [this, i] { chain.setBypassed (i, ! chain.isBypassed (i)); };
        r.edit.setTooltip ("Open the plugin's window");
        r.edit.onClick = [this, i] { openEditor (i); };
        r.up.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb2"));
        r.down.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xbc"));
        r.up.setTitle ("Move slot " + juce::String (i + 1) + " up");
        r.down.setTitle ("Move slot " + juce::String (i + 1) + " down");
        r.up.onClick = [this, i] { chain.moveSlot (i, i - 1); if (context.changed) context.changed(); };
        r.down.onClick = [this, i] { chain.moveSlot (i, i + 1); if (context.changed) context.changed(); };
        r.bypass.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
        r.bypass.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        for (auto* b : { &r.picker, &r.bypass, &r.edit, &r.up, &r.down })
            addAndMakeVisible (b);
    }

    if (returnLevel != nullptr)
    {
        returnKnob = std::make_unique<Knob> (*returnLevel, "Return", juce::Colour (0xffd9d6cc));
        returnKnob->setFormatter ([] (float v) { return faderDbText (v); });
        addAndMakeVisible (*returnKnob);
    }

    chain.onChange = [this] { refresh(); };
    refresh();
    setSize (returnKnob != nullptr ? 470 : 410, 44 + InsertChain::numSlots * 30 + 8);
}

InsertChainEditor::~InsertChainEditor()
{
    chain.onChange = nullptr;
}

void InsertChainEditor::refresh()
{
    for (int i = 0; i < InsertChain::numSlots; ++i)
    {
        auto& r = rows[(size_t) i];
        const bool loaded = chain.getProcessor (i) != nullptr;
        r.picker.setButtonText (loaded ? chain.getDescription (i).name : juce::String ("(empty)"));
        r.picker.setToggleState (false, juce::dontSendNotification);
        r.bypass.setEnabled (loaded);
        r.bypass.setToggleState (chain.isBypassed (i), juce::dontSendNotification);
        r.edit.setEnabled (loaded);
        r.up.setEnabled (i > 0 && (loaded || chain.getProcessor (i - 1) != nullptr));
        r.down.setEnabled (i < InsertChain::numSlots - 1 && (loaded || chain.getProcessor (i + 1) != nullptr));
    }
    repaint();
}

void InsertChainEditor::choosePlugin (int slot)
{
    const auto effects = context.manager.effects();
    juce::PopupMenu menu;
    if (chain.getProcessor (slot) != nullptr)
    {
        menu.addItem (-1, "Remove " + chain.getDescription (slot).name);
        menu.addSeparator();
    }
    if (effects.isEmpty())
        menu.addItem (-2, "No plugins yet: click Plugins in the top bar and scan", false, false);
    else
        juce::KnownPluginList::addToMenu (menu, effects, juce::KnownPluginList::sortByManufacturer,
                                          chain.getDescription (slot).createIdentifierString());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rows[(size_t) slot].picker),
                        [safe = juce::Component::SafePointer<InsertChainEditor> (this), slot, effects] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;

        if (result == -1)
        {
            self.chain.clearSlot (slot);
        }
        else
        {
            const int index = juce::KnownPluginList::getIndexChosenByMenu (effects, result);
            if (! juce::isPositiveAndBelow (index, effects.size()))
                return;

            const auto desc = effects[index];
            juce::String error;
            auto instance = self.context.manager.create (desc, self.context.sampleRate(), Config::maxBlock, error);
            if (instance == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load plugin",
                                                        desc.name + " could not be loaded.\n\n" + error);
                return;
            }
            self.chain.setSlot (slot, std::move (instance), desc);
            self.openEditor (slot);   // a newly chosen plugin opens straight away
        }
        if (self.context.changed)
            self.context.changed();
    });
}

void InsertChainEditor::openEditor (int slot)
{
    if (auto* p = chain.getProcessor (slot))
        context.windows.open (*p, chain.name + "  |  slot " + juce::String (slot + 1) + "  |  " + chain.getDescription (slot).name);
}

void InsertChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1e));
    g.setColour (legend);
    g.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
    g.drawText (chain.name + " inserts", getLocalBounds().reduced (10, 6).removeFromTop (24), juce::Justification::centredLeft, false);
    g.setColour (legend.withAlpha (0.6f));
    g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
    g.drawText ("Signal flows from slot 1 to slot 4", getLocalBounds().reduced (10, 6).removeFromTop (24),
                juce::Justification::centredRight, false);
}

void InsertChainEditor::resized()
{
    auto area = getLocalBounds().reduced (10, 6);
    area.removeFromTop (30);
    if (returnKnob != nullptr)
        returnKnob->setBounds (area.removeFromRight (56).withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));

    for (auto& r : rows)
    {
        auto row = area.removeFromTop (30).reduced (0, 3);
        r.number.setBounds (row.removeFromLeft (20));
        r.down.setBounds (row.removeFromRight (26));
        row.removeFromRight (2);
        r.up.setBounds (row.removeFromRight (26));
        row.removeFromRight (6);
        r.edit.setBounds (row.removeFromRight (44));
        row.removeFromRight (4);
        r.bypass.setBounds (row.removeFromRight (40));
        row.removeFromRight (6);
        r.picker.setBounds (row);
    }
}

//==============================================================================
InsertsSection::InsertsSection (AudioEngine& e, PluginContext* ctx) : engine (e), context (ctx)
{
    for (int k = 0; k < Config::numSubs; ++k)
        cells.push_back ({ engine.subInserts[k], nullptr, "Sub " + juce::String (k + 1), nullptr, nullptr });
    for (int k = 0; k < Config::numAux; ++k)
        cells.push_back ({ engine.auxInserts[k], engine.auxReturn[(size_t) k], "A" + juce::String (k + 1), nullptr, nullptr });
    cells.push_back ({ &engine.masterInserts, nullptr, "Master", nullptr, nullptr });

    for (auto& c : cells)
    {
        c.fx = std::make_unique<juce::TextButton> ("FX");
        c.fx->setTooltip (c.chain->name + " inserts");
        c.fx->setTitle (c.chain->name + " inserts");
        c.fx->setEnabled (context != nullptr);
        c.fx->onClick = [this, &c] { openChain (c); };
        addAndMakeVisible (*c.fx);

        if (c.returnLevel != nullptr)
        {
            c.knob = std::make_unique<Knob> (*c.returnLevel, "Return", juce::Colour (0xffd9d6cc));
            c.knob->setFormatter ([] (float v) { return faderDbText (v); });
            addAndMakeVisible (*c.knob);
        }
    }
    startTimerHz (4);
}

void InsertsSection::openChain (Cell& c)
{
    if (context == nullptr)
        return;
    auto editor = std::make_unique<InsertChainEditor> (*c.chain, *context, c.returnLevel);
    juce::CallOutBox::launchAsynchronously (std::move (editor), c.fx->getScreenBounds(), nullptr);
}

void InsertsSection::timerCallback()
{
    for (auto& c : cells)
    {
        const int n = c.chain->numLoaded();
        const auto text = n == 0 ? juce::String ("FX") : "FX " + juce::String (n);
        if (c.fx->getButtonText() != text)
            c.fx->setButtonText (text);
        c.fx->setToggleState (c.chain->isActive(), juce::dontSendNotification);
    }
}

void InsertsSection::resized()
{
    const int cellW = getWidth() / (int) cells.size();
    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = juce::Rectangle<int> ((int) i * cellW, 0, cellW, getHeight()).reduced (3, 0);
        cell.removeFromTop (16);   // title
        cells[i].fx->setBounds (cell.removeFromBottom (22).reduced (2, 0));
        if (cells[i].knob != nullptr)
            cells[i].knob->setBounds (cell.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
    }
}

void InsertsSection::paint (juce::Graphics& g)
{
    const int cellW = getWidth() / (int) cells.size();
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto cell = juce::Rectangle<int> ((int) i * cellW, 0, cellW, getHeight());
        g.setColour (legend);
        g.drawText (cells[i].title, cell.withHeight (14), juce::Justification::centred, false);
        if (i == (size_t) Config::numSubs || i == cells.size() - 1)
        {
            g.setColour (juce::Colour (0xff8e9598).withAlpha (0.6f));
            g.fillRect (cell.getX(), 2, 1, getHeight() - 4);
        }
    }
}
