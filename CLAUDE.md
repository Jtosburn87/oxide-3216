# Oxide-3216

JUCE 8 / CMake app, built for Windows (main development PC) and Linux. See BRIEF.md for the design.

## Two platforms, one codebase
- Windows: `build.ps1` (Visual Studio, ASIO + WASAPI), `release.ps1` for the zip.
- Linux: `build.sh` (GCC, ALSA + JACK), output in `build-linux/`. A second PC running Linux Mint
  pulls from GitHub every 15 minutes and rebuilds automatically (`update-linux.sh`), so every
  commit pushed to `main` must still build and pass `EngineTests` on Linux.
- Keep Windows-only code inside `#if JUCE_WINDOWS` (with a Linux fallback or no-op), and Windows-only
  CMake settings inside `if(WIN32)`. No `<windows.h>`, Win32 calls, `__declspec`, or `C:\` paths outside
  those guards. Use `juce::File` and `getSpecialLocation` for paths, never hard-coded separators.
- New source files go in `CMakeLists.txt` (both platforms use it); never only in a Visual Studio project.

## After making changes
Commit, then `git push` so the Linux PC picks them up.
