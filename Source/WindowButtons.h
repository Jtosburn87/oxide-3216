#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ConsoleLookAndFeel.h"

/** The window's own buttons at the right end of the top bar, always there (full screen has no title bar):
    minimise ( _ ), full screen on / off (the square; two squares while full screen) and close (X). */
class WindowButtons : public juce::Component,
                      private juce::Timer
{
public:
    /** isFullScreen: full screen is on. toggleFullScreen: on <-> off. */
    WindowButtons (std::function<bool()> isFullScreen, std::function<void()> toggleFullScreen)
        : fullScreen (std::move (isFullScreen))
    {
        minimise.onClick = [this]
        {
            if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                w->setMinimised (true);
        };
        // After the click has finished: switching full screen rebuilds the window.
        square.onClick = [f = std::move (toggleFullScreen)] { if (f) juce::MessageManager::callAsync (f); };
        close.onClick = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
        minimise.setTooltip ("Minimise");
        close.setTooltip ("Close Oxide-3216");
        for (auto* b : { &minimise, &square, &close })
            addAndMakeVisible (b);
        update();
        startTimerHz (5);
    }

    static constexpr int buttonW = 46, width = 3 * buttonW;

    void resized() override
    {
        auto r = getLocalBounds();
        close.setBounds (r.removeFromRight (buttonW));
        square.setBounds (r.removeFromRight (buttonW));
        minimise.setBounds (r);
    }

private:
    enum class Glyph { minimise, square, close };

    /** A flat title-bar button with a drawn glyph (red behind the X on hover, as on Windows). */
    class GlyphButton : public juce::Button
    {
    public:
        explicit GlyphButton (Glyph g) : juce::Button ({}), glyph (g) {}

        bool doubled = false;   // the square as two (full screen: back to a window)

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat();
            if (over || down)
            {
                g.setColour (glyph == Glyph::close ? juce::Colour (down ? 0xffa8261c : 0xffc42b1c)
                                                   : juce::Colours::white.withAlpha (down ? 0.18f : 0.10f));
                g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
            }
            g.setColour (over && glyph == Glyph::close ? juce::Colours::white : ConsoleColours::legend);
            const auto c = r.getCentre();
            const float s = 5.5f;
            switch (glyph)
            {
                case Glyph::minimise:
                    g.fillRect (juce::Rectangle<float> (2.0f * s, 1.2f).withCentre (c));
                    break;
                case Glyph::square:
                    if (doubled)
                    {
                        const auto front = juce::Rectangle<float> (1.6f * s, 1.6f * s).withCentre (c.translated (-1.5f, 1.5f));
                        g.drawRect (front, 1.2f);
                        const auto back = front.translated (3.0f, -3.0f);   // only the part not behind the front one
                        g.fillRect (back.getX() + 3.0f - 0.6f, back.getY(), back.getWidth() - 2.4f, 1.2f);
                        g.fillRect (back.getRight() - 1.2f, back.getY(), 1.2f, back.getHeight() - 2.4f);
                    }
                    else
                    {
                        g.drawRect (juce::Rectangle<float> (2.0f * s, 2.0f * s).withCentre (c), 1.2f);
                    }
                    break;
                case Glyph::close:
                    g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 1.3f);
                    g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.3f);
                    break;
            }
        }

    private:
        const Glyph glyph;
    };

    std::function<bool()> fullScreen;
    GlyphButton minimise { Glyph::minimise }, square { Glyph::square }, close { Glyph::close };

    void update()
    {
        const bool on = fullScreen != nullptr && fullScreen();
        if (on == square.doubled && square.getTooltip().isNotEmpty())
            return;
        square.doubled = on;
        square.setTooltip (on ? "Back to a normal window (F11)" : "Full screen (F11)");
        square.repaint();
    }

    void timerCallback() override { update(); }
};
