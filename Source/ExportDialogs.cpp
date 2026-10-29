#include "ExportDialogs.h"
#include "Session.h"
#include "ConsoleLookAndFeel.h"
#include <cmath>

using namespace ConsoleColours;

namespace
{
    juce::String clockText (double seconds)
    {
        const int s = juce::jmax (0, (int) seconds);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    juce::String rangeText (const TapeMachine& tape)
    {
        const double sr = juce::jmax (1.0, tape.getSampleRate());
        return clockText ((double) tape.getInPoint() / sr) + " - " + clockText ((double) tape.getOutPoint() / sr);
    }

    /** The end of the song in tape samples: the end of the recording or of the last MIDI clip, whichever is later. */
    juce::int64 songEnd (AudioEngine& e)
    {
        const auto midiEnd = (juce::int64) std::ceil (e.tempo.samplesAt ((double) e.midi.songEnd(), e.tape.getSampleRate()));
        return juce::jmax (e.tape.getLength(), midiEnd);
    }

    void askToReplace (const juce::String& title, const juce::Array<juce::File>& files, std::function<void()> run)
    {
        juce::StringArray existing;
        for (auto& f : files)
            if (f.existsAsFile())
                existing.add (f.getFileName());
        if (existing.isEmpty())
            return run();
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle (title)
                           .withMessage ((existing.size() == 1 ? existing[0] + " is" : juce::String (existing.size()) + " of these files are")
                                         + " already in Bounced Audio. Replace " + (existing.size() == 1 ? "it?" : "them?"))
                           .withButton ("Replace").withButton ("Cancel");
        juce::AlertWindow::showAsync (options, [run] (int result) { if (result == 1) run(); });
    }

    //==========================================================================
    /** A bounce in progress: the song plays and the main mix goes to the file in real time. The small window shows
        how far it has got, with Finish (keep what is there) and Cancel (throw it away). It finishes by itself at the
        end; pressing Stop on the transport finishes it too. */
    class BounceRun : public juce::Component,
                      private juce::Timer
    {
    public:
        BounceRun (AudioEngine& e, juce::File f, juce::int64 startSample, juce::int64 endSample, double tailSeconds, double rate)
            : engine (e), file (std::move (f)), start (startSample), end (endSample), tail (tailSeconds), deviceRate (rate)
        {
            title.setText ("Bouncing " + file.getFileName(), juce::dontSendNotification);
            title.setFont (ConsoleLookAndFeel::uiFont (15.0f, true));
            title.setColour (juce::Label::textColourId, legend);
            status.setFont (ConsoleLookAndFeel::uiFont (13.0f));
            status.setColour (juce::Label::textColourId, legend.withAlpha (0.8f));
            hint.setText ("Mix as it plays: faders, mutes and plugins all go into the file. Stop on the transport or Finish ends it.",
                          juce::dontSendNotification);
            hint.setFont (ConsoleLookAndFeel::uiFont (12.0f));
            hint.setColour (juce::Label::textColourId, legend.withAlpha (0.6f));
            finishButton.onClick = [this] { finish (true); };
            cancelButton.onClick = [this] { finish (false); };
            for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &status, (juce::Component*) &hint,
                                        (juce::Component*) &bar, (juce::Component*) &finishButton, (juce::Component*) &cancelButton })
                addAndMakeVisible (c);
            setSize (460, 170);
        }

        ~BounceRun() override
        {
            if (running)
                finish (true, false);
        }

        bool begin (juce::String& error)
        {
            file.getParentDirectory().createDirectory();
            file.deleteFile();
            auto stream = file.createOutputStream();
            juce::WavAudioFormat wav;
            auto* writer = stream != nullptr ? wav.createWriterFor (stream.get(), deviceRate, 2, 24, {}, 0) : nullptr;
            if (writer == nullptr)
            {
                error = "Could not write " + file.getFullPathName();
                return false;
            }
            stream.release();
            writerThread.startThread();
            threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, writerThread, 1 << 18);

            auto& tape = engine.tape;
            tape.stop();
            wasLoop = tape.loopOn.load();
            tape.loopOn = false;
            tape.setRecordEnabled (false);   // a bounce never records on the tape
            tape.locate (start);

            // Output samples to write: the song at the tape's playback rate, the plugin delay (left out of the file's
            // start) and the tail for reverbs to ring out.
            const double perTapeSample = deviceRate / juce::jmax (1.0, tape.getSampleRate()) / juce::jmax (0.01, tape.getPlaybackRate());
            const auto skip = (juce::int64) engine.getPluginLatency();
            total = (juce::int64) std::ceil ((double) (end - start) * perTapeSample) + (juce::int64) (tail * deviceRate);
            engine.startBounce (threaded.get(), skip, total);
            tape.play();
            running = true;
            startedAt = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (10);
            return true;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 12);
            title.setBounds (r.removeFromTop (22));
            status.setBounds (r.removeFromTop (20));
            r.removeFromTop (6);
            bar.setBounds (r.removeFromTop (14));
            r.removeFromTop (6);
            hint.setBounds (r.removeFromTop (34));
            auto buttons = r.removeFromBottom (28);
            cancelButton.setBounds (buttons.removeFromRight (90));
            buttons.removeFromRight (8);
            finishButton.setBounds (buttons.removeFromRight (90));
        }

        void paint (juce::Graphics& g) override { g.fillAll (room); }

        std::function<void()> onClosed;

    private:
        AudioEngine& engine;
        juce::File file;
        juce::int64 start, end, total = 0;
        double tail, deviceRate, startedAt = 0.0, progress = 0.0;
        bool wasLoop = false, running = false;
        juce::TimeSliceThread writerThread { "Bounce writer" };
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
        juce::Label title, status, hint;
        juce::ProgressBar bar { progress };
        juce::TextButton finishButton { "Finish" }, cancelButton { "Cancel" };

        void timerCallback() override
        {
            const double done = (double) engine.getBounced() / deviceRate, of = (double) total / deviceRate;
            progress = juce::jlimit (0.0, 1.0, of > 0.0 ? done / of : 0.0);
            status.setText (clockText (done) + " of " + clockText (of), juce::dontSendNotification);

            if (engine.isBounceDone())
                return finish (true);
            // Stop pressed before the end: the bounce ends there. (At the end of the song the tape stops by itself
            // and the tail is still being written.)
            auto& tape = engine.tape;
            const bool early = tape.getPosition() < end - (juce::int64) (0.1 * tape.getSampleRate());
            if (! tape.isPlaying() && early && juce::Time::getMillisecondCounterHiRes() - startedAt > 500.0)
                finish (true);
        }

        void finish (bool keep, bool tell = true)
        {
            if (! running)
                return;
            running = false;
            stopTimer();
            engine.stopBounce();
            threaded = nullptr;   // writes what is left and closes the file
            writerThread.stopThread (2000);
            auto& tape = engine.tape;
            tape.stop();
            tape.loopOn = wasLoop;
            tape.locate (start);
            if (! keep)
                file.deleteFile();
            if (tell)
            {
                if (keep)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Bounce Song", "The bounce was saved to\n" + file.getFullPathName());
                if (onClosed)
                    juce::MessageManager::callAsync (onClosed);
            }
        }
    };

    std::unique_ptr<juce::DocumentWindow> bounceWindow;

    /** The bounce windows (setup, then the run): the close button, where there is one, cancels. */
    struct BounceWindow : juce::DocumentWindow
    {
        using juce::DocumentWindow::DocumentWindow;
        void closeButtonPressed() override { juce::MessageManager::callAsync ([] { bounceWindow = nullptr; }); }
    };

    void showInWindow (juce::Component* content, const juce::String& title, int buttons)
    {
        bounceWindow = std::make_unique<BounceWindow> (title, room, buttons);
        bounceWindow->setUsingNativeTitleBar (true);
        bounceWindow->setContentOwned (content, true);
        bounceWindow->setAlwaysOnTop (true);
        bounceWindow->centreWithSize (bounceWindow->getWidth(), bounceWindow->getHeight());
        bounceWindow->setVisible (true);
    }

    void startBounce (AudioEngine& engine, juce::File file, juce::int64 start, juce::int64 end, double tail, double rate)
    {
        auto run = std::make_unique<BounceRun> (engine, file, start, end, tail, rate);
        juce::String error;
        if (! run->begin (error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Bounce Song", error);
            return;
        }
        run->onClosed = [] { bounceWindow = nullptr; };
        showInWindow (run.release(), "Bounce Song", juce::DocumentWindow::minimiseButton);
    }

    //==========================================================================
    /** Before a bounce: the range, its own Set In / Set Out (the tape's In and Out, as on the console, from where the
        tape is now) and the file name. A window that stays open, so the tape can be moved to the points meanwhile. */
    class BounceSetup : public juce::Component,
                        private juce::Timer
    {
    public:
        BounceSetup (AudioEngine& e, juce::File f, juce::PropertiesFile& s, double rate)
            : engine (e), folder (std::move (f)), settings (s), deviceRate (rate)
        {
            intro.setText ("Plays the song in real time and records the main mix (after the limiter, without the click) to a "
                           "24-bit WAV in\n" + folder.getFullPathName() + "\n\nMix as it plays.", juce::dontSendNotification);
            intro.setFont (ConsoleLookAndFeel::uiFont (13.0f));
            intro.setJustificationType (juce::Justification::topLeft);
            for (auto* l : { &intro, &rangeLabel, &inLabel, &outLabel, &nameLabel })
                l->setColour (juce::Label::textColourId, legend);
            for (auto* l : { &inTime, &outTime })
            {
                l->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
                l->setColour (juce::Label::textColourId, juce::Colour (0xffff5a45));
                l->setColour (juce::Label::backgroundColourId, juce::Colour (0xff0b0c0d));
                l->setJustificationType (juce::Justification::centred);
            }
            rangeLabel.setText ("Range", juce::dontSendNotification);
            inLabel.setText ("In", juce::dontSendNotification);
            outLabel.setText ("Out", juce::dontSendNotification);
            nameLabel.setText ("File name", juce::dontSendNotification);

            range.addItem ("Whole song (with a 3 s tail)", 1);
            range.addItem ("In to Out", 2);
            range.setSelectedId (engine.tape.hasRegion() && settings.getValue ("bounceRange") == "inOut" ? 2 : 1, juce::dontSendNotification);
            setIn.setTooltip ("Set the In point where the tape is now");
            setOut.setTooltip ("Set the Out point where the tape is now");
            setIn.onClick  = [this] { engine.tape.setInPoint (engine.tape.getPosition()); pointsSet(); };
            setOut.onClick = [this] { engine.tape.setOutPoint (engine.tape.getPosition()); pointsSet(); };
            name.setText (juce::File::createLegalFileName (engine.projectName.toString()), false);
            name.onReturnKey = [this] { go(); };
            startButton.onClick = [this] { go(); };
            cancelButton.onClick = [] { juce::MessageManager::callAsync ([] { bounceWindow = nullptr; }); };

            for (juce::Component* c : { (juce::Component*) &intro, (juce::Component*) &rangeLabel, (juce::Component*) &range,
                                        (juce::Component*) &inLabel, (juce::Component*) &inTime, (juce::Component*) &setIn,
                                        (juce::Component*) &outLabel, (juce::Component*) &outTime, (juce::Component*) &setOut,
                                        (juce::Component*) &nameLabel, (juce::Component*) &name,
                                        (juce::Component*) &startButton, (juce::Component*) &cancelButton })
                addAndMakeVisible (c);
            timerCallback();
            startTimerHz (10);
            setSize (480, 330);
        }

        void paint (juce::Graphics& g) override { g.fillAll (room); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 12);
            intro.setBounds (r.removeFromTop (92));
            r.removeFromTop (6);
            const auto row = [&r] { auto x = r.removeFromTop (28); r.removeFromTop (8); return x; };
            auto x = row();
            rangeLabel.setBounds (x.removeFromLeft (70));
            range.setBounds (x);
            for (auto [label, time, button] : { std::tuple { &inLabel, &inTime, &setIn }, std::tuple { &outLabel, &outTime, &setOut } })
            {
                x = row();
                label->setBounds (x.removeFromLeft (70));
                time->setBounds (x.removeFromLeft (110));
                x.removeFromLeft (8);
                button->setBounds (x.removeFromLeft (90));
            }
            x = row();
            nameLabel.setBounds (x.removeFromLeft (70));
            name.setBounds (x);
            auto buttons = r.removeFromBottom (28);
            cancelButton.setBounds (buttons.removeFromRight (90));
            buttons.removeFromRight (8);
            startButton.setBounds (buttons.removeFromRight (90));
        }

    private:
        AudioEngine& engine;
        juce::File folder;
        juce::PropertiesFile& settings;
        double deviceRate;
        juce::Label intro, rangeLabel, inLabel, outLabel, inTime, outTime, nameLabel;
        juce::ComboBox range;
        juce::TextButton setIn { "Set In" }, setOut { "Set Out" }, startButton { "Start" }, cancelButton { "Cancel" };
        juce::TextEditor name;

        /** m:ss.t (the In and Out points to a tenth). */
        static juce::String pointText (juce::int64 samples, double sr)
        {
            const double s = (double) samples / juce::jmax (1.0, sr);
            return clockText (s) + "." + juce::String ((int) (s * 10.0) % 10);
        }

        void pointsSet()
        {
            if (engine.tape.hasRegion())
                range.setSelectedId (2, juce::dontSendNotification);   // marking a range means bouncing it
            timerCallback();
        }

        void timerCallback() override   // In and Out can change on the console too
        {
            auto& tape = engine.tape;
            const double sr = tape.getSampleRate();
            inTime.setText (pointText (tape.getInPoint(), sr), juce::dontSendNotification);
            outTime.setText (pointText (tape.getOutPoint(), sr), juce::dontSendNotification);
            const bool region = tape.hasRegion();
            const auto text = region ? "In to Out (" + rangeText (tape) + ")" : juce::String ("In to Out (Out must be after In)");
            if (range.getItemText (1) != text)
                range.changeItemText (2, text);
            range.setItemEnabled (2, region);
            if (! region && range.getSelectedId() == 2)
                range.setSelectedId (1, juce::dontSendNotification);
        }

        void go()
        {
            auto& t = engine.tape;
            const bool inOut = range.getSelectedId() == 2 && t.hasRegion();
            settings.setValue ("bounceRange", inOut ? "inOut" : "whole");
            auto fileName = juce::File::createLegalFileName (name.getText().trim());
            if (fileName.isEmpty())
                fileName = "Bounce";
            const auto file = folder.getChildFile (fileName.endsWithIgnoreCase (".wav") ? fileName : fileName + ".wav");
            const auto start = inOut ? t.getInPoint() : (juce::int64) 0;
            const auto end = inOut ? t.getOutPoint() : songEnd (engine);
            const double tail = inOut ? 0.0 : 3.0;
            auto& e = engine;
            const double rate = deviceRate;
            askToReplace ("Bounce Song", { file }, [&e, file, start, end, tail, rate]
            {
                juce::MessageManager::callAsync ([&e, file, start, end, tail, rate]
                {
                    bounceWindow = nullptr;   // the setup window (and this panel)
                    startBounce (e, file, start, end, tail, rate);
                });
            });
        }
    };

    //==========================================================================
    /** Writes the chosen tape tracks on a worker thread (the audio keeps running). */
    class ExportTapeJob : public juce::ThreadWithProgressWindow
    {
    public:
        ExportTapeJob (AudioEngine& e, juce::File f, juce::String p, juce::Array<int> t, juce::int64 s, juce::int64 en)
            : juce::ThreadWithProgressWindow ("Export From Tape", true, true), engine (e), folder (std::move (f)), prefix (std::move (p)),
              tracks (std::move (t)), start (s), end (en)
        {
            setStatusMessage ("Writing " + juce::String (tracks.size()) + " track" + (tracks.size() == 1 ? "" : "s") + " off the tape...");
        }

        void run() override
        {
            result = Session::exportTapeTracks (engine, folder, prefix, tracks, start, end, [this] (double p)
            {
                setProgress (p);
                return ! threadShouldExit();
            }, &written);
        }

        void threadComplete (bool) override
        {
            if (result.ok)
            {
                juce::StringArray names;
                for (auto& f : written)
                    names.add (f.getFileName());
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export From Tape",
                                                        "Saved in " + folder.getFullPathName() + ":\n\n" + names.joinIntoString ("\n"));
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export From Tape", result.error);
            delete this;
        }

    private:
        AudioEngine& engine;
        juce::File folder;
        juce::String prefix;
        juce::Array<int> tracks;
        juce::int64 start, end;
        Session::Result result;
        juce::Array<juce::File> written;
    };

    /** Which tape tracks, the range and the file names. */
    class ExportTapePanel : public juce::Component
    {
    public:
        ExportTapePanel (AudioEngine& e, juce::File f, juce::PropertiesFile& s) : engine (e), folder (std::move (f)), settings (s)
        {
            auto& tape = engine.tape;
            for (int t = 0; t < TapeMachine::numTracks; ++t)
            {
                auto* b = trackButtons.add (new juce::ToggleButton (juce::String (t + 1)));
                const bool audio = tape.trackHasAudio (t);
                b->setEnabled (audio);
                b->setToggleState (audio, juce::dontSendNotification);
                b->setTooltip (audio ? "Tape track " + juce::String (t + 1) + " (" + engine.returnStrips[t]->name + ")" : "Tape track " + juce::String (t + 1) + " is blank");
                b->setColour (juce::ToggleButton::textColourId, legend);
                b->onClick = [this] { updateButtons(); };
                addAndMakeVisible (b);
            }
            all.onClick = [this] { for (auto* b : trackButtons) if (b->isEnabled()) b->setToggleState (true, juce::dontSendNotification); updateButtons(); };
            none.onClick = [this] { for (auto* b : trackButtons) b->setToggleState (false, juce::dontSendNotification); updateButtons(); };

            const bool region = tape.hasRegion();
            range.addItem ("Whole tape", 1);
            range.addItem (region ? "In to Out (" + rangeText (tape) + ")" : juce::String ("In to Out (set In and Out first)"), 2);
            range.setItemEnabled (2, region);
            range.setSelectedId (region && settings.getValue ("exportTapeRange") == "inOut" ? 2 : 1, juce::dontSendNotification);
            range.onChange = [this] { updateButtons(); };

            prefix.setText (juce::File::createLegalFileName (engine.projectName.toString()), false);
            prefix.onTextChange = [this] { updateButtons(); };

            exportButton.onClick = [this] { go(); };
            cancelButton.onClick = [this] { close(); };
            for (juce::Component* c : { (juce::Component*) &all, (juce::Component*) &none, (juce::Component*) &range, (juce::Component*) &prefix,
                                        (juce::Component*) &exportButton, (juce::Component*) &cancelButton, (juce::Component*) &example })
                addAndMakeVisible (c);
            example.setFont (ConsoleLookAndFeel::uiFont (12.0f));
            example.setColour (juce::Label::textColourId, legend.withAlpha (0.7f));
            setSize (520, 300);
            updateButtons();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 12);
            tracksArea = r.removeFromTop (20);
            for (int row = 0; row < 2; ++row)
            {
                auto line = r.removeFromTop (28);
                const int w = line.getWidth() / 8;
                for (int c = 0; c < 8; ++c)
                    trackButtons[row * 8 + c]->setBounds (line.removeFromLeft (w));
            }
            auto quick = r.removeFromTop (28);
            all.setBounds (quick.removeFromLeft (70).reduced (0, 2));
            quick.removeFromLeft (6);
            none.setBounds (quick.removeFromLeft (70).reduced (0, 2));
            r.removeFromTop (10);
            rangeLabel = r.removeFromTop (18);
            range.setBounds (r.removeFromTop (26));
            r.removeFromTop (8);
            nameLabel = r.removeFromTop (18);
            prefix.setBounds (r.removeFromTop (26));
            example.setBounds (r.removeFromTop (20));
            auto buttons = r.removeFromBottom (28);
            cancelButton.setBounds (buttons.removeFromRight (90));
            buttons.removeFromRight (8);
            exportButton.setBounds (buttons.removeFromRight (90));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (room);
            g.setColour (legend);
            g.setFont (ConsoleLookAndFeel::uiFont (13.0f, true));
            g.drawText ("Tape tracks (blank ones are greyed out)", tracksArea, juce::Justification::centredLeft, false);
            g.drawText ("Range", rangeLabel, juce::Justification::centredLeft, false);
            g.drawText ("File names start with", nameLabel, juce::Justification::centredLeft, false);
        }

    private:
        AudioEngine& engine;
        juce::File folder;
        juce::PropertiesFile& settings;
        juce::OwnedArray<juce::ToggleButton> trackButtons;
        juce::TextButton all { "All" }, none { "None" }, exportButton { "Export" }, cancelButton { "Cancel" };
        juce::ComboBox range;
        juce::TextEditor prefix;
        juce::Label example;
        juce::Rectangle<int> tracksArea, rangeLabel, nameLabel;

        juce::Array<int> chosen() const
        {
            juce::Array<int> t;
            for (int i = 0; i < trackButtons.size(); ++i)
                if (trackButtons[i]->isEnabled() && trackButtons[i]->getToggleState())
                    t.add (i);
            return t;
        }

        void updateButtons()
        {
            const auto t = chosen();
            exportButton.setEnabled (! t.isEmpty());
            example.setText (t.isEmpty() ? juce::String ("Choose at least one track.")
                                         : "One mono 24-bit WAV each, e.g. " + Session::tapeExportName (engine, prefix.getText(), t.getFirst()),
                             juce::dontSendNotification);
        }

        void close()
        {
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState (0);
        }

        void go()
        {
            auto& tape = engine.tape;
            const bool inOut = range.getSelectedId() == 2 && tape.hasRegion();
            settings.setValue ("exportTapeRange", inOut ? "inOut" : "whole");
            const auto tracks = chosen();
            const auto start = inOut ? tape.getInPoint() : (juce::int64) 0;
            const auto end = inOut ? tape.getOutPoint() : tape.getLength();
            juce::Array<juce::File> files;
            for (int t : tracks)
                files.add (folder.getChildFile (Session::tapeExportName (engine, prefix.getText(), t)));
            auto& e = engine;
            const auto dir = folder;
            const auto p = prefix.getText();
            close();
            askToReplace ("Export From Tape", files, [&e, dir, p, tracks, start, end]
            {
                (new ExportTapeJob (e, dir, p, tracks, start, end))->launchThread();
            });
        }
    };
}

//==============================================================================
void ExportDialogs::bounceSong (AudioEngine& engine, const juce::File& folder, juce::PropertiesFile& settings)
{
    if (bounceWindow != nullptr)
    {
        bounceWindow->toFront (true);
        return;
    }
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Bounce Song",
                                                "Audio is off. A bounce plays the song in real time, so switch the audio interface on first (Power).");
        return;
    }
    if (songEnd (engine) <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Bounce Song", "There is nothing on the tape or the MIDI page yet.");
        return;
    }

    showInWindow (new BounceSetup (engine, folder, settings, device->getCurrentSampleRate()), "Bounce Song",
                  juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton);
}

void ExportDialogs::exportFromTape (AudioEngine& engine, const juce::File& folder, juce::PropertiesFile& settings)
{
    bool any = false;
    for (int t = 0; t < TapeMachine::numTracks; ++t)
        any = any || engine.tape.trackHasAudio (t);
    if (! any)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export From Tape", "The tape is blank: record something first.");
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ExportTapePanel (engine, folder, settings));
    o.dialogTitle = "Export From Tape";
    o.dialogBackgroundColour = room;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
