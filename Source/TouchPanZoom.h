#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

/** Touch gestures for a zoomable viewport: two fingers pinch to zoom, keeping the spot where the pinch
    started in place. Touch never moves the view otherwise (no one-finger drag, and moving both fingers
    together does not pan): the scroll bars are the only way round, so a stray finger or palm can't slide
    the console from under a fader.

    Only touches that start on the panels count: a touch that starts on a control (anything with the
    viewport-ignore-drag flag, or inside one) belongs to that control. The viewed component must hold the
    zoomed content at (0, 0), scaled by the zoom. */
class TouchPanZoom : private juce::MouseListener
{
public:
    TouchPanZoom (juce::Viewport& v, std::function<float()> getZoomFn, std::function<void (float)> setZoomFn)
        : viewport (v), getZoom (std::move (getZoomFn)), setZoom (std::move (setZoomFn))
    {
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
        if (auto* viewed = viewport.getViewedComponent())
            viewed->addMouseListener (this, true);
    }

    ~TouchPanZoom() override
    {
        if (auto* viewed = viewport.getViewedComponent())
            viewed->removeMouseListener (this);
    }

private:
    juce::Viewport& viewport;
    std::function<float()> getZoom;
    std::function<void (float)> setZoom;

    std::map<int, juce::Point<float>> touches;   // finger (input source index) -> screen position
    float pinchStartDistance = 0.0f, pinchStartZoom = 1.0f;
    juce::Point<float> pinchAnchor;              // the unzoomed content point where the pinch started
    juce::Point<float> pinchSpot;                // ... and where it sits in the viewport (kept there)

    bool startsOnControl (const juce::Component* c) const
    {
        for (; c != nullptr && c != &viewport; c = c->getParentComponent())
            if (c->getViewportIgnoreDragFlag())
                return true;
        return false;
    }

    std::pair<juce::Point<float>, float> midAndDistance() const
    {
        const auto a = touches.begin()->second, b = std::next (touches.begin())->second;
        return { (a + b) * 0.5f, a.getDistanceFrom (b) };
    }

    void startPinch()
    {
        const auto [mid, distance] = midAndDistance();
        pinchStartDistance = juce::jmax (distance, 1.0f);
        pinchStartZoom = getZoom();
        pinchSpot = viewport.getLocalPoint (nullptr, mid);
        pinchAnchor = (pinchSpot + viewport.getViewPosition().toFloat()) / pinchStartZoom;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.source.isTouch() || startsOnControl (e.eventComponent))
            return;

        touches[e.source.getIndex()] = e.source.getScreenPosition();
        if (touches.size() == 2)
            startPinch();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto it = touches.find (e.source.getIndex());
        if (it == touches.end())
            return;
        it->second = e.source.getScreenPosition();

        if (touches.size() == 2)
        {
            setZoom (pinchStartZoom * midAndDistance().second / pinchStartDistance);
            // Keep the content point where the pinch started at the same place, at the zoom actually set.
            viewport.setViewPosition ((pinchAnchor * getZoom() - pinchSpot).roundToInt());
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (touches.erase (e.source.getIndex()) == 0)
            return;
        if (touches.size() == 2)   // a third finger lifted: carry on with the two left
            startPinch();
    }
};
