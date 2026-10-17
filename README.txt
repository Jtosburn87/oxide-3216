OXIDE-3216
==========

A 16-track tape machine fed by a 32-channel console, for Windows 10/11 (64-bit). A macOS
version (11 Big Sur or later, Apple silicon and Intel) and a Linux version are being tested: see
"On a Mac" and "On Linux" below. The Credits button in the top bar says who made it and how to get in touch.


RUNNING IT
----------
Unzip anywhere and run Oxide-3216.exe. Nothing to install: it needs no extra runtime.

First time:
  1. Click "Audio device" and pick your interface's ASIO driver, sample rate and buffer size.
     Choose the main outputs there (aux and sub outputs are optional). Interface inputs are
     patched to the input channels in the patch bay ("Interface 1 to 1" does the usual setup).
  2. Click "Options" > "Plugins", then Options > "Scan for new or updated VST3 plug-ins" (the standard folder
     is C:\Program Files\Common Files\VST3; add others in the scan dialog). Each plugin is scanned
     in its own process, so a bad plugin cannot crash the console. Failed ones are shown in red.

On a Mac (in testing): unzip and drag Oxide-3216.app to Applications. It isn't signed by a
registered Apple developer yet, so the first time, right-click it and choose Open, then Open again
(on macOS 15: try to open it once, then System Settings > Privacy & Security > Open Anyway).
Allow microphone access when asked: macOS calls every audio interface input a "microphone", and
without it every input is silent. Audio device lists your Core Audio interfaces. The plugin scan
finds VST3 plugins and Audio Units. Wherever this README says Ctrl, use Cmd.

On Linux (in testing; built on Ubuntu 22.04, x64): untar anywhere and run ./Oxide-3216. It uses
ALSA or JACK (PipeWire serves JACK clients too). It needs the usual desktop libraries (ALSA,
FreeType, Fontconfig, X11), which any desktop distro already has.


THE CONSOLE
-----------
Left bank: 16 input channels. Right bank: 16 tape-return channels. Each strip's fader has a column
beside it (after the reference console): Mute, Main and S1-S4 assigns, the channel meter, and Solo with
its LED. Pan sits at the foot of the knob area.
 Centre: tape machine,
group EQs, Tempo, inserts and aux returns, bus faders and the main fader. The stereo bus
compressor has a window of its own: the SBC button above the Master insert slots opens it (SBC lights
while the compressor is in; the GR bar under the slots shows its gain reduction).
The console stretches to fill the window (the centre section on its own keeps its shape). The Zoom
slider zooms in evenly, with scroll bars; resizing the window fits it again.
The small "-" on the EQ, Gate, Comp and Aux sends titles minimises that section on
the whole bank ("+" brings it back); the bank gets shorter and its faders stay in line.
The "In" button at the left of the EQ, Gate and Comp titles switches that section in or out.
The "Meters In/Out" button at the top of the first strip's fader section switches that bank's meters between the input (after the trim) and
the output (after the fader).
"Options" opens the input bank or return bank in a window of its own as well (for a second
monitor): a copy, so the console keeps its Record / Mix / Full view; both move together. The centre
section moves into its window instead; close it, or press "Put back", to return it. Windows that
are out stay out, in the same place, the next time Oxide-3216 starts.
"Options" > "Unlock chain order" adds a small up arrow to the EQ, gate and compressor titles of
every channel: it moves that section earlier in the channel's signal chain, and the strip shows
the sections in the order the signal passes through them. The order is saved with the session.
On the return strips, the button on the gate's title line switches it to a de-esser: Freq picks the
sibilance, Reduce is the most it turns down (dB). Wide: everything above Freq (off: a band around
it). Gentle: only the sibilant band comes down (off: the whole signal does). Byp bypasses it.

Every strip: phase (Ø), gain (drives the preamp), trim (after the preamp), gain/trim link
(the chain button between them, on by default: turning the gain turns the trim the other way,
so you drive the preamp harder or cleaner at the same level), high-pass filter, 4-band EQ, gate (Fast atk = fast attack with the pop),
compressor, 8 aux sends (P = pre-fader), sub assigns S1-S4, main assign, pan,
mute, solo, fader. Double-click a scribble strip to rename it.

THE CHANNEL WINDOW: double-click any knob on a channel strip to open that channel big, laid out
like a channel-strip plugin (Input, Equaliser, Gate / Compressor, Aux sends, Output). Everything in
it is the console's own control. The arrows either side of the name step through the channels of
the same bank (16 goes round to 1). It also has controls the strip doesn't:
  EQ: LF and HF "Bell" (off = shelf), Q for LMF and HMF.
  Gate: Release, and a side chain (In, HPF / LPF / BPF, Freq, Q) that filters what the gate
  listens to, not the audio.
  Compressor: the type buttons along the bottom, and the strip shows that type's controls too.
    60  VCA, after the dbx 160: Threshold, Ratio (to infinity), Output, OverEasy (soft knee).
    76  FET, after the 1176: Input (drives it), Output, Attack, Release, ratio 4/8/12/20, All.
    LA  Opto, after the LA-2A: Peak Reduction, Gain, Comp / Limit.
    G   After the SSL G-series channel compressor: Threshold, Ratio, Release, Fast attack;
        the output is made up automatically.
  A new channel starts on 60; sessions from before the types open on 76, as they were made.

Knobs and faders: drag up/down, mouse wheel, Ctrl+click (Cmd+click on a Mac) for the default (double-click too, except on
channel strip knobs, where it opens the channel window); with the keyboard, arrows step, Page Up/Down
step more, Home or Delete resets.

Group EQ: four fixed bands over each sub fader (gain only): HF shelf 8 kHz, HMF 3 kHz, LMF 500 Hz,
LF shelf 100 Hz.

Talkback (Monitor / TB box): the XLR socket is the talkback mic in. Right-click it to choose the
interface input your mic is on; that patches it into TB In on the patch bay. Hold Talk to talk,
double-click it to latch. Setup chooses which auxes (1-8) it goes to and sets the TB limiter. The
monitors dim while you talk.

Scenes (Scene box, under the Console buttons): 99 snapshots of every fader and knob. Switches,
names, plugins, the patch bay and the tape machine are not part of a scene. The red readout shows
the selected scene; it flashes until you press GO to recall it, and the amber dot lights when a
fader or knob has moved since. The arrows pick the previous / next stored scene. Right-click the
number for Scene Memory: scenes 01-99 with titles, Go, Store (asks before replacing a scene),
Prev / Next, and Fade (Instant up to 5 s: recalls glide there; grab a control mid-fade and it's
yours). Scenes and the fade time are saved with the session.

Tempo (top right of the centre section): the tempo (drag up / down, Shift for fine, mouse wheel, or
double-click to type 20-300), the time signature (click for a menu), Tap (tap the beat four times or
more), the song position in bar | beat | tick (bar 1 is the start of the tape; 960 ticks a quarter note), and
the Click with its level and Setup: the click's own strip with level, pan, a four-band EQ, sends
to Aux 1-8 for the headphone mixes (they join after the aux returns, like the talkback) and the Main send on / off. The click sounds on every beat while the tape plays, higher on the first
beat of the bar, on the main and monitor outs only: it is never recorded or exported. The tempo is
saved with the session and is not part of scenes.
Tempo map: Session > Import tempo map... (or Tempo map on the MIDI page) reads the tempo and time
signature changes from a MIDI file. The click, the bars, the grid and the MIDI follow them from bar 1;
audio on tape does not move. While a map is in, the Tempo box shows the tempo and signature where the
tape is (the legend turns amber) and can't be changed by hand: Session > Clear tempo map goes back to
one tempo (the map's opening one).

Signal flow: input strip -> tape in (normalled) -> tape out -> return strip (normalled).
Both banks go to main and/or the subs. Subs -> group EQ -> inserts -> sub fader -> main.
Main: bus compressor -> master inserts -> main fader -> limiter at -3 dBFS -> outputs.


MIDI PAGE
---------

The MIDI button (Ctrl+M) shows the MIDI page in place of the console: MIDI tracks (each with one VST3
instrument, a MIDI input and Out: routing into the input section or the tape), a timeline of clips in
Bars or Min:Sec with snap, and the piano roll of the selected clip. The timeline runs with the tape.
Mouse wheel: scrolls up and down; Shift + wheel scrolls across; Alt + wheel zooms across; Ctrl + wheel
zooms down (taller or shorter tracks, or piano roll rows). Alt + drag duplicates a clip.
Drag a MIDI file onto the timeline to make a clip; if the file has a tempo map, a menu asks whether to
bring the map too (it starts where the clip lands).


TAPE
----
Arm tracks to monitor and record their input; unarmed tracks play back.
Rew and FF wind like a real machine: they speed up over 3 seconds to 20x play speed and slow
down to arrive at the start or the end of the recording. Stop brakes; Play takes over at once.
Rec arms recording; Play rolls the tape. Recording is shifted by the interface's latency
(plus any plugin delay) so overdubs line up with what you heard. If they still land early
or late with your interface, correct it under Audio device > Recording offset.

Speed 7.5 / 15 / 30 ips changes the tape character (saturation, head bump, top end, hiss).
W&F: subtle wow and flutter (about +/-0.2 ms at 7.5 ips, less at higher speeds; switch it off
for sample-exact overdubs).

Locate: "Set In" / "Set Out" mark a region at the current position.
  Loop  - play the region round and round.
  Punch - armed tracks record only inside the region; outside it you hear the old take.

Click the cassette label to rename the project; it is the default session and export name.

Jog / shuttle (right of the group EQ):
  Shuttle ring - turn it to wind either way, faster the further it turns (1/2x to 32x); let go
                 and it springs back and stops. Double-click it to stop.
  Jog wheel    - one turn moves the tape one second; the mouse wheel over it jogs too.
  Jog / Scrub  - Jog moves the tape silently; Scrub lets you hear it (unarmed tracks) while the
                 wheel or ring moves it, to find a spot by ear.
  In / Out     - Set In, Set Out, Loop, Punch (the same switches as under the tape machine);
                 Go In / Go Out wind the tape there.
  Locate       - Store, then 1-6, keeps the tape position (saved with the session); 1-6 then wind
                 the tape there. RTZ winds back to zero.

Keyboard: Space play/stop, Home rewind, End fast forward, Ctrl+N new, Ctrl+O open, Ctrl+S save,
Ctrl+Shift+S save as, Ctrl+E Bounce Song, Ctrl+Shift+E Export From Tape. Jog / shuttle: J Jog/Scrub, M Store, 1-6 memories, 0 RTZ,
I / O Set In / Out, Shift+I / Shift+O Go In / Out, L Loop, P Punch, , and . jog a notch back /
forward, [ and ] step the shuttle ring back / forward (it stays there until Stop).
Options > "Keyboard shortcuts" changes any of these and adds keys for Play, Stop and Record.


PATCH BAY
---------
"Patch bay" opens it. It has two pairs of rows, outputs above inputs. Click an output, then an
input, to plug a cable. Click a plugged jack to pull its cables. Dashed lines are the normals;
a cable into a tape or return input breaks its normal. A cable into a channel strip input adds
to the interface input.

The Interface in jacks (top left) are your interface's inputs. The thin lines show which one
feeds each channel strip: click an Interface in jack, then a Channel strip in jack, to change it.
Click a channel strip jack to unplug it (patch cables come out first). "Interface 1 to 1" feeds
channel 1 from input 1, channel 2 from input 2, and so on. "Pull all cables" leaves these alone.


PLUGINS (VST3, and Audio Units on a Mac)
----------------------------------------
The inserts row in the centre shows the 4 insert slots of each sub, aux return and the master,
over their faders (slot 1 at the top; amber = running, dim = bypassed). Click a slot to open its
plugin (an empty one: the plugin menu); the list button left of it opens the plugin menu;
right-click a plugin for Bypass, Replace and Remove. "Rack" shows every slot at once.
Aux returns: put an effect in an aux's slots (e.g. a reverb at 100% wet) and set its Return level.
A return is heard only when its chain has an active plugin. Plugin delay is compensated
automatically across the whole console.


SESSIONS AND EXPORT
-------------------
A session is a folder (in Documents\Oxide-3216 Sessions) with session.json (every control,
cable, plugin and its settings, the project name) and one WAV per tape track. It saves
itself, like real tape: recording, importing and erasing go into the track WAVs within half
a second, and console moves into session.json a moment later. Recording over something
replaces it for good. There is no undo.

Session > New asks for a name and creates the folder. Session > Save now writes everything
at once. Session > Save as copies the session to a new folder and carries on in the copy;
the old folder stays as it was, safe from new recording. Session > Open: choose the
session.json inside a session folder. The last session reopens at start-up.

Session > Export tracks, both into the session's Bounced Audio folder:
- Bounce Song (Ctrl+E): plays the song in real time and records the main mix (after the limiter,
  without the click) to a 24-bit stereo WAV, so you can mix as it plays. Whole song (to the end of the
  tape or the last MIDI clip, plus 3 s) or In to Out. Finish or Stop ends it early; Cancel throws it away.
- Export From Tape (Ctrl+Shift+E): the tape tracks you tick, whole tape or In to Out, straight off the
  tape, one mono 24-bit WAV each ("<name> Track 01.wav", plus the return strip's name if renamed).


WHERE THINGS ARE KEPT
---------------------
%APPDATA%\Oxide-3216\Oxide-3216.settings   audio device, routing, plugin list
(Mac: ~/Library/Application Support/Oxide-3216/Oxide-3216.settings)

The app used to be called ConsoleTape. The first time Oxide-3216 starts it copies the old
%APPDATA%\ConsoleTape settings. If Documents\ConsoleTape Sessions exists it keeps saving
there, and sessions saved by ConsoleTape open as before.


CREDITS AND LICENCES
--------------------
Built with JUCE 8 (juce.com). ASIO is a trademark and software of Steinberg Media
Technologies GmbH. VST is a trademark of Steinberg Media Technologies GmbH.
