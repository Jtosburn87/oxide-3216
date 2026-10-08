#include "PluginWindows.h"

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
    windows.add (new Window (*this, p, title));
}

void PluginWindows::close (juce::AudioProcessor* p)
{
    for (int i = windows.size(); --i >= 0;)
        if (&windows[i]->processor == p)
            windows.remove (i);
}
