#include "SectionWindow.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

//==============================================================================
ZoomView::ZoomView()
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    touch = std::make_unique<TouchPanZoom> (viewport, [this] { return zoom; },
                                            [this] (float z) { if (onZoomRequested) onZoomRequested (z); else setZoom (z); });
}

void ZoomView::setContent (juce::Component* c)
{
    if (content != nullptr)
        holder.removeChildComponent (content);
    content = c;
    if (content != nullptr)
        holder.addAndMakeVisible (content);
    contentSizeChanged();
}

void ZoomView::setZoom (float z)
{
    zoom = juce::jlimit (0.1f, 4.0f, z);
    contentSizeChanged();
}

float ZoomView::fitZoom() const
{
    if (content == nullptr || content->getWidth() <= 0 || content->getHeight() <= 0)
        return 1.0f;
    return juce::jmin ((float) viewport.getWidth() / (float) content->getWidth(),
                       (float) viewport.getHeight() / (float) content->getHeight());
}

void ZoomView::contentSizeChanged()
{
    if (content == nullptr)
        return;
    content->setTransform (juce::AffineTransform::scale (zoom));
    holder.setSize (juce::roundToInt ((float) content->getWidth() * zoom), juce::roundToInt ((float) content->getHeight() * zoom));
}

void ZoomView::resized()
{
    viewport.setBounds (getLocalBounds());
}

//==============================================================================
class SectionWindow::Content : public juce::Component
{
public:
    Content (juce::Component& section, std::function<void()> putBack)
        : frame (section)
    {
        zoomLabel.setText ("Zoom", juce::dontSendNotification);
        zoomLabel.setFont (ConsoleLookAndFeel::uiFont (14.0f));
        zoomLabel.setColour (juce::Label::textColourId, bridgeText);
        zoomLabel.setJustificationType (juce::Justification::centredRight);

        zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        zoomSlider.setRange (20.0, 200.0, 1.0);
        zoomSlider.setTitle ("Zoom");
        zoomSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff9aa0a3));
        zoomSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff5d6467));
        zoomSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff26292b));
        zoomSlider.onValueChange = [this] { view.setZoom ((float) zoomSlider.getValue() / 100.0f); };
        view.onZoomRequested = [this] (float z) { zoomSlider.setValue (z * 100.0f, juce::sendNotificationSync); };

        fitButton.setTooltip ("Zoom so the whole section fits the window");
        fitButton.onClick = [this] { fit(); };
        putBackButton.setTooltip ("Put this section back into the console window");
        putBackButton.onClick = [putBack] { putBack(); };

        for (juce::Component* c : { (juce::Component*) &zoomLabel, (juce::Component*) &zoomSlider,
                                    (juce::Component*) &fitButton, (juce::Component*) &putBackButton, (juce::Component*) &view })
            addAndMakeVisible (c);

        frame.onSizeChanged = [this] { view.contentSizeChanged(); };
        view.setContent (&frame);
        // First time out: most of the screen height, in the section's proportions.
        const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        const float z = juce::jmin (1.0f, (float) screen.getHeight() * 0.85f / (float) (frame.getHeight() + barH),
                                    (float) screen.getWidth() * 0.9f / (float) frame.getWidth());
        setSize (juce::jmax (400, juce::roundToInt ((float) frame.getWidth() * z) + 20), juce::roundToInt ((float) frame.getHeight() * z) + barH + 20);
    }

    ~Content() override
    {
        view.setContent (nullptr);
    }

    void fit()
    {
        zoomSlider.setValue (juce::jlimit (20.0, 200.0, std::floor (view.fitZoom() * 100.0)), juce::sendNotificationSync);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bridge);
        g.setColour (juce::Colour (0xff3a3f42));
        g.fillRect (0, barH - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (barH).reduced (10, 7);
        putBackButton.setBounds (bar.removeFromRight (90));
        bar.removeFromRight (8);
        fitButton.setBounds (bar.removeFromRight (56));
        bar.removeFromRight (6);
        zoomSlider.setBounds (bar.removeFromRight (juce::jmin (150, juce::jmax (60, bar.getWidth() - 50))));
        zoomLabel.setBounds (bar.removeFromRight (48));

        view.setBounds (getLocalBounds().withTrimmedTop (barH));
        if (! fitted && view.getWidth() > 0)
        {
            fitted = true;
            fit();
        }
    }

private:
    static constexpr int barH = 40;
    SectionFrame frame;
    ZoomView view;
    juce::Label zoomLabel;
    juce::Slider zoomSlider;
    juce::TextButton fitButton { "Fit" }, putBackButton { "Put back" };
    bool fitted = false;
};

//==============================================================================
SectionWindow::SectionWindow (Desk::Section s, juce::Component& sectionComponent,
                              std::function<void()> onPutBack, std::function<bool (const juce::KeyPress&)> keyHandler)
    : juce::DocumentWindow ("ConsoleTape  -  " + Desk::sectionName (s), room, juce::DocumentWindow::allButtons),
      section (s), putBack (std::move (onPutBack)), keys (std::move (keyHandler))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (sectionComponent, putBack), true);
    setResizable (true, false);
    setResizeLimits (300, 300, 10000, 10000);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}
