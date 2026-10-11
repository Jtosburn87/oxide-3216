#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Desk.h"
#include "TouchPanZoom.h"

/** Scrollable, zoomable view of a fixed-size component (the console, or one section of it). */
class ZoomView : public juce::Component
{
public:
    ZoomView();

    /** Not owned. */
    void setContent (juce::Component*);
    void setZoom (float);
    float getZoom() const noexcept { return zoom; }

    /** The zoom that fits the whole content in the view. */
    float fitZoom() const;

    /** Call when the content's own size has changed. */
    void contentSizeChanged();

    /** A pinch asks for this zoom (the owner usually routes it through its zoom slider). */
    std::function<void (float)> onZoomRequested;

    void resized() override;

private:
    juce::Viewport viewport;
    juce::Component holder;   // sized to the zoomed content, so the viewport scrolls correctly
    std::unique_ptr<TouchPanZoom> touch;
    juce::Component* content = nullptr;
    float zoom = 1.0f;
};

/** A section of the console in a window of its own (e.g. on a second monitor), with its own zoom.
    The centre section moves into the window: closing it, or "Put back", returns it to the console.
    A strip bank's window holds a copy (the console keeps the bank): closing it just closes it. */
class SectionWindow : public juce::DocumentWindow
{
public:
    SectionWindow (Desk::Section, juce::Component& section,
                   std::function<void()> putBack, std::function<bool (const juce::KeyPress&)> keys);
    /** Owns the copy. */
    SectionWindow (Desk::Section, std::unique_ptr<juce::Component> copy,
                   std::function<void()> close, std::function<bool (const juce::KeyPress&)> keys);

    void closeButtonPressed() override { putBack(); }
    bool keyPressed (const juce::KeyPress& k) override { return keys != nullptr && keys (k); }

    const Desk::Section section;

private:
    class Content;
    std::function<void()> putBack;
    std::function<bool (const juce::KeyPress&)> keys;
};
