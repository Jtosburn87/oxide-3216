# Oxide-3216

JUCE 8 / CMake app, built for Windows (main development PC), Linux and macOS. See BRIEF.md for the design.

## Three platforms, one codebase
- Windows: `build.ps1` (Visual Studio, ASIO + WASAPI), `release.ps1` for the zip.
- Linux: `build.sh` (GCC, ALSA + JACK), output in `build-linux/`. A second PC running Linux Mint
  pulls from GitHub every 15 minutes and rebuilds automatically (`update-linux.sh`), so every
  commit pushed to `main` must still build and pass `EngineTests` on Linux.
- macOS: `build-mac.sh` (Xcode command line tools, Core Audio, VST3 + Audio Units), universal
  binary, output in `build-mac/`, zip in `dist/`. There is no Mac here: the "Mac build" GitHub
  Actions workflow (`.github/workflows/macos.yml`) builds it and runs `EngineTests` on a GitHub Mac.
  It runs by hand (Actions > Mac build > Run workflow) or on a `v*` tag, because Mac minutes are
  expensive on a private repo. The app is ad-hoc signed only (no Apple developer ID yet).
- Keep Windows-only code inside `#if JUCE_WINDOWS` (with a fallback or no-op for the others), Mac-only
  code inside `#if JUCE_MAC`, and platform CMake settings inside `if(WIN32)` / `if(APPLE)`. No
  `<windows.h>`, Win32 calls, `__declspec`, or `C:\` paths outside those guards. Use `juce::File` and
  `getSpecialLocation` for paths, never hard-coded separators.
- Modifier keys: use `ModifierKeys::commandModifier` (Ctrl on Windows/Linux, Cmd on a Mac), not
  `ctrlModifier`; on a Mac Ctrl+click is a right-click.
- New source files go in `CMakeLists.txt` (all platforms use it); never only in a Visual Studio project.

## After making changes
Commit, then `git push` so the Linux PC picks them up.
