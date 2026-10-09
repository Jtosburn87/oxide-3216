#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "AudioImport.h"

/** Session > Import audio: choose a file, the track (a stereo file takes two neighbouring tracks)
    and where on the tape it starts. The file is decoded with a progress window, then written to tape. */
class ImportAudioDialog : public juce::Component
{
public:
    ImportAudioDialog (AudioEngine&, juce::PropertiesFile& settings);
    ~ImportAudioDialog() override;

    /** Opens the window (or brings it to the front). */
    static void show (AudioEngine&, juce::PropertiesFile& settings);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Sixteen track cells; picks one track, or two neighbouring tracks for a stereo file. */
    class TrackPicker : public juce::Component
    {
    public:
        TrackPicker();
        void setSpan (int numTracks);   // 1 or 2
        int getSpan() const noexcept { return span; }
        void setFirst (int track);
        int getFirst() const noexcept { return first; }

        std::function<bool (int)> hasAudio;
        std::function<void()> onChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        int span = 1, first = 0;
        void pickAt (int x);
    };

private:
    AudioEngine& engine;
    juce::PropertiesFile& settings;

    juce::File file;
    AudioImport::Info info;

    juce::Label fileTitle, fileName, infoLabel, tracksTitle, startTitle, note;
    juce::TextButton chooseButton { "Choose file..." }, importButton { "Import" }, cancelButton { "Cancel" };
    TrackPicker picker;
    juce::ComboBox startBox;
    std::unique_ptr<juce::FileChooser> chooser;

    enum StartIds { startOfTape = 1, currentPosition, inPoint };

    void chooseFile();
    void setFile (const juce::File&);
    void refresh();
    juce::int64 startSample() const;
    juce::int64 lengthOnTape() const;
    juce::String formatTime (juce::int64 samples) const;
    void startImport();
    void close();

    static juce::Component::SafePointer<juce::DialogWindow> window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImportAudioDialog)
};
