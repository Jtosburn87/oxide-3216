#include "ImportAudioDialog.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    constexpr int numTracks = Config::numTracks;

    /** The files to import, where, and what went wrong so far. */
    struct Batch
    {
        struct File { juce::File file; int track, right; };   // right: a stereo file's right track
        std::vector<File> files;
        juce::int64 start = 0;
        juce::StringArray problems;
    };

    /** Decodes one file on a worker thread, writes it to tape on the message thread, then starts the next one
        (so only one file is ever held in memory). */
    class ImportJob : public juce::ThreadWithProgressWindow
    {
    public:
        ImportJob (AudioEngine& e, std::shared_ptr<Batch> b, size_t i)
            : juce::ThreadWithProgressWindow ("Importing " + b->files[i].file.getFileName()
                                                  + (b->files.size() > 1 ? "  (" + juce::String ((int) i + 1) + " of " + juce::String ((int) b->files.size()) + ")" : juce::String()),
                                              true, true),
              engine (e), batch (std::move (b)), index (i),
              rate (e.tape.getSampleRate()), capacity (e.tape.getCapacity() - batch->start)
        {
            setStatusMessage ("Recording file's audio onto the tape");
        }

        void run() override
        {
            audio = AudioImport::read (batch->files[index].file, rate, capacity, [this] (double p)
            {
                setProgress (p);
                return ! threadShouldExit();
            });
        }

        void threadComplete (bool cancelled) override
        {
            const auto& f = batch->files[index];
            if (audio.ok && ! cancelled)
            {
                engine.tape.stop();
                AudioImport::place (engine.tape, audio, f.track, batch->start, f.right);
                if (audio.channels.size() == 2)
                    engine.linkStereoReturns (f.track, f.right);   // linked, panned hard left / right
                if (audio.truncated)
                    batch->problems.add (f.file.getFileName() + ": only the part that fits on the tape ("
                                         + juce::String (Config::maxTapeMinutes) + " minutes) was imported.");
            }
            else if (! cancelled && audio.error.isNotEmpty())
            {
                batch->problems.add (f.file.getFileName() + ": " + audio.error);
            }

            if (! cancelled && index + 1 < batch->files.size())
                (new ImportJob (engine, batch, index + 1))->launchThread();
            else if (! batch->problems.isEmpty() || cancelled)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                        (cancelled ? "Import cancelled: " + juce::String ((int) index) + " of "
                                                                         + juce::String ((int) batch->files.size()) + " files went onto the tape.\n" : juce::String())
                                                        + batch->problems.joinIntoString ("\n"));
            delete this;
        }

    private:
        AudioEngine& engine;
        std::shared_ptr<Batch> batch;
        size_t index;
        double rate;
        juce::int64 capacity;
        AudioImport::Audio audio;
    };

    juce::String kHz (double rate)   // 44.1, 48
    {
        const auto k = rate / 1000.0;
        return std::abs (k - std::round (k)) < 0.05 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1);
    }

    const juce::Colour otherFile { 0xff4f7fa6 };   // a placed file that is not the selected one
}

juce::Component::SafePointer<juce::DialogWindow> ImportAudioDialog::window;

//==============================================================================
/** A file in the list: number, name and format, its track(s), remove. */
class ImportAudioDialog::Row : public juce::Component
{
public:
    explicit Row (ImportAudioDialog& d) : dialog (d)
    {
        tracks.setTooltip ("The tape track for this file (a stereo file's left channel). A file already there moves out of the way.");
        rightTracks.setTooltip ("The tape track for this stereo file's right channel: any track. "
                                "Their return channels are linked, and panned hard left and right.");
        tracks.onChange = [this]
        {
            const auto& item = dialog.items[(size_t) row];
            const int t = tracks.getSelectedId() - 1;
            if (t >= 0)
                dialog.setTracks (row, t, t == item.second ? item.first : item.second);   // onto its right track: they swap
        };
        rightTracks.onChange = [this]
        {
            const auto& item = dialog.items[(size_t) row];
            const int t = rightTracks.getSelectedId() - 1;
            if (t >= 0)
                dialog.setTracks (row, t == item.first ? item.second : item.first, t);
        };
        addChildComponent (rightTracks);
        remove.setTooltip ("Leave this file out");
        remove.onClick = [this] { dialog.removeItem (row); };
        addAndMakeVisible (tracks);
        addAndMakeVisible (remove);
        setInterceptsMouseClicks (true, true);
    }

    void update (int r, bool isSelected)
    {
        row = r;
        selectedRow = isSelected;
        const auto& item = dialog.items[(size_t) r];
        const bool stereo = item.span() == 2;
        const auto fill = [&] (juce::ComboBox& box, const juce::String& prefix, int current, int own)
        {
            box.clear (juce::dontSendNotification);
            for (int t = 0; t < numTracks; ++t)
            {
                juce::String text = prefix + juce::String (t + 1);
                if (const int other = dialog.occupant (t, r); other >= 0)
                    text << "   (#" << (other + 1) << " moves)";
                else if (t == own)
                    text << "   (swaps)";
                else if (dialog.engine.tape.trackHasAudio (t))
                    text << "   (has audio)";
                box.addItem (text, t + 1);
            }
            box.setSelectedId (current + 1, juce::dontSendNotification);
        };
        fill (tracks, stereo ? "L: " : "Track ", item.first, stereo ? item.second : -1);
        if (stereo)
            fill (rightTracks, "R: ", item.second, item.first);
        rightTracks.setVisible (stereo);
        resized();
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { dialog.select (row); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 3);
        remove.setBounds (r.removeFromRight (26));
        r.removeFromRight (6);
        auto boxes = r.removeFromRight (190);
        if (rightTracks.isVisible())
        {
            rightTracks.setBounds (boxes.removeFromRight (92));
            boxes.removeFromRight (6);
        }
        tracks.setBounds (boxes);
    }

    void paint (juce::Graphics& g) override
    {
        if (selectedRow)
            g.fillAll (juce::Colour (0xff2b4a6b));
        g.setColour (juce::Colour (0xff191b1c));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        const auto& item = dialog.items[(size_t) row];
        const int textW = getWidth() - 236;
        g.setColour (selectedRow ? amber : otherFile.brighter (0.4f));
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
        g.drawText ("#" + juce::String (row + 1), 8, 0, 30, getHeight(), juce::Justification::centredLeft, false);
        g.setColour (legend);
        g.setFont (ConsoleLookAndFeel::uiFont (13.0f));
        g.drawText (item.file.getFileName(), 40, 0, textW - 40, getHeight() / 2 + 4, juce::Justification::bottomLeft, true);
        g.setColour (legend.withAlpha (0.55f));
        g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
        g.drawText (describe (item.info) + "   " + dialog.formatTime (dialog.lengthOnTape (item)), 40, getHeight() / 2 + 2, textW - 40, getHeight() / 2 - 4,
                    juce::Justification::topLeft, true);
    }

private:
    ImportAudioDialog& dialog;
    juce::ComboBox tracks, rightTracks;   // right: a stereo file's right channel
    juce::TextButton remove { juce::CharPointer_UTF8 ("\xe2\x9c\x95") };
    int row = 0;
    bool selectedRow = false;
};

//==============================================================================
void ImportAudioDialog::TrackPicker::paint (juce::Graphics& g)
{
    const auto cell = (float) getWidth() / (float) numTracks;
    const auto h = (float) getHeight();
    for (int t = 0; t < numTracks; ++t)
    {
        const auto r = juce::Rectangle<float> (cell * (float) t, 0.0f, cell, h).reduced (2.0f);
        const int who = dialog.occupant (t);
        const bool isSelected = who >= 0 && who == dialog.selected;
        const auto fill = isSelected ? amber : who >= 0 ? otherFile : plate;

        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (who >= 0 ? fill.brighter (0.3f) : edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        g.setColour (isSelected ? juce::Colours::black : legend);
        g.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
        g.drawText (juce::String (t + 1), r.withTrimmedBottom (h * 0.35f), juce::Justification::centred);

        // Which file (and L / R for stereo); a bar for audio already on the track (it gets replaced).
        if (who >= 0)
        {
            const auto& item = dialog.items[(size_t) who];
            g.setFont (ConsoleLookAndFeel::uiFont (11.0f));
            g.drawText ("#" + juce::String (who + 1) + (item.span() == 2 ? (t == item.first ? " L" : " R") : juce::String()),
                        r.withTrimmedTop (h * 0.5f).withTrimmedBottom (6.0f), juce::Justification::centred);
        }
        if (dialog.engine.tape.trackHasAudio (t))
        {
            g.setColour (isSelected ? juce::Colours::black.withAlpha (0.6f) : legend.withAlpha (0.55f));
            g.fillRoundedRectangle (r.withTrimmedTop (h - 7.0f).reduced (5.0f, 0.0f).withHeight (3.0f), 1.5f);
        }
    }
}

void ImportAudioDialog::TrackPicker::mouseDown (const juce::MouseEvent& e)
{
    const int t = juce::jlimit (0, numTracks - 1, (int) ((float) e.x / ((float) getWidth() / (float) numTracks)));
    if (dialog.selected >= 0)
    {
        dialog.move (dialog.selected, t);   // a stereo file's left channel, its right on the next free track
    }
    else if (dialog.occupant (t) >= 0)
    {
        dialog.select (dialog.occupant (t));
    }
}

//==============================================================================
ImportAudioDialog::ImportAudioDialog (AudioEngine& e, juce::PropertiesFile& s)
    : engine (e), settings (s)
{
    const auto styleLabel = [this] (juce::Label& l, const juce::String& text, float size, juce::Colour c)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (ConsoleLookAndFeel::uiFont (size));
        l.setColour (juce::Label::textColourId, c);
        addAndMakeVisible (l);
    };
    styleLabel (filesTitle, "Audio files", 14.0f, legend.withAlpha (0.7f));
    styleLabel (tracksTitle, "Tape tracks: select a file, then click where it goes", 14.0f, legend.withAlpha (0.7f));
    styleLabel (startTitle, "Start at", 14.0f, legend.withAlpha (0.7f));
    styleLabel (note, {}, 14.0f, amber);
    note.setJustificationType (juce::Justification::topLeft);

    addButton.setTooltip ("Choose audio files (several at once: Ctrl or Shift click). Up to 16 tracks: mono files take one, stereo files two.");
    clearButton.setTooltip ("Take every file off the list");
    addButton.onClick = [this] { chooseFiles(); };
    clearButton.onClick = [this] { items.clear(); problems.clear(); selected = -1; refresh(); };
    importButton.onClick = [this] { startImport(); };
    cancelButton.onClick = [this] { close(); };
    for (auto* b : { &addButton, &clearButton, &importButton, &cancelButton })
        addAndMakeVisible (b);

    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0e0f10));
    list.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2a2d30));
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
    addAndMakeVisible (picker);

    startBox.setTitle ("Start at");
    startBox.onChange = [this] { refresh(); };
    addAndMakeVisible (startBox);

    auto& tape = engine.tape;
    startBox.addItem ("Start of the tape", startOfTape);
    if (tape.getPosition() > 0)
        startBox.addItem ("Tape position (" + formatTime (tape.getPosition()) + ")", currentPosition);
    if (tape.getInPoint() > 0)
        startBox.addItem ("In point (" + formatTime (tape.getInPoint()) + ")", inPoint);
    startBox.setSelectedId (startOfTape, juce::dontSendNotification);

    setSize (700, 560);
    refresh();
}

ImportAudioDialog::~ImportAudioDialog()
{
    list.setModel (nullptr);
}

void ImportAudioDialog::show (AudioEngine& engine, juce::PropertiesFile& settings)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    auto* content = new ImportAudioDialog (engine, settings);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = "Import audio";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    window = o.launchAsync();
    content->chooseFiles();   // straight to the file browser
}

void ImportAudioDialog::paint (juce::Graphics& g)
{
    g.fillAll (room);
}

void ImportAudioDialog::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    auto row = area.removeFromTop (30);
    clearButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (8);
    addButton.setBounds (row.removeFromRight (120));
    filesTitle.setBounds (row);
    area.removeFromTop (6);
    list.setBounds (area.removeFromTop (220));

    area.removeFromTop (10);
    tracksTitle.setBounds (area.removeFromTop (20));
    picker.setBounds (area.removeFromTop (54));

    area.removeFromTop (10);
    row = area.removeFromTop (28);
    startTitle.setBounds (row.removeFromLeft (80));
    startBox.setBounds (row.removeFromLeft (280));

    auto buttons = area.removeFromBottom (32);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    importButton.setBounds (buttons.removeFromRight (100));

    area.removeFromTop (6);
    note.setBounds (area);
}

//==============================================================================
juce::Component* ImportAudioDialog::refreshComponentForRow (int row, bool isSelected, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
    {
        delete existing;
        return nullptr;
    }
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }
    r->update (row, isSelected);
    return r;
}

void ImportAudioDialog::selectedRowsChanged (int row)
{
    if (row != selected)
    {
        selected = row;
        refresh();
    }
}

void ImportAudioDialog::select (int index)
{
    selected = juce::isPositiveAndBelow (index, (int) items.size()) ? index : -1;
    list.selectRow (selected, false, true);
    refresh();
}

int ImportAudioDialog::tracksUsed() const
{
    int n = 0;
    for (auto& i : items)
        n += i.span();
    return n;
}

int ImportAudioDialog::occupant (int track, int except) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (i != except && items[(size_t) i].on (track))
            return i;
    return -1;
}

bool ImportAudioDialog::fits (const Item& item, int first, int second, int except) const
{
    if (! juce::isPositiveAndBelow (first, numTracks) || occupant (first, except) >= 0)
        return false;
    return item.span() == 1 || (juce::isPositiveAndBelow (second, numTracks) && second != first && occupant (second, except) < 0);
}

bool ImportAudioDialog::autoPlace (int index)
{
    auto& item = items[(size_t) index];
    const auto empty = [this] (int t) { return ! engine.tape.trackHasAudio (t); };
    item.first = item.second = -1;
    if (item.span() == 1)
    {
        for (int pass = 0; pass < 2; ++pass)   // a track with nothing on it, if there is one
            for (int t = 0; t < numTracks; ++t)
                if (fits (item, t, -1, index) && (pass == 1 || empty (t)))
                {
                    item.first = t;
                    return true;
                }
        return false;
    }
    // Stereo: an empty neighbouring pair, any neighbouring pair, any two empty tracks, any two tracks.
    for (int pass = 0; pass < 4; ++pass)
    {
        const bool pairs = pass < 2, needEmpty = pass % 2 == 0;
        for (int a = 0; a < numTracks; a += pairs ? 2 : 1)
            for (int b = a + 1; b < (pairs ? juce::jmin (a + 2, numTracks) : numTracks); ++b)
                if (fits (item, a, b, index) && (! needEmpty || (empty (a) && empty (b))))
                {
                    item.first = a;
                    item.second = b;
                    return true;
                }
    }
    return false;
}

bool ImportAudioDialog::repackAll()
{
    for (auto& i : items)
        i.first = i.second = -1;
    for (int pass = 0; pass < 2; ++pass)   // stereo first, so they get neighbouring pairs while there are some
        for (int i = 0; i < (int) items.size(); ++i)
            if ((items[(size_t) i].span() == 2) == (pass == 0) && ! autoPlace (i))
                return false;
    return true;
}

bool ImportAudioDialog::move (int index, int track)
{
    if (! juce::isPositiveAndBelow (index, (int) items.size()))
        return false;
    if (items[(size_t) index].span() == 1)
        return setTracks (index, track, -1);
    // Stereo: left on the track clicked, right on the next track along that no other file uses.
    int right = -1;
    for (int k = 1; k < numTracks && right < 0; ++k)
        if (const int t = (track + k) % numTracks; occupant (t, index) < 0)
            right = t;
    return setTracks (index, track, right >= 0 ? right : (track + 1) % numTracks);
}

bool ImportAudioDialog::setTracks (int index, int first, int second)
{
    if (! juce::isPositiveAndBelow (index, (int) items.size()))
        return false;
    auto& item = items[(size_t) index];
    problems.clear();
    if (item.span() == 1)
        second = -1;
    if (first == item.first && second == item.second)
    {
        select (index);
        return true;
    }
    if (! juce::isPositiveAndBelow (first, numTracks) || (item.span() == 2 && (! juce::isPositiveAndBelow (second, numTracks) || second == first)))
        return false;

    // Put it there; whatever was in the way finds another place (stereo files first), or nothing changes.
    std::vector<std::pair<int, int>> before;
    for (auto& i : items)
        before.push_back ({ i.first, i.second });
    std::vector<int> displaced;
    for (const int t : { first, second })
        if (const int o = occupant (t, index); o >= 0 && std::find (displaced.begin(), displaced.end(), o) == displaced.end())
            displaced.push_back (o);
    item.first = first;
    item.second = second;
    for (int o : displaced)
        items[(size_t) o].first = items[(size_t) o].second = -1;
    bool ok = true;
    for (int pass = 0; pass < 2 && ok; ++pass)
        for (int o : displaced)
            if ((items[(size_t) o].span() == 2) == (pass == 0))
                ok = ok && autoPlace (o);
    if (! ok)
    {
        for (size_t i = 0; i < items.size(); ++i)
            std::tie (items[i].first, items[i].second) = before[i];
        problems = { "No room there for the files in the way: move a mono file first." };
    }
    select (index);
    return ok;
}

void ImportAudioDialog::removeItem (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) items.size()))
        return;
    items.erase (items.begin() + index);
    selected = items.empty() ? -1 : juce::jmin (index, (int) items.size() - 1);
    list.selectRow (selected, false, true);
    refresh();
}

//==============================================================================
void ImportAudioDialog::chooseFiles()
{
    auto dir = juce::File (settings.getValue ("importFolder"));
    if (! dir.isDirectory())
        dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Import audio files onto the tape (Ctrl or Shift click for several)", dir, AudioImport::wildcard());
    juce::Component::SafePointer<ImportAudioDialog> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [self] (const juce::FileChooser& fc)
    {
        if (self != nullptr && ! fc.getResults().isEmpty())
            self->addFiles (fc.getResults());
    });
}

void ImportAudioDialog::addFiles (const juce::Array<juce::File>& files)
{
    problems.clear();
    int lastAdded = -1;
    for (auto& f : files)
    {
        if (! f.existsAsFile())
            continue;
        settings.setValue ("importFolder", f.getParentDirectory().getFullPathName());
        if (std::any_of (items.begin(), items.end(), [&] (const Item& i) { return i.file == f; }))
            continue;   // already on the list
        Item item { f, AudioImport::probe (f), -1, -1 };
        if (! item.info.ok)
        {
            problems.add (f.getFileName() + ": " + item.info.error);
            continue;
        }
        if (tracksUsed() + item.span() > numTracks)
        {
            problems.add (f.getFileName() + " left out: the 16 tracks are full (mono files take one, stereo files two).");
            continue;
        }
        items.push_back (item);
        const int index = (int) items.size() - 1;
        if (! autoPlace (index) && ! repackAll())
        {
            items.pop_back();   // can't happen within 16 tracks, but never leave a file without a place
            repackAll();
            problems.add (f.getFileName() + " left out: no room for it.");
            continue;
        }
        lastAdded = index;
    }
    list.updateContent();
    select (lastAdded >= 0 ? lastAdded : selected);
}

juce::int64 ImportAudioDialog::startSample() const
{
    auto& tape = engine.tape;
    switch (startBox.getSelectedId())
    {
        case currentPosition: return tape.getPosition();
        case inPoint:         return tape.getInPoint();
        default:              return 0;
    }
}

juce::int64 ImportAudioDialog::lengthOnTape (const Item& item) const
{
    if (! item.info.ok || item.info.sampleRate <= 0.0)
        return 0;
    return (juce::int64) std::ceil ((double) item.info.length * engine.tape.getSampleRate() / item.info.sampleRate);
}

juce::String ImportAudioDialog::formatTime (juce::int64 samples) const
{
    const double secs = (double) samples / engine.tape.getSampleRate();
    const int mins = (int) (secs / 60.0);
    return juce::String (mins) + ":" + juce::String (secs - mins * 60.0, 1).paddedLeft ('0', 4);
}

juce::String ImportAudioDialog::describe (const AudioImport::Info& info)
{
    return juce::String (info.channels == 1 ? "Mono" : info.channels == 2 ? "Stereo" : juce::String (info.channels) + " channels (first two)")
         + "   " + kHz (info.sampleRate) + " kHz   " + info.formatName.upToFirstOccurrenceOf (" ", false, false);
}

void ImportAudioDialog::refresh()
{
    list.updateContent();
    for (int r = 0; r < (int) items.size(); ++r)
        if (auto* row = dynamic_cast<Row*> (list.getComponentForRowNumber (r)))
            row->update (r, r == selected);
    picker.repaint();

    const int used = tracksUsed();
    filesTitle.setText ("Audio files   (" + juce::String (used) + " of " + juce::String (numTracks) + " tracks)", juce::dontSendNotification);
    addButton.setEnabled (used < numTracks);
    importButton.setEnabled (! items.empty());
    importButton.setButtonText (items.size() > 1 ? "Import " + juce::String ((int) items.size()) : juce::String ("Import"));

    // Warnings: what gets replaced, what gets left out.
    juce::StringArray lines (problems);
    if (items.empty() && problems.isEmpty())
        lines.add ("Add WAV, AIFF, FLAC, Ogg or MP3 files.");
    const auto start = startSample();
    juce::int64 longest = 0;
    juce::StringArray taken;
    for (auto& item : items)
    {
        longest = juce::jmax (longest, lengthOnTape (item));
        for (const int t : { item.first, item.second })
            if (item.on (t) && engine.tape.trackHasAudio (t))
                taken.add (juce::String (t + 1));
    }
    if (! taken.isEmpty())
        lines.add ((taken.size() == 1 ? "Track " + taken[0] + " already has audio. It is" : "Tracks " + taken.joinIntoString (", ") + " already have audio. They are")
                   + " replaced from " + formatTime (start) + " for the length of the file.");
    if (start + longest > engine.tape.getCapacity())
        lines.add ("The tape holds " + juce::String (Config::maxTapeMinutes) + " minutes: the end of a file will be cut off.");
    if (engine.tape.isPlaying() && ! items.empty())
        lines.add ("The tape stops for the import.");
    note.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

void ImportAudioDialog::startImport()
{
    if (items.empty())
        return;
    auto batch = std::make_shared<Batch>();
    batch->start = startSample();
    for (auto& item : items)
        if (item.first >= 0)
            batch->files.push_back ({ item.file, item.first, item.span() == 2 ? item.second : -1 });
    if (batch->files.empty())
        return;

    engine.tape.stop();
    auto* job = new ImportJob (engine, batch, 0);
    close();
    job->launchThread();
}

void ImportAudioDialog::close()
{
    if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
        w->exitModalState (0);
}
