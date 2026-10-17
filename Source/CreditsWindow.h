#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The Credits window (top-bar Credits button): the logo, the version and a note from the author.
    The text can be selected, so the email address can be copied. */
class CreditsWindow : public juce::Component
{
public:
    /** Opens the window (or brings it to the front), centred on the screen at full size. */
    static void show();

    CreditsWindow();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::unique_ptr<juce::Drawable> logo;
    juce::TextEditor text;
    juce::HyperlinkButton emailButton;
    juce::TextButton closeButton { "Close" };

    static juce::Component::SafePointer<juce::DialogWindow> window;
};
