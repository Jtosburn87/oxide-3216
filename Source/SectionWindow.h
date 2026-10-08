#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Desk.h"

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

    void resized() override;

private:
    juce::Viewport viewport;
    juce::Component holder;   // sized to the zoomed content, so the viewport scrolls correctly
    juce::Component* content = nullptr;
    float zoom = 1.0f;
};

/** A section of the console in a window of its own (e.g. on a second monitor), with its own zoom.
    Closing the window, or "Put back", returns the section to the console. */
class SectionWindow : public juce::DocumentWindow
{
public:
    SectionWindow (Desk::Section, juce::Component& section,
                   std::function<void()> putBack, std::function<bool (const juce::KeyPress&)> keys);

    void closeButtonPressed() override { putBack(); }
    bool keyPressed (const juce::KeyPress& k) override { return keys != nullptr && keys (k); }

    const Desk::Section section;

private:
    class Content;
    std::function<void()> putBack;
    std::function<bool (const juce::KeyPress&)> keys;
};
