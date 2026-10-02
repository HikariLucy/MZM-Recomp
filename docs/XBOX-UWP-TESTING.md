# Xbox Series X|S Beta Testing & Deployment Guide

This guide is intended for testers running Xbox Series X|S consoles in **Developer Mode**.

> [!IMPORTANT]
> - Do NOT share console credentials, device portal passwords, or private IP addresses in bug reports or public channels.
> - Never request or share copyrighted ROM or BIOS binaries.

---

## 1. Prerequisites on Console

1. The Xbox console must be in **Developer Mode** (via the Xbox Dev Mode activation app).
2. Note the console IP address displayed in Dev Home (e.g., `192.168.1.xxx`).
3. Ensure **Xbox Device Portal** is enabled in Dev Home settings.
4. On your PC, open a web browser to:
   ```
   https://<console-ip>:11443
   ```
   (Accept the self-signed HTTPS certificate prompt).

---

## 2. Deploying MZM Recompiled

### Step A: Install the Development Certificate (One-Time Setup)
1. In Device Portal, navigate to **Security** / **Certificates**.
2. Upload and install the public `MZMDev.cer` into the console certificate store.

### Step B: Install the Package
1. Navigate to the **Apps** / **Add** section in Device Portal.
2. Under "Select your package", choose `MZMRecompiled-Xbox-x64.msix` (or `.appx`).
3. Click **Deploy**. Wait until installation reaches 100%.

### Step C: Staging Game Data (ROM & BIOS)
Because MZM Recompiled does NOT ship with copyrighted game files:
1. In Device Portal, click **File Explorer**.
2. Navigate to:
   ```
   DevelopmentFiles/LocalAppData/MZMRecompiled_<id>/LocalState/
   ```
3. Create a folder `roms` and upload your legally obtained:
   - `Metroid - Zero Mission (USA).gba` (SHA-1: `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`)
4. Create a folder `bios` and upload:
   - `gba_bios.bin` (SHA-1: `300c20df6731a33952ded8c436f7f186d25d3492`)

---

## 3. Launching from Dev Home

1. On the console, switch to the TV screen and open **Dev Home**.
2. Locate **MZM Recompiled** under the Games / Apps list.
3. Highlight the tile and press **A** to launch.

---

## 4. First Hardware Test Contract

Testers evaluate the build against this verification matrix:

| Test Item | Verification Criteria | Expected Result | Status (PASS / FAIL / BLOCKED) |
| :--- | :--- | :--- | :--- |
| **INSTALL** | Package installs cleanly via Device Portal without certificate or manifest errors | 100% complete | |
| **LAUNCH** | Dev Home successfully launches the app; process does not crash on startup | Process active | |
| **FIRST RUN** | App opens to Initial Configuration or detects staged ROM/BIOS | UI visible | |
| **ROM IMPORT** | Validates SHA-1 `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8` | "Valid" | |
| **BIOS IMPORT** | Validates SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492` | "Valid" | |
| **FIRST FRAME** | Main game boots into opening sequence / title screen | First frame drawn | |
| **PLAY** | Samus enters Brinstar; game loop advances normally | Gameplay active | |
| **CONTROLLER** | Xbox Wireless Controller D-Pad/sticks, A, B, L, R, Start respond accurately | Controls work | |
| **AUDIO** | Sound effects and background music play smoothly without stutter | Clean WASAPI audio| |
| **SAVE** | Saving at an in-game Save Station writes save file to `LocalState` | Save confirmed | |
| **RESTART** | App quit via Guide button / Dev Home and relaunched | Clean restart | |
| **CONTINUE** | In-game save is recognized on restart; Samus resumes at Save Station | Save restored | |
| **SUPPORT** | Support page opens; diagnostic info shows Xbox OS & GPU; logs accessible | Diagnostic active | |

---

## 5. Log Collection for Testers

If an issue occurs:
1. Open Device Portal File Explorer.
2. Navigate to:
   ```
   DevelopmentFiles/LocalAppData/MZMRecompiled_<id>/LocalState/logs/
   ```
3. Download `latest.log` and `previous.log` (or `last-crash.log`).
4. Attach logs to the test report.
