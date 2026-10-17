#include "CreditsWindow.h"
#include "ConsoleLookAndFeel.h"
#include "BinaryData.h"

using namespace ConsoleColours;

namespace
{
    constexpr int logoH = 96, pad = 20;

    const char* const creditsText =
        "This app is a home brewed, vibe coded project that I made for myself as a challenge to stop relying on "
        "editing, tuning and triggering my way out of bad recordings. If you'd like to join me in creating flawed, "
        "glorious, human music with our real human hands and voices, give it a shot. I did not make this to turn a "
        "profit and it didn't cost much of anything but my time. This is software I created, but I am distributing "
        "it for free and you are welcome to use it, share it, fork it and modify it all you want. If you'd like to "
        "say thanks, my PayPal is Justinosburn@live.com\n"
        "If you have any bugs or feature requests shoot me an email at the same email.\n"
        "\n"
        "Happy tracking,\n"
        "Justin Osburn";
}

juce::Component::SafePointer<juce::DialogWindow> CreditsWindow::window;

void CreditsWindow::show()
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new CreditsWindow());
    o.dialogTitle = "Credits";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    window = o.launchAsync();
}

CreditsWindow::CreditsWindow()
{
    logo = juce::Drawable::createFromImageData (BinaryData::oxide3216logo_svg, (size_t) BinaryData::oxide3216logo_svgSize);

    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setCaretVisible (false);
    text.setScrollbarsShown (false);
    text.setFont (juce::FontOptions (16.0f));
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::textColourId, legend);
    text.setText (creditsText, false);
    addAndMakeVisible (text);

    emailButton.setButtonText ("Justinosburn@live.com");
    emailButton.setURL (juce::URL ("mailto:Justinosburn@live.com?subject=Oxide-3216"));
    emailButton.setFont (juce::FontOptions (15.0f), false, juce::Justification::centred);
    emailButton.setColour (juce::HyperlinkButton::textColourId, amber);
    emailButton.setTooltip ("Write an email (bugs, feature requests)");
    addAndMakeVisible (emailButton);

    closeButton.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0); };
    addAndMakeVisible (closeButton);

    setSize (560, 440);
}

void CreditsWindow::paint (juce::Graphics& g)
{
    g.fillAll (room);
    auto top = getLocalBounds().reduced (pad).removeFromTop (logoH);
    if (logo != nullptr)
        logo->drawWithin (g, top.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    g.setColour (legend.withAlpha (0.6f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion(),
                getLocalBounds().reduced (pad).withTrimmedTop (logoH + 4).removeFromTop (20),
                juce::Justification::centred);
}

void CreditsWindow::resized()
{
    auto r = getLocalBounds().reduced (pad);
    r.removeFromTop (logoH + 34);
    auto bottom = r.removeFromBottom (30);
    closeButton.setBounds (bottom.removeFromRight (90));
    r.removeFromBottom (8);
    emailButton.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (6);
    text.setBounds (r);
}
