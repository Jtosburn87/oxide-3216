#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ConsoleLookAndFeel.h"

/** Full screen has no title bar: this one drops down while the mouse is at the very top of the screen
    (as in a browser's full screen), with the window's title and Exit full screen, Minimise and Close.
    It goes back up once the mouse moves well below it. Lives on top of the window's content. */
class TopEdgeBar : public juce::Component,
                   private juce::Timer
{
public:
    /** isActive: full screen is on. exitFullScreen: back to a normal window. */
    TopEdgeBar (std::function<bool()> isActive, std::function<void()> exitFullScreen)
        : active (std::move (isActive))
    {
        // After the click has finished: leaving full screen rebuilds the window.
        exitButton.onClick = [this, f = std::move (exitFullScreen)] { hideBar(); if (f) juce::MessageManager::callAsync (f); };
        minimiseButton.onClick = [this]
        {
            hideBar();
            if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                w->setMinimised (true);
        };
        closeButton.onClick = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
        exitButton.setTooltip ("Back to a normal window with a title bar (F11)");
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8c2a22));
        for (auto* b : { &exitButton, &minimiseButton, &closeButton })
            addAndMakeVisible (b);
        setVisible (false);
        startTimerHz (20);
    }

    static constexpr int height = 36;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1a1c1e));
        g.setColour (juce::Colours::black);
        g.fillRect (0, getHeight() - 2, getWidth(), 2);
        g.setColour (ConsoleColours::legend);
        g.setFont (ConsoleLookAndFeel::uiFont (14.0f));
        if (auto* top = getTopLevelComponent())
            g.drawText (top->getName(), getLocalBounds().reduced (14, 0).withTrimmedRight (380), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 5).withTrimmedBottom (2);
        closeButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (8);
        minimiseButton.setBounds (r.removeFromRight (90));
        r.removeFromRight (8);
        exitButton.setBounds (r.removeFromRight (140));
    }

private:
    std::function<bool()> active;
    juce::TextButton exitButton { "Exit full screen" }, minimiseButton { "Minimise" }, closeButton { "Close" };
    bool shown = false;

    void showBar()
    {
        shown = true;
        setBounds (0, -height, getParentWidth(), height);
        setVisible (true);
        toFront (false);
        juce::Desktop::getInstance().getAnimator().animateComponent (this, getBounds().withY (0), 1.0f, 120, false, 1.0, 1.0);
    }

    void hideBar()
    {
        if (! shown)
            return;
        shown = false;
        juce::Desktop::getInstance().getAnimator().animateComponent (this, getBounds().withY (-height), 0.0f, 120, true, 1.0, 1.0);
        setVisible (false);
    }

    void timerCallback() override
    {
        auto* parent = getParentComponent();
        if (parent == nullptr || active == nullptr || ! active() || ! juce::Process::isForegroundProcess())
        {
            hideBar();
            return;
        }

        const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
        const auto p = parent->getLocalPoint (nullptr, mouse.getScreenPosition()).roundToInt();
        const bool overWidth = p.x >= 0 && p.x < parent->getWidth();
        if (! shown && overWidth && p.y >= -1 && p.y <= 2)
            showBar();
        else if (shown && ! mouse.isDragging() && (! overWidth || p.y > height + 40))
            hideBar();
    }
};
