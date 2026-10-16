#include "InsertsUI.h"
#include "PluginManager.h"
#include "PluginWindows.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

//==============================================================================
void InsertSlot::choose (InsertChain& chain, int slot, PluginContext& context, Param* mainAssign, juce::Component& target)
{
    const auto effects = context.manager.effects();
    juce::PopupMenu menu;
    if (chain.getProcessor (slot) != nullptr)
    {
        menu.addItem (-1, "Remove " + chain.getDescription (slot).name);
        menu.addSeparator();
    }
    if (effects.isEmpty())
        menu.addItem (-2, "No plugins yet: scan from Options > Plugins", false, false);
    else
        juce::KnownPluginList::addToMenu (menu, effects, juce::KnownPluginList::sortByManufacturer,
                                          chain.getDescription (slot).createIdentifierString());

    // The chain and the context live as long as the app; the menu's target may not.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [&chain, &context, mainAssign, slot, effects] (int result)
    {
        if (result == 0)
            return;

        if (result == -1)
        {
            chain.clearSlot (slot);
        }
        else
        {
            const int index = juce::KnownPluginList::getIndexChosenByMenu (effects, result);
            if (! juce::isPositiveAndBelow (index, effects.size()))
                return;

            const auto desc = effects[index];
            juce::String error;
            auto instance = context.manager.create (desc, context.sampleRate(), Config::maxBlock, error);
            if (instance == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load plugin",
                                                        desc.name + " could not be loaded.\n\n" + error);
                return;
            }
            const bool wasEmpty = chain.numLoaded() == 0;
            chain.setSlot (slot, std::move (instance), desc);
            if (chain.isStereoRefused (slot))
            {
                chain.clearSlot (slot);
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Stereo plugins only",
                                                        desc.name + " has no stereo in/out. A linked aux pair takes stereo plugins only.");
                return;
            }
            if (wasEmpty && mainAssign != nullptr)
                mainAssign->set (1.0f);   // a return with a plugin on it is usually wanted in the mix
            openWindow (chain, slot, context);   // a newly chosen plugin opens straight away
        }
        if (context.changed)
            context.changed();
    });
}

void InsertSlot::openWindow (InsertChain& chain, int slot, PluginContext& context)
{
    if (auto* p = chain.getProcessor (slot))
        context.windows.open (*p, chain.name + "  |  slot " + juce::String (slot + 1) + "  |  " + chain.getDescription (slot).name);
}

void InsertSlot::click (InsertChain& chain, int slot, PluginContext& context, Param* mainAssign, juce::Component& target)
{
    if (chain.getProcessor (slot) != nullptr)
        openWindow (chain, slot, context);
    else
        choose (chain, slot, context, mainAssign, target);
}

void PluginListButton::setSlot (int slot)
{
    setTitle ("Plugin list for slot " + juce::String (slot + 1));
    setTooltip ("Choose a plugin for slot " + juce::String (slot + 1) + ", or remove the one there");
}

void PluginListButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOver, isButtonDown);
    // Three lines, smaller on the console's tiny slot buttons.
    const float w = juce::jmin (12.0f, (float) getWidth() - 5.0f), step = getHeight() < 20 ? 3.0f : 4.0f;
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (w, step * 2.0f + 2.0f);
    g.setColour (findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (r.getX(), r.getY() + (float) i * step, r.getWidth(), step < 4.0f ? 1.5f : 2.0f, step < 4.0f ? 0.75f : 1.0f);
}

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

        r.list.setSlot (i);
        r.list.onClick = [this, i] { InsertSlot::choose (chain, i, context, mainAssign, rows[(size_t) i].list); };
        r.name.onClick = [this, i] { InsertSlot::click (chain, i, context, mainAssign, rows[(size_t) i].name); };
        r.bypass.setTooltip ("Bypass this plugin");
        r.bypass.onClick = [this, i] { chain.setBypassed (i, ! chain.isBypassed (i)); };
        r.up.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb2"));
        r.down.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xbc"));
        r.up.setTitle ("Move slot " + juce::String (i + 1) + " up");
        r.down.setTitle ("Move slot " + juce::String (i + 1) + " down");
        r.up.onClick = [this, i] { chain.moveSlot (i, i - 1); if (context.changed) context.changed(); };
        r.down.onClick = [this, i] { chain.moveSlot (i, i + 1); if (context.changed) context.changed(); };
        r.bypass.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0473c));
        r.bypass.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        for (juce::Button* b : { (juce::Button*) &r.list, (juce::Button*) &r.name, (juce::Button*) &r.bypass, (juce::Button*) &r.up, (juce::Button*) &r.down })
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
        r.name.setButtonText (! loaded ? juce::String ("(empty)") : chain.getDescription (i).name + (refused ? " (not stereo: off while linked)" : ""));
        r.name.setTooltip (! loaded ? "Choose a plugin for slot " + juce::String (i + 1)
                         : refused ? "This plugin has no stereo in/out, so it is left out while the aux pair is linked. Click to open its window"
                                   : "Open the plugin's window");
        r.bypass.setEnabled (loaded);
        r.bypass.setToggleState (chain.isBypassed (i), juce::dontSendNotification);
        r.up.setEnabled (i > 0 && (loaded || chain.getProcessor (i - 1) != nullptr));
        r.down.setEnabled (i < InsertChain::numSlots - 1 && (loaded || chain.getProcessor (i + 1) != nullptr));
    }
    repaint();
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
        r.list.setBounds (row.removeFromLeft (30));
        row.removeFromLeft (4);
        r.down.setBounds (row.removeFromRight (26));
        row.removeFromRight (2);
        r.up.setBounds (row.removeFromRight (26));
        row.removeFromRight (6);
        r.bypass.setBounds (row.removeFromRight (40));
        row.removeFromRight (6);
        r.name.setBounds (row);
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

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto& c = cells[i];
        for (int s = 0; s < InsertChain::numSlots; ++s)
        {
            c.slots[(size_t) s] = std::make_unique<SlotBox> (*this, i, s);
            addAndMakeVisible (*c.slots[(size_t) s]);

            auto& list = c.lists[(size_t) s];
            list = std::make_unique<PluginListButton> (s);
            list->setEnabled (context != nullptr);
            list->onClick = [this, i, s]
            {
                auto& cell = cells[i];
                InsertSlot::choose (*cell.chain, s, *context, cell.mainAssign, *cell.lists[(size_t) s]);
            };
            addAndMakeVisible (*list);
        }

        if (c.returnPan != nullptr)
        {
            c.knob = std::make_unique<Knob> (*c.returnPan, "Pan", juce::Colour (0xffd9d6cc));
            c.knob->setDialSize (26);
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

    // The stereo bus compressor lives in its own window now: SBC, above the master slots, opens it.
    sbcButton.setTitle ("Stereo bus compressor");
    sbcButton.setTooltip ("Stereo bus compressor (on the main mix, before the master inserts). Lit while it is in.");
    sbcButton.setClickingTogglesState (false);
    sbcButton.onClick = [this] { if (onOpenBusComp) onOpenBusComp(); };
    addAndMakeVisible (sbcButton);
    startTimerHz (10);
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

void InsertsSection::slotChanged()
{
    timerCallback();
    if (context != nullptr && context->changed)
        context->changed();
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
        for (auto& s : c.slots)
            s->refresh();

    sbcButton.setToggleState (engine.busComp.in->on(), juce::dontSendNotification);
    // The GR bar: falls back slowly, like the needle.
    const float gr = engine.busComp.takeGainReduction (1);
    const float next = juce::jmax (gr, shownGr - 1.5f);
    if (std::abs (next - shownGr) > 0.05f)
    {
        shownGr = next;
        repaint (grBar.expanded (2));
    }
}

int InsertsSection::columnX (int i) const
{
    const int n = (int) cells.size();
    if (i >= n)
        return getWidth();
    return columnW > 0.0f ? juce::roundToInt ((float) i * columnW) : i * getWidth() / n;
}

juce::Rectangle<int> InsertsSection::cellBounds (size_t i) const
{
    // A linked aux pair: the odd cell spans both, the even one has none.
    const int x0 = columnX ((int) i);
    auto cell = juce::Rectangle<int> (x0, 0, columnX ((int) i + 1) - x0, getHeight());
    const int k = (int) i - Config::numSubs;
    if (juce::isPositiveAndBelow (k, Config::numAux) && shownLink[(size_t) (k / 2)])
        return k % 2 == 0 ? cell.withRight (columnX ((int) i + 2)) : juce::Rectangle<int>();
    return cell;
}

void InsertsSection::resized()
{
    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto& c = cells[i];
        auto cell = cellBounds (i).reduced (4, 0);
        for (int s = 0; s < InsertChain::numSlots; ++s)
        {
            c.slots[(size_t) s]->setVisible (! cell.isEmpty());
            c.lists[(size_t) s]->setVisible (! cell.isEmpty());
        }
        if (c.knob != nullptr)
            c.knob->setVisible (! cell.isEmpty());
        if (cell.isEmpty())
            continue;

        cell.removeFromTop (16 + 18);   // title, then the aux Link buttons (kept on every cell so the slots line up)
        for (int s = 0; s < InsertChain::numSlots; ++s)
        {
            auto row = cell.removeFromTop (15);
            c.lists[(size_t) s]->setBounds (row.removeFromLeft (13));
            row.removeFromLeft (2);
            c.slots[(size_t) s]->setBounds (row);
            cell.removeFromTop (2);
        }
        if (c.knob != nullptr)
            c.knob->setBounds (cell.withSizeKeepingCentre (Knob::smallWidth, Knob::smallHeight));
    }

    // Master cell: SBC in the row the aux Link buttons use, the compressor's GR bar under the slots.
    {
        auto master = cellBounds (cells.size() - 1).reduced (4, 0);
        sbcButton.setBounds (master.withTrimmedTop (16).withHeight (16).reduced (2, 0));
        const int below = 16 + 18 + InsertChain::numSlots * 17 + 6;
        grBar = master.withTrimmedTop (below).withHeight (10).reduced (6, 0);
    }

    // Each Link button straddles the line between its pair.
    for (int j = 0; j < AudioEngine::numAuxPairs; ++j)
    {
        const int x = columnX (Config::numSubs + 2 * j + 1);
        linkButtons[(size_t) j].setBounds (x - 22, 17, 44, 16);
    }
}

void InsertsSection::paint (juce::Graphics& g)
{
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
            g.fillRect (columnX ((int) i), 2, 1, getHeight() - 4);
        }
    }

    // The bus compressor's gain reduction, 0..20 dB growing from the right, and its legend.
    if (! grBar.isEmpty())
    {
        auto bar = grBar.toFloat();
        const auto legendArea = bar.removeFromRight (20.0f);
        g.setColour (juce::Colour (0xff0b0c0d));
        g.fillRoundedRectangle (bar, 2.0f);
        const float w = bar.getWidth() * juce::jlimit (0.0f, 1.0f, shownGr / 20.0f);
        if (w > 0.5f)
        {
            g.setColour (amber);
            g.fillRoundedRectangle (bar.withLeft (bar.getRight() - w), 2.0f);
        }
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (9.0f, true));
        g.drawText ("GR", legendArea, juce::Justification::centredRight, false);
    }
}

//==============================================================================
InsertsSection::SlotBox::SlotBox (InsertsSection& o, size_t c, int s) : owner (o), cell (c), slot (s)
{
    setTitle (o.cells[c].chain->name + " slot " + juce::String (s + 1));
    refresh();
}

void InsertsSection::SlotBox::refresh()
{
    auto& chain = *owner.cells[cell].chain;
    const bool loaded = chain.getProcessor (slot) != nullptr;
    const int state = ! loaded ? 0 : chain.isStereoRefused (slot) ? 3 : chain.isBypassed (slot) ? 2 : 1;
    const auto name = loaded ? chain.getDescription (slot).name : juce::String();
    if (state == shownState && name == shownName)
        return;
    shownState = state;
    shownName = name;
    const auto where = "slot " + juce::String (slot + 1);
    setTooltip (state == 0 ? "Empty " + where + ": click to choose a plugin"
              : state == 3 ? name + " (" + where + ") has no stereo in/out, so it is left out while the aux pair is linked"
                           : name + " (" + where + (state == 2 ? ", bypassed" : "") + "): click to open, right-click for bypass / replace / remove");
    repaint();
}

void InsertsSection::SlotBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    juce::Colour fill (0xff0b0c0d), rim (0xff2a2d2f), text (0xff5d6467);
    if (shownState == 1)      { fill = amber;                   rim = juce::Colour (0xff8a6a24); text = juce::Colour (0xff1a1406); }
    else if (shownState == 2) { fill = juce::Colour (0xff4a3e22); rim = juce::Colour (0xff5e4d29); text = juce::Colour (0xffb39a62); }
    else if (shownState == 3) { fill = juce::Colour (0xff3a1512); rim = juce::Colour (0xff6b2a24); text = juce::Colour (0xffd0877f); }
    g.setColour (fill);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (rim);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

    g.setColour (text);
    auto font = ConsoleLookAndFeel::uiFont (9.5f, shownState == 1);
    if (shownState == 2)
        font = font.italicised();
    g.setFont (font);
    const auto textArea = getLocalBounds().reduced (3, 0);
    g.drawText (shownState == 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : shownName, textArea,
                shownState == 0 ? juce::Justification::centred : juce::Justification::centredLeft, true);
    if (shownState == 3)
        g.fillRect ((float) textArea.getX(), r.getCentreY(), (float) textArea.getWidth(), 1.0f);
}

void InsertsSection::SlotBox::mouseUp (const juce::MouseEvent& e)
{
    if (owner.context == nullptr || ! getLocalBounds().contains (e.getPosition()))
        return;
    auto& c = owner.cells[cell];
    auto& chain = *c.chain;
    auto& ctx = *owner.context;

    if (! e.mods.isPopupMenu() || chain.getProcessor (slot) == nullptr)
    {
        InsertSlot::click (chain, slot, ctx, c.mainAssign, *this);
        return;
    }

    juce::PopupMenu menu;
    menu.addItem (1, "Bypass", true, chain.isBypassed (slot));
    menu.addItem (2, "Replace...");
    menu.addItem (3, "Remove");
    juce::Component::SafePointer<SlotBox> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, &chain, &ctx, s = slot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& section = safe->owner;
        if (result == 1)      chain.setBypassed (s, ! chain.isBypassed (s));
        else if (result == 3) chain.clearSlot (s);
        else                  InsertSlot::choose (chain, s, ctx, section.cells[safe->cell].mainAssign, *safe);
        section.slotChanged();
    });
}
