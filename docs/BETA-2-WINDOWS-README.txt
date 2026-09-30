MZM Recompiled - Beta 2 (Windows x64)
=====================================

This is a private beta. It contains NO game file and NO GBA BIOS. You need:
  * your own copy of Metroid: Zero Mission (USA) revision 0, a .gba file
    (SHA-1 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8), and
  * your own GBA BIOS file (16 KB).
Nothing else needs to be installed: no Python, Git, CMake, WSL or compiler.

How to run
----------
1. Extract the whole ZIP to a folder you can write to (for example
   C:\Games\MZMRecompiled). Do not run it from inside the ZIP.
2. Double-click MZMRecomp.exe. A console window opens next to the launcher;
   leave it open, it closes with the game.
3. In the launcher choose Game Data, select your game file and your BIOS.
   Both must show "Valid".
4. Press CONTINUE, then PLAY.

Your save file (.sav) is written next to the game file you selected, so keep
that file in a folder you can write to (not Program Files).
Launcher settings: %APPDATA%\MZMRecompiled\
Logs:              %LOCALAPPDATA%\MZMRecompiled\logs\

Logs
----
  latest.log            this run's runtime output (replaced at every start):
                        build, GBARecomp commit, cpu_backend, strict_static
                        and dispatch counters, and the reason of any fatal error.
  mzm-recompiled.log    launcher/session events (appended, one line each).
The logs never contain the game or BIOS contents. They can contain your
Windows user name inside file paths: read them before sending.

If something fails, send
  1. latest.log and mzm-recompiled.log,
  2. BUILD-INFO.txt,
  3. what you were doing, your Windows version and GPU,
  4. the completed TESTER-CHECKLIST.txt.
Nothing is uploaded automatically.

Known limits are listed in BUILD-INFO.txt. This beta is for noncommercial use.
