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

On a Windows ARM PC (Snapdragon etc.; in testing): two choices. The win-arm64 zip is native
and runs fastest, but only loads ARM64 VST3 plugins, and ASIO only works with an ARM64 ASIO
driver (otherwise use WASAPI). Most plugins are x64 only: the normal win64 zip also runs on an
ARM PC (Windows emulates it) and loads x64 plugins and drivers, at some extra CPU cost.


THE CONSOLE
-----------
Left bank: 16 input channels. Right bank: 16 tape-return channels. Each strip's fader has a column
beside it (after the reference console): Mute, Main and S1-S4 assigns, the channel meter, and Solo with
its LED. Solo goes to the monitor outs and to the main outs (there is no separate monitor out on
the patch bay); exports and Bounce Song ignore it and always take the full mix. Pan sits at the foot of the knob area.
 Centre: tape machine,
group EQs, Tempo, inserts and aux returns, bus faders and the main fader. The stereo bus
compressor has a window of its own: the SBC button above the Master insert slots opens it (SBC lights
while the compressor is in; the GR bar under the slots shows its gain reduction).
Sends on Fader (under the Master slots): pick an aux and every channel fader, inputs and returns, sets
that channel's send to it instead of its level, on the same dB scale (amber caps; a send tops out at
+3.5 dB). A linked aux pair is one choice. Pick Off to give the faders back. The channel window's fader
and the Wi-Fi remote stay on the channel level.
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
Channel colours: click a scribble strip to select it (Shift+click selects the ones between, Ctrl+click
adds or removes one), then click the colour box left of a name: Red, Orange, Yellow, Green, Blue,
Indigo, Violet, White or Black for every selected channel. The name is written in the opposite colour
(made dark or light enough to read). Channels of one colour in the same bank are a group: their faders
move together from where they stood when coloured. White is no group. Right-click drag a grouped
fader to move it alone (a stereo-linked partner still follows). Mute, Main, Solo and S1-S4 switch the
selected channels (when the one clicked is selected) and the whole colour group the same way.
Input and return banks never group together; colours are saved with the session.
Stereo link: any two channels of one bank can be linked. Click Link beside one channel's pan (it
blinks), then Link on the other: both light up and show their partner (e.g. "↔7"). Every control
then moves both, and the pans turn in opposite directions (the lower-numbered channel is the left
side). Linking copies the lower-numbered channel's settings onto the higher one. Click a lit Link
to unlink both. A channel has one partner: linking it elsewhere breaks its old link.

THE CHANNEL WINDOW: double-click any knob on a channel strip to open that channel big, laid out
like a channel-strip plugin (Input, Equaliser, Gate / Compressor, Aux sends, Output). Everything in
it is the console's own control. The arrows either side of the name step through the channels of
the same bank (16 goes round to 1). Copy takes every knob and switch of the channel (not
the fader, solo, name, colour or inserts); Paste puts them on another channel of the same bank
(inputs to inputs, returns to returns). It also has controls the strip doesn't:
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
yours). Store keeps the Fade with the scene as its fade out: going from that scene to any other
glides over it, so a scene's fade in is the fade of the scene you leave. Recalling a scene sets
the Fade knob to that scene's fade (scenes stored before fades were kept use the knob). The list
shows each scene's fade before the date. The window stays on top and the console works behind it.
MIDI control (bottom of Scene Memory): each scene has a controller that selects it (scene NN on
CC NN to start; any value above 0) and one program change presses Go (Program 1 to start). Pick
the MIDI input (None, All MIDI inputs or one device) and/or MIDI page tracks (tracks whose Out
menu has Send to scene control on: their clips' controllers and program changes drive the scenes
in time with the song), and a channel (Any or 1-16). Learn takes the next controller for the
selected scene, or the next program change for Go. Saved with the session.
Scenes and the fade time are saved with the session.

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
bring the map too (it starts where the clip lands). Notes and controllers keep their MIDI channel.

Hardware: the Out: menu also has MIDI out (a MIDI output on this computer; the track plays its clips
and what you play live to it, with or without an instrument), Channel (1-16, or As recorded: each
note on the channel it was played or imported on; this applies to the instrument too) and MIDI out
offset (ms; nudge a synth that speaks late or early). MIDI goes out as the audio it lines up with is
heard, so a hardware synth patched back into the console lands in time on tape.
Sync (MIDI page toolbar) sends MIDI clock to the outputs you tick: 24 clocks a quarter note on the
tempo (and tempo map, and vari-speed), song position + start / continue when the tape plays or
locates, stop when it stops. Clock offset as above. An output switched on while the tape is running
joins at the next play or locate.


TAPE
----
Arm tracks to monitor and record their input; unarmed tracks play back. Each Record arm button
steps grey (off) > yellow (armed: the tape plays back to its return strip until Record is on in
the transport, then it takes the input and records) > red (armed: input only, nothing from tape).
Rew and FF wind like a real machine: they speed up over 3 seconds to 20x play speed and slow
down to arrive at the start or the end of the recording. Stop brakes; Play takes over at once.
Hold Rew or FF down (the keys, or their keyboard shortcuts) and the tape winds up to 40x while held;
let go and it settles back to 20x.
Rec arms recording (the whole key flashes red while it is on); Play rolls the tape. Stop takes
Rec off again; the tracks stay armed. Recording is shifted by the interface's latency
(plus any plugin delay) so overdubs line up with what you heard. If they still land early
or late with your interface, correct it under Audio device > Recording offset.

Speed 7.5 / 15 / 30 ips changes the tape character (saturation, head bump, top end, hiss).
W&F: subtle wow and flutter (about +/-0.2 ms at 7.5 ips, less at higher speeds; switch it off
for sample-exact overdubs). Wow and Flutter knobs (Tape Machine window; right-click W&F on the console)
set each from 0 to 5x what the tape speed has (100 % = as before); double-click resets to 100 %.

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
  Window       - right-click the box anywhere (or double-click its background), or Windows > Jog /
                 shuttle window: a copy of the box with 99 locate memories, six keys at a time (the
                 mouse wheel over them or the up / down keys move the numbers on), and MIDI control:
                 an input and channel, Learn for the jog wheel (a controller: relative encoder or
                 absolute), the shuttle ring (a controller, 64 = centre) and every key. Saved in the
                 settings, and it works with the window closed.

Keyboard: Space play/stop, Home rewind, End fast forward, Ctrl+N new, Ctrl+O open, Ctrl+S save,
Ctrl+Shift+S save as, Ctrl+E Bounce Song, Ctrl+Shift+E Export From Tape. Jog / shuttle: J Jog/Scrub, M Store, 1-6 memories, 0 RTZ,
I / O Set In / Out, Shift+I / Shift+O Go In / Out, L Loop, P Punch, , and . jog a notch back /
forward, [ and ] step the shuttle ring back / forward (it stays there until Stop).
Options > "Keyboard shortcuts" changes any of these and adds keys for Play, Stop and Record. Its MIDI
tab gives any command a MIDI note or controller (Learn, then press it), from any MIDI input.

Wi-Fi remote: the Remote button (top bar, lit while it is on) opens a window with a QR code. Turn the
remote on and scan the code with a phone or iPad on the same network: its browser shows the counter,
the transport (hold Rew / F Fwd for 40x), record arm for tracks 1-16 (tap to step off / yellow / red,
Disarm all), the jog / shuttle wheel, the In / Out keys and all 99 locate memories (Store, then a
number; RTZ). No internet or app needed, and older iPads (iOS 12) work. Oxide serves the page itself
on port 52819; the link has a secret key (New link makes a new one and drops old phones). If the
venue Wi-Fi keeps devices apart, turn on the computer's Mobile Hotspot and join that instead. If the
phone holding the shuttle ring drops off the network, the tape stops winding by itself.
The tabs along the top (each with little meters) switch between Center (the tape machine, above) and
the channels: In 1-8 / 9-16 (the inputs) and Out 1-8 / 9-16 (the tape returns). A channel tab shows 8
fader columns as on the console: Mute, the fader and meter, Solo, and the name at the bottom. Tap a name
for that channel's whole strip (Input, EQ, Gate or De-esser, Comp, Aux sends, Pan, and the output with
Main and S1-S4); the arrows step through the bank. SENDS ON FADER over the faders (Off, A1-A8, linked pairs as one)
turns them into that aux's sends, amber caps, on this phone or iPad only (the console keeps its own, and
each device remembers its choice) - handy for a player running their own headphone mix. Knobs turn with a drag (sideways always; up / down
too when the page isn't scrolling), a double-tap resets one; double-tap a fader for 0 dB. Fader, mute,
solo and assigns move their colour group, as on the console. Meters follow each bank's Meters In/Out.


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

Session > Import audio (Ctrl+I): choose one or more files (Ctrl or Shift click; Add files... adds
more). Up to 16 tracks: a mono file takes one track, a stereo file any two (L and R boxes in
the list). Each file gets a free track (an empty one if there is one; a stereo file a neighbouring
pair while there is one); change it in the list, or select a file and click a track in the row of
16 (a stereo file's left goes there, its right on the next free track; a file already there moves
out of the way). A stereo file links the two return channels it lands on and pans them hard left
and right. They all start at the same place: the start of the tape, the tape position or In.

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
The Wi-Fi remote's QR codes: QR Code generator library by Project Nayuki (MIT licence,
third_party/qrcodegen).
