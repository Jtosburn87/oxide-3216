#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "AudioImport.h"

/** Session > Import audio: choose one or more files (up to 16 tape tracks' worth: a mono file takes one track,
    a stereo file any two, left and right), pick each file's track in the list or on the
    track overview, and where on the tape they all start. The files are decoded one after another with a progress
    window and written to tape. */
class ImportAudioDialog : public juce::Component,
                          private juce::ListBoxModel
{
public:
    ImportAudioDialog (AudioEngine&, juce::PropertiesFile& settings);
    ~ImportAudioDialog() override;

    /** Opens the window (or brings it to the front). */
    static void show (AudioEngine&, juce::PropertiesFile& settings);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** One file in the list and where it goes. */
    struct Item
    {
        juce::File file;
        AudioImport::Info info;
        int first = -1;    // the (left) tape track, 0-based; -1 = not placed
        int second = -1;   // a stereo file's right track (any other track)
        int span() const noexcept { return info.tracksNeeded(); }
        bool on (int track) const noexcept { return track >= 0 && (track == first || (span() == 2 && track == second)); }
    };

    /** The sixteen tracks: which file lands where (the selected file in amber). Click a track to put the
        selected file there (a stereo file's left channel, its right on the next free track). */
    class TrackPicker : public juce::Component
    {
    public:
        explicit TrackPicker (ImportAudioDialog& d) : dialog (d) { setTitle ("Tape tracks"); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        ImportAudioDialog& dialog;
    };

private:
    class Row;

    AudioEngine& engine;
    juce::PropertiesFile& settings;

    std::vector<Item> items;
    int selected = -1;
    juce::StringArray problems;   // files that could not be added, for the note

    juce::Label filesTitle, tracksTitle, startTitle, note;
    juce::TextButton addButton { "Add files..." }, clearButton { "Clear" }, importButton { "Import" }, cancelButton { "Cancel" };
    juce::ListBox list;
    TrackPicker picker { *this };
    juce::ComboBox startBox;
    std::unique_ptr<juce::FileChooser> chooser;

    enum StartIds { startOfTape = 1, currentPosition, inPoint };

    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int row) override;

    // Track bookkeeping.
    int tracksUsed() const;
    int occupant (int track, int except = -1) const;          // the item on a track, or -1
    bool fits (const Item&, int first, int second, int except) const;   // free of the other items
    bool autoPlace (int index);                               // the best free spot (an empty track if it can)
    bool repackAll();                                         // stereo pairs first, then mono
    bool move (int index, int track);                         // overview click: stereo left there, right on the next free track
    bool setTracks (int index, int first, int second);        // the files in the way go elsewhere
    void removeItem (int index);
    void select (int index);

    void chooseFiles();
    void addFiles (const juce::Array<juce::File>&);
    void refresh();
    juce::int64 startSample() const;
    juce::int64 lengthOnTape (const Item&) const;
    juce::String formatTime (juce::int64 samples) const;
    static juce::String describe (const AudioImport::Info&);
    void startImport();
    void close();

    static juce::Component::SafePointer<juce::DialogWindow> window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImportAudioDialog)
};
