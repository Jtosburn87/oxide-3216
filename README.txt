CONSOLE 32 / TAPE 16
====================

A 16-track tape machine fed by a 32-channel console, for Windows 10/11 (64-bit).


RUNNING IT
----------
Unzip anywhere and run ConsoleTape.exe. Nothing to install: it needs no extra runtime.

First time:
  1. Click "Audio device" and pick your interface's ASIO driver, sample rate and buffer size.
     Map interface inputs to the 16 input channels (Inputs tab) and choose the main outputs
     (Outputs tab; aux and sub outputs are optional).
  2. Click "Plugins", then Options > "Scan for new or updated VST3 plug-ins" (the standard folder
     is C:\Program Files\Common Files\VST3; add others in the scan dialog). Each plugin is scanned
     in its own process, so a bad plugin cannot crash the console. Failed ones are shown in red.


THE CONSOLE
-----------
Left bank: 16 input channels. Right bank: 16 tape-return channels. Centre: tape machine,
group EQs, bus compressor, inserts and aux returns, bus faders and the main fader.
Zoom with the slider; "Fit" shows the whole console.
"Sections" moves the input bank, centre section or return bank into a window of its own (for a
second monitor). Close that window, or press "Put back", to return it to the console. Windows
that are out stay out, in the same place, the next time ConsoleTape starts.

Every strip: trim, high-pass filter, 4-band EQ, gate (Fast atk = fast attack with the pop),
1176-style compressor, 8 aux sends (P = pre-fader), sub assigns S1-S4, main assign, pan,
mute, solo, fader. Double-click a scribble strip to rename it.

Knobs and faders: drag up/down, mouse wheel, double-click for the default; with the keyboard,
arrows step, Page Up/Down step more, Home or Delete resets.

Signal flow: input strip -> tape in (normalled) -> tape out -> return strip (normalled).
Both banks go to main and/or the subs. Subs -> group EQ -> inserts -> sub fader -> main.
Main: bus compressor -> master inserts -> main fader -> limiter at -3 dBFS -> outputs.


TAPE
----
Arm tracks to monitor and record their input; unarmed tracks play back.
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

Keyboard: Space play/stop, Home rewind, Ctrl+N new, Ctrl+O open, Ctrl+S save,
Ctrl+Shift+S save as, Ctrl+E export.


PATCH BAY
---------
"Patch bay" opens it. Click an output (top row), then an input (bottom row). Click a plugged
jack to pull its cables. Dashed lines are the normals; a cable into a tape or return input
breaks its normal. A cable into a channel strip input adds to the interface input.


PLUGINS (VST3)
--------------
The FX buttons in the centre open the insert slots: 4 on each sub, each aux return and the
master. Choose a plugin, Byp = bypass, Edit = its window, arrows = order.
Aux returns: put an effect on an aux's FX (e.g. a reverb at 100% wet) and set its Return level.
A return is heard only when its chain has an active plugin. Plugin delay is compensated
automatically across the whole console.


SESSIONS AND EXPORT
-------------------
Session > Save writes a folder (by default in Documents\ConsoleTape Sessions) with
session.json (every control, cable, plugin and its settings, the project name) and one
WAV per tape track. Session > Open: choose the session.json inside a session folder.
The last session reopens at start-up. You are asked before closing with unsaved changes.
An auto-save is kept every 2 minutes; if ConsoleTape did not close properly, it offers to
recover it next time.

Session > Export mix renders the main bus (after the limiter) from the start of the tape to
its end plus 3 seconds, to a 24-bit WAV.


WHERE THINGS ARE KEPT
---------------------
%APPDATA%\ConsoleTape\ConsoleTape.settings   audio device, routing, plugin list
%APPDATA%\ConsoleTape\Autosave\              the latest auto-save


CREDITS AND LICENCES
--------------------
Built with JUCE 8 (juce.com). ASIO is a trademark and software of Steinberg Media
Technologies GmbH. VST is a trademark of Steinberg Media Technologies GmbH.
