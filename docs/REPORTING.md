# Reporting problems (Support module)

Builds after Beta 3 add a **Support** page to the launcher (Home → Support, or About → Support / Report
Problem). It helps a tester describe a problem. **Nothing is uploaded automatically**: there is no telemetry,
no crash uploader, no backend and no GitHub API call. Every action below happens only when you click it.

## What it shows

Release label and channel (`development | beta | rc | stable`), MZM and GBARecomp commit (compiled in, so
every build reports its own), platform, and whether the previous session ended cleanly.

## What it collects (locally, on demand)

Collected when the page is opened or **Refresh details** is pressed — never per frame, never while you play:

| Item | Source |
|---|---|
| OS | Windows `RtlGetVersion`; Linux `/etc/os-release` + kernel |
| CPU | CPUID brand string (`Unknown` if unavailable) |
| GPU / OpenGL | `glGetString` of the launcher's own window |
| Display refresh | SDL current display mode of the launcher window |
| Gamepad / audio device | SDL names (a short-lived SDL subsystem; `Unknown` if SDL cannot tell) |
| Display options | `config.ini [Display]` (presentation, window mode, filter, scaling, CRT, colour) |
| ROM / BIOS | only `configured, valid / INVALID / not configured` |
| Last session | `latest.log` of the previous run: clean exit, unexpected end, or crash marker |

The report also states the game runs at ~59.73 Hz; a high *display* refresh is not game FPS.

## What is never included

ROM/BIOS contents or paths, save data, user name, host name, IP/MAC, environment variables, tokens. Config and
log paths have your home directory replaced by `~` (Linux) or `%USERPROFILE%` (Windows); the "recent log"
view applies the same redaction. A long log is never copied wholesale: you attach `latest.log` yourself.

## Buttons

| Button | Action |
|---|---|
| Report Issue on GitHub | copies the bug report to the clipboard, then opens `…/MZM-Recomp/issues/new?template=bug_report.yml&title=[Beta]` in your browser. The URL carries no diagnostics. If the browser cannot open you are told the URL. |
| Copy Bug Report | markdown template ready to paste into the issue (build, hardware, problem area, steps, components, diagnostics) |
| Copy Diagnostic Info | the full local summary |
| Copy Build Info / Copy Log Path | build identity / path of `latest.log` |
| Open Log Folder / Open latest.log | the OS file manager / default app (arguments are never passed through a shell) |
| Open Feedback Guide | opens the packaged `FEEDBACK.md`; the same instructions are built in (**What to send**) |

Everything except opening the browser works offline.

## Logs

`latest.log` is rewritten each run (Windows `%LOCALAPPDATA%\MZMRecompiled\logs\`, Linux
`~/.local/state/MZMRecompiled/logs/`). From this version the previous run's log is kept as `previous.log`, and
as `last-crash.log` when it ended with a crash marker or without a clean-exit line, so restarting the program
does not erase the evidence. Opening Support never deletes anything. The log records `previous_session=` at start.
Review a log before sharing it; the runtime may print file paths in some error messages.

## Reporting workflow

1. Support → choose the **problem area** → **Report Issue on GitHub** (the report is already on the clipboard).
2. Paste it into the form, describe what happened, attach `latest.log` and, if useful, a screenshot.
3. Do **not** upload ROM, BIOS or `.sav` files.

## Windows security warnings

The Windows executable may be unsigned and may trigger SmartScreen, reputation or policy warnings. Do not
disable your antivirus. Report the Windows version, the security product, the exact detection/message, a
screenshot and the SHA-256 of the ZIP and of `MZMRecomp.exe` (issue template: *Antivirus / SmartScreen*). On a
managed work or school PC, use an authorized personal computer instead.

## Developer notes

Text generation is pure and unit-tested (`src/mzm_diagnostics.*`, `mzm-diagnostics`); platform glue is in
`src/mzm_support.*`. Tests may set `MZM_SUPPORT_TEST_DIR` so that URL/folder/file opens are recorded in
`opener.txt` instead of launching anything and clipboard writes are mirrored to `clipboard.txt`
(`scripts/run-support-module.py`). The release label comes from `-DMZM_RELEASE_LABEL=` (e.g. `"Beta 4"`) and
`-DMZM_RELEASE_CHANNEL=`.
