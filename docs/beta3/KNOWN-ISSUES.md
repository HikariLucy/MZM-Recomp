# Known issues — MZM Recompiled Beta 3

Current and verified at the time of this build.

- **Windows runtime community validation is pending.** The Windows build was cross-compiled
  and inspected statically (architecture, imports, DLLs); it has not been run on Windows by
  the developers. Launcher, audio device, stack behaviour and non-ASCII paths are untested there.
- **Audio fidelity has not been verified against hardware or a reference.** Audio is produced and
  non-silent; accuracy is not claimed.
- **The GBA-like colour mode is approximate.** No measured LCD data was used.
- **Monitor Refresh repeats frames; there is no interpolation.** The game still runs at ~59.73 Hz.
  On a 144 Hz display each game frame is shown two or three times. Presentation is single-threaded,
  so achieved PRESENT is slightly under the monitor rate.
- **NES unlock follows the original game's progression** unless you use your own compatible
  completed save. No unlock shortcut is provided.
- **Full-game coverage is not complete.** Only the paths exercised so far are qualified; an
  unqualified code path stops with an error instead of silently falling back, and a report with
  `latest.log` is very welcome.
- Only the USA revision 0 ROM and the standard 16 KB GBA BIOS are accepted.
- Linux needs system SDL2 and OpenGL. Window size/position are not remembered.
- The controller menu was verified with a virtual pad on Linux, not a physical one.
- The Windows executable is unsigned (see the SmartScreen note in README.md).
