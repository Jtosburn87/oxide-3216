#include "InsertsUI.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

//==============================================================================
InsertChainEditor::InsertChainEditor (InsertChain& c, PluginContext& ctx, Param* returnPan, Param* main)
    : chain (c), context (ctx), mainAssign (main)
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

    if (returnPan != nullptr)
    {
        returnKnob = std::make_unique<Knob> (*returnPan, "Return pan", juce::Colour (0xffd9d6cc));
        returnKnob->setEndLabels ("L", "R");
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
        const bool refused = chain.isStereoRefused (i);
        r.picker.setButtonText (! loaded ? juce::String ("(empty)") : chain.getDescription (i).name + (refused ? " (not stereo: off while linked)" : ""));
        r.picker.setTooltip (refused ? "This plugin has no stereo in/out, so it is left out while the aux pair is linked" : "Choose a plugin for slot " + juce::String (i + 1));
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
            const bool wasEmpty = self.chain.numLoaded() == 0;
            self.chain.setSlot (slot, std::move (instance), desc);
            if (self.chain.isStereoRefused (slot))
            {
                self.chain.clearSlot (slot);
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Stereo plugins only",
                                                        desc.name + " has no stereo in/out. A linked aux pair takes stereo plugins only.");
                return;
            }
            if (wasEmpty && self.mainAssign != nullptr)
                self.mainAssign->set (1.0f);   // a return with a plugin on it is usually wanted in the mix
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
    g.drawText (chain.name + " inserts" + (chain.isStereoOnly() ? juce::String (", stereo") : juce::String()), getLocalBounds().reduced (10, 6).removeFromTop (24), juce::Justification::centredLeft, false);
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
        cells.push_back ({ engine.subInserts[k], nullptr, nullptr, "Sub " + juce::String (k + 1), nullptr, nullptr });
    for (int k = 0; k < Config::numAux; ++k)
        cells.push_back ({ engine.auxInserts[k], engine.auxReturnPan[(size_t) k], engine.auxMain[(size_t) k], "A" + juce::String (k + 1), nullptr, nullptr });
    cells.push_back ({ &engine.masterInserts, nullptr, nullptr, "Master", nullptr, nullptr });

    for (auto& c : cells)
    {
        c.fx = std::make_unique<juce::TextButton> ("FX");
        c.fx->setTooltip (c.chain->name + " inserts");
        c.fx->setTitle (c.chain->name + " inserts");
        c.fx->setEnabled (context != nullptr);
        c.fx->onClick = [this, &c] { openChain (c); };
        addAndMakeVisible (*c.fx);

        if (c.returnPan != nullptr)
        {
            c.knob = std::make_unique<Knob> (*c.returnPan, "Pan", juce::Colour (0xffd9d6cc));
            c.knob->setEndLabels ("L", "R");
            addAndMakeVisible (*c.knob);
        }
    }

    for (int j = 0; j < AudioEngine::numAuxPairs; ++j)
    {
        auto& b = linkButtons[(size_t) j];
        const auto pair = "A" + juce::String (2 * j + 1) + " + A" + juce::String (2 * j + 2);
        b.setButtonText ("Link");
        b.setTitle ("Link " + pair);
        b.setTooltip ("Link " + pair + " into one stereo aux: one stereo FX chain, masters move together, "
                      "and on every channel the A" + juce::String (2 * j + 2) + " knob becomes the send's pan");
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a8fd6));
        b.onClick = [this, j] { toggleLink (j); };
        addAndMakeVisible (b);
    }
    startTimerHz (4);
}

void InsertsSection::toggleLink (int pair)
{
    const auto notStereo = engine.setAuxLink (pair, ! engine.auxLink[(size_t) pair]->on());
    timerCallback();
    if (context != nullptr && context->changed)
        context->changed();
    if (! notStereo.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Stereo plugins only",
                                                "A linked aux pair takes stereo plugins only. These have no stereo in/out and are "
                                                "left out until the pair is unlinked:\n\n" + notStereo.joinIntoString ("\n"));
}

void InsertsSection::openChain (Cell& c)
{
    if (context == nullptr)
        return;
    auto editor = std::make_unique<InsertChainEditor> (*c.chain, *context, c.returnPan, c.mainAssign);
    juce::CallOutBox::launchAsynchronously (std::move (editor), c.fx->getScreenBounds(), nullptr);
}

void InsertsSection::timerCallback()
{
    bool relayout = false;
    for (int j = 0; j < AudioEngine::numAuxPairs; ++j)
    {
        const bool on = engine.auxLink[(size_t) j]->on();
        linkButtons[(size_t) j].setToggleState (on, juce::dontSendNotification);
        relayout = relayout || on != shownLink[(size_t) j];
        shownLink[(size_t) j] = on;
    }
    if (relayout)
    {
        resized();
        repaint();
    }

    for (auto& c : cells)
    {
        const int n = c.chain->numLoaded();
        const auto text = n == 0 ? juce::String ("FX") : "FX " + juce::String (n);
        if (c.fx->getButtonText() != text)
            c.fx->setButtonText (text);
        c.fx->setToggleState (c.chain->isActive(), juce::dontSendNotification);
    }
}

juce::Rectangle<int> InsertsSection::cellBounds (size_t i) const
{
    // A linked aux pair: the odd cell spans both, the even one has none.
    const int cellW = getWidth() / (int) cells.size();
    auto cell = juce::Rectangle<int> ((int) i * cellW, 0, cellW, getHeight());
    const int k = (int) i - Config::numSubs;
    if (juce::isPositiveAndBelow (k, Config::numAux) && shownLink[(size_t) (k / 2)])
        return k % 2 == 0 ? cell.withWidth (2 * cellW) : juce::Rectangle<int>();
    return cell;
}

void InsertsSection::resized()
{
    const int cellW = getWidth() / (int) cells.size();
    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto& c = cells[i];
        auto cell = cellBounds (i).reduced (3, 0);
        c.fx->setVisible (! cell.isEmpty());
        if (c.knob != nullptr)
            c.knob->setVisible (! cell.isEmpty());
        if (cell.isEmpty())
            continue;

        cell.removeFromTop (16);   // title
        if (c.returnPan != nullptr)
            cell.removeFromTop (18);   // link buttons
        c.fx->setBounds (cell.removeFromBottom (22).reduced (2, 0));
        if (c.knob != nullptr)
            c.knob->setBounds (cell.withSizeKeepingCentre (Knob::preferredWidth, Knob::preferredHeight));
    }

    // Each Link button straddles the line between its pair.
    for (int j = 0; j < AudioEngine::numAuxPairs; ++j)
    {
        const int x = (Config::numSubs + 2 * j + 1) * cellW;
        linkButtons[(size_t) j].setBounds (x - 22, 17, 44, 16);
    }
}

void InsertsSection::paint (juce::Graphics& g)
{
    const int cellW = getWidth() / (int) cells.size();
    g.setFont (ConsoleLookAndFeel::uiFont (11.0f, true));
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto cell = cellBounds (i);
        const int k = (int) i;
        if (! cell.isEmpty())
        {
            const int a = k - Config::numSubs;
            const bool linkedPair = juce::isPositiveAndBelow (a, Config::numAux) && shownLink[(size_t) (a / 2)];
            const auto title = k < Config::numSubs ? engine.subNames[(size_t) k]
                             : linkedPair ? engine.auxNames[(size_t) a] + " + " + engine.auxNames[(size_t) a + 1]
                             : k < Config::numSubs + Config::numAux ? engine.auxNames[(size_t) a]
                             : cells[i].title;
            g.setColour (legend);
            g.drawFittedText (title, cell.withHeight (14).reduced (2, 0), juce::Justification::centred, 1, 0.7f);
        }
        if (i == (size_t) Config::numSubs || i == cells.size() - 1)
        {
            g.setColour (juce::Colour (0xff8e9598).withAlpha (0.6f));
            g.fillRect ((int) i * cellW, 2, 1, getHeight() - 4);
        }
    }
}
