#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

/** Touch gestures for a zoomable viewport: one finger drags the view around, two fingers pinch to zoom
    (keeping the spot between them in place, and panning as they move together).

    Only touches that start on the panels count: a touch that starts on a control (anything with the
    viewport-ignore-drag flag, or inside one) belongs to that control. Replaces the viewport's own
    drag-to-scroll, which cannot hand over to a pinch. The viewed component must hold the zoomed content
    at (0, 0), scaled by the zoom. */
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
    juce::Point<float> panStart;                 // one finger: where it started, and the view then
    juce::Point<int> viewStart;
    float pinchStartDistance = 0.0f, pinchStartZoom = 1.0f;
    juce::Point<float> pinchAnchor;              // two fingers: the unzoomed content point held between them

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

    void startPan (juce::Point<float> screenPos)
    {
        panStart = screenPos;
        viewStart = viewport.getViewPosition();
    }

    void startPinch()
    {
        const auto [mid, distance] = midAndDistance();
        pinchStartDistance = juce::jmax (distance, 1.0f);
        pinchStartZoom = getZoom();
        pinchAnchor = (viewport.getLocalPoint (nullptr, mid) + viewport.getViewPosition().toFloat()) / pinchStartZoom;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.source.isTouch() || startsOnControl (e.eventComponent))
            return;

        touches[e.source.getIndex()] = e.source.getScreenPosition();
        if (touches.size() == 1)
            startPan (e.source.getScreenPosition());
        else if (touches.size() == 2)
            startPinch();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto it = touches.find (e.source.getIndex());
        if (it == touches.end())
            return;
        it->second = e.source.getScreenPosition();

        if (touches.size() == 1)
        {
            viewport.setViewPosition (viewStart - (it->second - panStart).roundToInt());
        }
        else if (touches.size() == 2)
        {
            const auto [mid, distance] = midAndDistance();
            setZoom (pinchStartZoom * distance / pinchStartDistance);
            // Put the anchored content point back under the fingers' midpoint, at the zoom actually set.
            const auto anchorNow = pinchAnchor * getZoom();
            viewport.setViewPosition ((anchorNow - viewport.getLocalPoint (nullptr, mid)).roundToInt());
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (touches.erase (e.source.getIndex()) == 0)
            return;

        // Carry on smoothly with the fingers that are left.
        if (touches.size() == 1)
            startPan (touches.begin()->second);
        else if (touches.size() == 2)
            startPinch();
    }
};
