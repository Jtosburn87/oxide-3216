# Console 32 / Tape 16: Windows recording and mixing app

Build a Windows desktop app (.exe) for recording and mixing songs: a 16-track tape machine fed by a 32-channel SSL-style mixing console, with VST3 plugin slots on the buses.

## How to work

- Stack: C++ with JUCE 8 (CMake build), ASIO for audio I/O, JUCE's built-in VST3 hosting. Windows 10/11, x64.
- Work in this folder. Set up what you need (Visual Studio Build Tools with the C++ workload, CMake, Git; fetch JUCE with CMake FetchContent). Tell me before any large download.
- ASIO: Steinberg's ASIO SDK has its own licence that I must accept myself. Stop and tell me where to download it and where to put it. Do not download it yourself.
- Work in the stages at the bottom. Build, run and test each stage before the next, and commit after each. If a build fails, read the error, fix it and rebuild yourself. I am not a C++ developer, so explain problems in plain language.
- The finished product is a runnable `ConsoleTape.exe` and a zip I can copy to another PC.

## Reference files in this folder

- `console-tape.html`: working web prototype. Open it in Chrome. It is the spec for layout, controls, value ranges, signal flow and behaviour. Where this brief and the prototype differ, ask me.
- `ref-console.jpg`: top view of a real large console. Match the overall structure: left bank, centre section, right bank, black meter bridge on top, mid-grey knob area above a black fader area, black end cheeks.
- `ref-buscomp.webp`: faceplate of a stereo bus compressor. Match its look for the master bus compressor.
- Do not copy any manufacturer names or logos. Use generic names.

## Layout

- Left bank: 16 input channels. Right bank: 16 tape-return channels. Centre: tape machine, group EQs, bus compressor, bus faders.
- Black meter bridge across the top: one vertical LED meter per channel above each strip; stereo meters above the sub and main faders (mono above each aux fader), aligned over their faders.
- Each channel strip, top to bottom: bridge cell with meter, mid-grey knob area with boxed printed legends, black fader area with mute, long-throw fader with printed scale, white scribble strip with the channel name.
- Knobs: small, dark skirt with white pointer, coloured centre cap, printed tick marks around them. EQ caps: HF red, HMF green, LMF blue, LF black. Muted, matte, not cartoony.
- Zoom slider and Fit button in the top bar so the whole console can be seen at once. The patch bay is behind a "Patch bay" button and opens as a pop-up panel.
- Dark theme only is fine.

## Channel strip (all 32)

- Input trim and a sweepable high-pass filter (20 to 300 Hz).
- 4-band EQ, gain +/-15 dB with a sweepable frequency for each band: HF shelf 1.5 to 16 kHz, HMF bell 0.6 to 7 kHz, LMF bell 0.2 to 2.5 kHz, LF shelf 30 to 450 Hz. Each frequency knob shows its live value and has its range printed at the ends of the sweep.
- Gate: threshold, range, and a Fast Attack button.
  - Normal attack about 1.5 ms. Fast attack opens in about 0.1 ms. Hysteresis 5 dB, hold 50 ms, release about 120 ms.
  - With Fast Attack on, when a loud transient (above about -12 dBFS) hits a gate that was closed, add an exaggerated pop: a low thump (about 150 Hz, decaying over about 14 ms) plus a short noise click (about 0.7 ms), at roughly 1.2 times the transient peak, capped at 0.8. Re-trigger lockout 40 ms. Flash the button when it fires. Make the pop amount easy to change.
- Compressor, modelled on a black-faced 1176-style FET compressor:
  - Input knob 0 to 10 drives the signal into a fixed threshold (about -14 dBFS); 3 dB of drive per unit.
  - Output knob, +/-12 dB.
  - Attack 1 to 7 (7 is fastest): 800 us down to 20 us. Release 1 to 7 (7 fastest): 1.1 s down to 50 ms.
  - Ratio buttons 4, 8, 12, 20, plus an All button: all buttons in gives about 60:1 with extra drive and distortion and a release 1.4 times slower.
  - In/out button (default out) and a gain-reduction meter.
  - Peak detection, stereo-linked, soft knee about 4 dB, gentle asymmetric tanh saturation after the gain reduction. No automatic makeup gain.
- 8 aux sends, each with a pre/post button. 4 stereo sub assigns, main assign, pan, solo, mute.

## Centre section

Left column:
- Tape machine shown as a beat-up old cassette: worn plastic, chipped corner, scratches, stained paper label. The label is an editable handwritten-style text field showing the project name. Save it with the session and use it as the default session and export file name. Show the tape speed on the label. The two hubs spin at a rate that depends on tape speed, and the tape packs move from one side to the other as the tape plays.
- Clock, transport (Power, Rew, Stop, Play, Rec, Erase with confirmation), speed switch 7.5 / 15 / 30 ips, record level knob, 16 record-arm buttons.
- Group EQ: four independent 4-band sweepable EQs, one per sub bus, same bands as the channel EQ.

Right column:
- Stereo bus compressor faceplate, as in `ref-buscomp.webp`: a black VU-style meter showing 0 to 20 dB of gain reduction with a moving needle, six large blue-capped knobs with printed scales, and an IN button.
  - Threshold: -20 to +20 dB, 0 at the top. Map knob 0 to -18 dBFS, 0.9 dB per unit.
  - Make up: -5 to +15 dB.
  - Attack: .1 .3 1 3 10 30 ms. Release: .1 .2 .4 .8 1.6 s and Auto. Ratio: 1.5 2 3 4 5 10.
  - HPF on the sidechain only: OFF 30 60 105 125 185 Hz, 12 dB/oct.
  - DSP: stereo-linked feed-forward VCA-style compressor, soft knee about 6 dB, no automatic makeup. Auto release is a blend of a 0.1 s and a 1.2 s release. Peak detector with about 1 ms decay.
- Signal order on the main bus: sub and channel outputs, bus compressor, master VST3 inserts, master fader, limiter at -3 dB, outputs.

Bottom, black fader area across the whole centre section: Sub 1 to 4, Aux masters A1 to A8, and the Main fader on the far right behind a divider. Give these faders plenty of room: long throw, wide cap, printed scale.

## Tape machine behaviour

- 16 tracks. Armed tracks monitor the input; unarmed tracks play back. Recording must be sample-accurate and compensated for interface latency, so overdubs line up with what I heard in my headphones.
- Tape character by speed (starting values from the prototype, tune by ear):

| Speed | Saturation | Head bump | High shelf at 8 kHz | Low-pass | Hiss |
|---|---|---|---|---|---|
| 7.5 ips | strongest | +4 dB at 90 Hz | -3 dB | 12 kHz | -50 dBFS |
| 15 ips | medium | +2.5 dB at 60 Hz | -1 dB | 18 kHz | -57 dBFS |
| 30 ips | lightest | +1.5 dB at 38 Hz | 0 dB | 21 kHz | -64 dBFS |

- Later: subtle wow and flutter, scaled by speed; punch in/out; loop region.

## Patch bay (pop-up)

- Styled like a TRS patch bay. Top row outputs: channel direct outs (16), return direct outs (16), tape outs (16), aux outs (8), sub outs (4), main L/R. Bottom row inputs: tape ins (16), return strip ins (16), channel strip ins (16).
- Click an output, then an input, to plug a cable. Click a plugged jack to pull it. Cables are colour-coded by source with a shadow.
- Normalled by default: channel out to tape in, tape out to return strip in (shown as dashed lines). A cable into a normalled input breaks the normal.

## Audio I/O

- Audio device window: choose ASIO driver, sample rate and buffer size; map interface inputs to the 16 input channels and interface outputs to main and, optionally, the aux and sub outputs.
- Record several inputs at once.

## VST3 plugin slots

- 4 insert slots on each of: the 4 sub buses (after the group EQ), the 8 aux returns, and the master bus (after the bus compressor, before the master fader).
- Each slot: plugin picker, bypass button, a button to open the plugin's own window, and a drag-to-reorder or simple up/down order control.
- Scan `C:\Program Files\Common Files\VST3` and any extra folders I add, in a separate process so a bad plugin cannot crash the app. Remember plugins that failed to scan.
- Aux returns: each aux bus needs a return path to the main mix with its own level, so reverbs and delays set to 100% wet work.
- Use automatic latency compensation across the whole graph. Handle mono, stereo and multi-channel plugins sensibly. Handle high-DPI scaling for plugin windows.
- Save and restore every plugin's state inside the session, and show a clear message if a saved plugin is missing.

## Sessions and export

- Save and load sessions as a folder: one WAV per tape track, plus a JSON file with every control, patch cable, plugin state and the project name from the cassette label.
- Export the main bus to a 24-bit WAV file.
- Auto-save, and ask before closing with unsaved changes.

## Build stages

1. Project builds to an .exe. ASIO device picker works. Audio passes through. Basic window.
2. 4 channel strips, main fader, and a tape machine that records and plays 4 tracks, with the real transport and clock.
3. Full 32 strips (EQ, gate with fast attack and pop, 1176-style compressor, aux and sub sends), group EQs, meter bridge.
4. 16-track tape with the speed switch and tape character, cassette graphic with editable label.
5. Bus compressor faceplate and DSP, master section, bus faders and meters.
6. Patch bay pop-up with normalled routing.
7. VST3 hosting and the insert slots.
8. Sessions, WAV export, punch/loop, wow and flutter, polish, and the zipped release.

## Quality bar

- Never block or allocate memory in the audio thread. No audio dropouts at 64 to 256 sample buffers on a normal laptop with 16 tracks playing.
- Keyboard-accessible controls, visible focus, readable text at 100% and 150% Windows scaling.
- The look must follow the reference photos and the prototype: matte, dense, hardware-like. Draw knobs, faders and meters as vector graphics with a custom JUCE look-and-feel.
