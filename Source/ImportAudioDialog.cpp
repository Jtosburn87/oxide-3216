#include "ImportAudioDialog.h"
#include "ConsoleLookAndFeel.h"

using namespace ConsoleColours;

namespace
{
    /** Decodes the file on a worker thread, then writes it to tape on the message thread. */
    class ImportJob : public juce::ThreadWithProgressWindow
    {
    public:
        ImportJob (AudioEngine& e, juce::File f, int track, juce::int64 at)
            : juce::ThreadWithProgressWindow ("Importing " + f.getFileName(), true, true),
              engine (e), file (std::move (f)), firstTrack (track), start (at),
              rate (e.tape.getSampleRate()), capacity (e.tape.getCapacity() - at)
        {
            setStatusMessage ("Reading the file onto the tape...");
        }

        void run() override
        {
            audio = AudioImport::read (file, rate, capacity, [this] (double p)
            {
                setProgress (p);
                return ! threadShouldExit();
            });
        }

        void threadComplete (bool cancelled) override
        {
            if (audio.ok && ! cancelled)
            {
                engine.tape.stop();
                AudioImport::place (engine.tape, audio, firstTrack, start);
                if (audio.truncated)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                            "Only the part of the file that fits on the tape ("
                                                            + juce::String (Config::maxTapeMinutes) + " minutes) was imported.");
            }
            else if (! cancelled && audio.error.isNotEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import", audio.error);
            }
            delete this;
        }

    private:
        AudioEngine& engine;
        juce::File file;
        int firstTrack;
        juce::int64 start;
        double rate;
        juce::int64 capacity;
        AudioImport::Audio audio;
    };

    constexpr int numTracks = Config::numTracks;

    juce::String kHz (double rate)   // 44.1, 48
    {
        const auto k = rate / 1000.0;
        return std::abs (k - std::round (k)) < 0.05 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1);
    }
}

juce::Component::SafePointer<juce::DialogWindow> ImportAudioDialog::window;

//==============================================================================
ImportAudioDialog::TrackPicker::TrackPicker()
{
    setWantsKeyboardFocus (true);
    setTitle ("Tape track");
}

void ImportAudioDialog::TrackPicker::setSpan (int n)
{
    span = juce::jlimit (1, 2, n);
    setFirst (first);
}

void ImportAudioDialog::TrackPicker::setFirst (int t)
{
    t = juce::jlimit (0, numTracks - span, t);
    if (t == first)
    {
        repaint();
        return;
    }
    first = t;
    repaint();
    if (onChange)
        onChange();
}

void ImportAudioDialog::TrackPicker::pickAt (int x)
{
    const auto cell = (float) getWidth() / (float) numTracks;
    auto t = (int) ((float) x / cell);
    // A stereo pick puts the left channel on the cell clicked, unless that is track 16.
    setFirst (t);
}

void ImportAudioDialog::TrackPicker::mouseDown (const juce::MouseEvent& e) { grabKeyboardFocus(); pickAt (e.x); }
void ImportAudioDialog::TrackPicker::mouseDrag (const juce::MouseEvent& e) { pickAt (e.x); }

bool ImportAudioDialog::TrackPicker::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::leftKey)  { setFirst (first - 1); return true; }
    if (k == juce::KeyPress::rightKey) { setFirst (first + 1); return true; }
    return false;
}

void ImportAudioDialog::TrackPicker::paint (juce::Graphics& g)
{
    const auto cell = (float) getWidth() / (float) numTracks;
    const auto h = (float) getHeight();
    for (int t = 0; t < numTracks; ++t)
    {
        const auto r = juce::Rectangle<float> (cell * (float) t, 0.0f, cell, h).reduced (2.0f);
        const bool picked = t >= first && t < first + span;

        g.setColour (picked ? amber : plate);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (picked ? amber.brighter (0.3f) : edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        g.setColour (picked ? juce::Colours::black : legend);
        g.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
        g.drawText (juce::String (t + 1), r.withTrimmedBottom (h * 0.35f), juce::Justification::centred);

        // What lands where: L / R for stereo; a bar for audio already on the track.
        if (picked && span == 2)
        {
            g.setFont (ConsoleLookAndFeel::uiFont (12.0f));
            g.drawText (t == first ? "L" : "R", r.withTrimmedTop (h * 0.5f).withTrimmedBottom (4.0f), juce::Justification::centred);
        }
        else if (hasAudio && hasAudio (t))
        {
            g.setColour (picked ? juce::Colours::black.withAlpha (0.6f) : legend.withAlpha (0.55f));
            g.fillRoundedRectangle (r.withTrimmedTop (h * 0.72f).reduced (5.0f, 1.0f).withHeight (3.0f), 1.5f);
        }
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (focus);
        g.drawRect (juce::Rectangle<float> (cell * (float) first, 0.0f, cell * (float) span, h), 1.5f);
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
    styleLabel (fileTitle, "Audio file", 14.0f, legend.withAlpha (0.7f));
    styleLabel (fileName, "No file chosen", 16.0f, legend);
    styleLabel (infoLabel, {}, 14.0f, bridgeText);
    styleLabel (tracksTitle, "Place on track", 14.0f, legend.withAlpha (0.7f));
    styleLabel (startTitle, "Start at", 14.0f, legend.withAlpha (0.7f));
    styleLabel (note, {}, 14.0f, amber);
    note.setJustificationType (juce::Justification::topLeft);
    fileName.setMinimumHorizontalScale (0.6f);

    chooseButton.onClick = [this] { chooseFile(); };
    importButton.onClick = [this] { startImport(); };
    cancelButton.onClick = [this] { close(); };
    for (auto* b : { &chooseButton, &importButton, &cancelButton })
        addAndMakeVisible (b);

    picker.hasAudio = [this] (int t) { return engine.tape.trackHasAudio (t); };
    picker.onChange = [this] { refresh(); };
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

    setSize (640, 330);
    refresh();
}

ImportAudioDialog::~ImportAudioDialog() = default;

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
    content->chooseFile();   // straight to the file browser
}

void ImportAudioDialog::paint (juce::Graphics& g)
{
    g.fillAll (room);
}

void ImportAudioDialog::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    fileTitle.setBounds (area.removeFromTop (20));
    auto row = area.removeFromTop (32);
    chooseButton.setBounds (row.removeFromRight (130));
    fileName.setBounds (row.withTrimmedRight (8));
    infoLabel.setBounds (area.removeFromTop (22));

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
void ImportAudioDialog::chooseFile()
{
    auto dir = juce::File (settings.getValue ("importFolder"));
    if (! dir.isDirectory())
        dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Import an audio file onto the tape", dir, AudioImport::wildcard());
    juce::Component::SafePointer<ImportAudioDialog> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (self != nullptr && f.existsAsFile())
            self->setFile (f);
    });
}

void ImportAudioDialog::setFile (const juce::File& f)
{
    file = f;
    info = AudioImport::probe (f);
    settings.setValue ("importFolder", f.getParentDirectory().getFullPathName());
    fileName.setText (f.getFileName(), juce::dontSendNotification);

    // Prefer the first free track (or pair) for the new audio.
    picker.setSpan (info.ok ? info.tracksNeeded() : 1);
    for (int t = 0; t + picker.getSpan() <= numTracks; ++t)
    {
        bool free = true;
        for (int k = 0; k < picker.getSpan(); ++k)
            free = free && ! engine.tape.trackHasAudio (t + k);
        if (free)
        {
            picker.setFirst (t);
            break;
        }
    }
    refresh();
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

juce::int64 ImportAudioDialog::lengthOnTape() const
{
    if (! info.ok)
        return 0;
    return (juce::int64) std::ceil ((double) info.length * engine.tape.getSampleRate() / info.sampleRate);
}

juce::String ImportAudioDialog::formatTime (juce::int64 samples) const
{
    const double secs = (double) samples / engine.tape.getSampleRate();
    const int mins = (int) (secs / 60.0);
    return juce::String (mins) + ":" + juce::String (secs - mins * 60.0, 1).paddedLeft ('0', 4);
}

void ImportAudioDialog::refresh()
{
    const int w = picker.getSpan();
    const int first = picker.getFirst();
    tracksTitle.setText (w == 2 ? "Place on tracks (stereo: left on the first, right on the next)" : "Place on track",
                         juce::dontSendNotification);
    importButton.setEnabled (info.ok);

    if (! info.ok)
    {
        infoLabel.setText (file.existsAsFile() ? info.error : "Choose a WAV, AIFF, FLAC, Ogg or MP3 file.", juce::dontSendNotification);
        note.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto tapeRate = engine.tape.getSampleRate();
    auto text = juce::String (info.channels == 1 ? "Mono" : info.channels == 2 ? "Stereo" : juce::String (info.channels) + " channels")
              + "   " + kHz (info.sampleRate) + " kHz"
              + "   " + formatTime (lengthOnTape())
              + "   " + info.formatName.upToFirstOccurrenceOf (" ", false, false);
    if (std::abs (info.sampleRate - tapeRate) > 0.01)
        text << "   (converted to " << kHz (tapeRate) << " kHz)";
    infoLabel.setText (text, juce::dontSendNotification);

    // Warnings: what gets replaced, what gets left out.
    juce::StringArray lines;
    if (info.channels > 2)
        lines.add ("The file has " + juce::String (info.channels) + " channels: only the first two are imported.");

    const auto start = startSample(), length = lengthOnTape();
    juce::StringArray taken;
    for (int t = first; t < first + w; ++t)
        if (engine.tape.trackHasAudio (t))
            taken.add (juce::String (t + 1));
    if (! taken.isEmpty())
        lines.add ((taken.size() == 1 ? "Track " + taken[0] + " already has audio. It is" : "Tracks " + taken.joinIntoString (" and ") + " already have audio. They are")
                   + " replaced from " + formatTime (start) + " to " + formatTime (start + length) + ".");

    if (start + length > engine.tape.getCapacity())
        lines.add ("The tape holds " + juce::String (Config::maxTapeMinutes) + " minutes: the end of the file will be cut off.");
    if (engine.tape.isPlaying())
        lines.add ("The tape stops for the import.");

    note.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

void ImportAudioDialog::startImport()
{
    if (! info.ok)
        return;

    engine.tape.stop();
    auto* job = new ImportJob (engine, file, picker.getFirst(), startSample());
    close();
    job->launchThread();
}

void ImportAudioDialog::close()
{
    if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
        w->exitModalState (0);
}
