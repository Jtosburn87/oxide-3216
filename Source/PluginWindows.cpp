#include "PluginWindows.h"

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#endif

PluginWindows::Window::Window (PluginWindows& o, juce::AudioProcessor& p, const juce::String& title)
    : juce::DocumentWindow (title, juce::Colour (0xff161718), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      owner (o), processor (p)
{
    setUsingNativeTitleBar (true);

    juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (p);   // plugins without their own window

    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setAlwaysOnTop (o.onTop);
    setVisible (true);
}

PluginWindows::Window::~Window()
{
    // The editor must go before the processor; clearing the content deletes it here.
    clearContentComponent();
}

void PluginWindows::open (juce::AudioProcessor& p, const juce::String& title)
{
    for (auto* w : windows)
    {
        if (&w->processor == &p)
        {
            w->setMinimised (false);
            w->toFront (true);
            return;
        }
    }
    onTop = juce::Process::isForegroundProcess();
    windows.add (new Window (*this, p, title));
    startTimer (200);
}

void PluginWindows::close (juce::AudioProcessor* p)
{
    for (int i = windows.size(); --i >= 0;)
        if (&windows[i]->processor == p)
            windows.remove (i);

    if (windows.isEmpty())
        stopTimer();
}

void PluginWindows::timerCallback()
{
    const bool active = juce::Process::isForegroundProcess();
    if (active != onTop)
        setOnTop (active);
}

void PluginWindows::setOnTop (bool shouldBeOnTop)
{
    onTop = shouldBeOnTop;

   #if JUCE_WINDOWS
    // Going behind another app: slot each window straight in under that app's window first. Windows
    // drops "topmost" when a window is placed after a normal one, so the JUCE call below is then a
    // no-op instead of briefly raising the plugin above everything (HWND_NOTOPMOST would).
    if (! onTop)
        if (auto* front = GetForegroundWindow())
            for (auto* w : windows)
                if (auto* peer = w->getPeer())
                    SetWindowPos ((HWND) peer->getNativeHandle(), front, 0, 0, 0, 0,
                                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
   #endif

    for (auto* w : windows)
        w->setAlwaysOnTop (onTop);
}
