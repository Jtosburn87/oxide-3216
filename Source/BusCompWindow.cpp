#include "BusCompWindow.h"
#include "BusCompPanel.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace { const juce::Colour panelColour { 0xff151617 }; }

namespace
{
    constexpr int margin = 12;
    constexpr int designW = BusCompPanel::width + 2 * margin, designH = BusCompPanel::height + 2 * margin;
}

/** Holds the faceplate at its design size, scales it to fit the window and drives its meter. */
class BusCompWindow::Content : public juce::Component,
                               private juce::Timer
{
public:
    explicit Content (AudioEngine& e) : panel (e.busComp)
    {
        addAndMakeVisible (panel);
        panel.setBounds (margin, margin, BusCompPanel::width, BusCompPanel::height);
        setSize (designW, designH);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override { g.fillAll (panelColour); }

    void resized() override
    {
        const float s = juce::jmin ((float) getWidth() / designW, (float) getHeight() / designH);
        panel.setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - s * designW) * 0.5f,
                                                                         (getHeight() - s * designH) * 0.5f));
    }

private:
    BusCompPanel panel;

    void timerCallback() override
    {
        if (isShowing())
            panel.update();
    }
};

BusCompWindow::BusCompWindow (AudioEngine& e, juce::PropertiesFile& s, std::function<void()> save)
    : juce::DocumentWindow ("Stereo bus compressor", panelColour, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      settings (s), saveSettings (std::move (save))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (e), true);
    constrainer.setFixedAspectRatio ((double) designW / designH);
    constrainer.setSizeLimits (designW * 2 / 3, designH * 2 / 3, designW * 3, designH * 3);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto state = settings.getValue ("busCompWindow");
    if (state.isEmpty() || ! restoreWindowStateFromString (state))
        centreWithSize (getWidth(), getHeight());
}

void BusCompWindow::show()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
}

void BusCompWindow::closeButtonPressed()
{
    settings.setValue ("busCompWindow", getWindowStateAsString());
    if (saveSettings)
        saveSettings();
    setVisible (false);
}
