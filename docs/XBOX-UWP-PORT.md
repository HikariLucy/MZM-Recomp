# Xbox Series X|S / UWP Portability Matrix & Architecture

## Overview & Target Definition

- **Target Platform**: Xbox Series X|S (Developer Mode)
- **Application Model**: Universal Windows Platform (UWP / WinRT)
- **Target Architecture**: `x64` (`x86_64`) strictly (no x86-32, ARM64 optional future)
- **Execution Sandbox**: AppContainer / UWP sandboxed environment
- **Baseline Release**: MZM Recompiled Beta 4 (`9a5217a33e561839cbe82561d8eb4725d4c6ca55`)
- **Pinned Dependencies**: GBARecomp (`266f82f556fe995ece1bfc558cfe59496b0ef31c`), recomp-ui (`2b7e9c6140be55da99941747071e540c026a43cf`), SDL 2.30.0+

---

## 1. Portability Matrix

| Component | Current Linux Implementation | Current Win32 Implementation | UWP / Xbox Compatibility | Action (Reuse / Adapt / Replace) | Blocker | Evidence / Technical Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **CPU / Recompiler** | AOT generated C++ (`recompiled_*.cpp`), static dispatch table | Same | Fully compatible | **Reuse** | None | Pure C++ static code; no JIT, no runtime code generation. |
| **Memory** | Standard heap (`malloc`/`new`), static guest RAM arrays | Same | Fully compatible | **Reuse** | None | No `mmap(PROT_EXEC)` or `VirtualAlloc` executable pages needed. `HOST_EXECUTABLE_MEMORY_REQUIRED=NO`. |
| **Timing** | `std::chrono`, `SDL_GetTicks64`, `SDL_GetPerformanceCounter` | Same | Fully compatible | **Reuse** | None | SDL WinRT wraps `QueryPerformanceCounter` / WinRT timers. |
| **Event Loop** | `SDL_PollEvent`, `SDL_PumpEvents` | Same | Fully compatible | **Reuse** | None | `SDL_WinRTApp::PumpEvents` integrates with `CoreWindow::Dispatcher`. |
| **Window / View** | X11 / Wayland `SDL_Window` | Win32 `HWND` via `SDL_CreateWindow` | Fully compatible | **Adapt** | None | WinRT uses `CoreWindow` managed by `SDL_CreateWindow` with `SDL_WINDOW_FULLSCREEN`. |
| **Game Renderer** | `SDL_Renderer` (OpenGL/software fallback) in GBARecomp `host_window.cpp` | `SDL_Renderer` (D3D11/OpenGL) | Direct3D 11 native | **Reuse / Adapt** | None | GBARecomp `host_window.cpp` renders GBA scanlines/framebuffer via `SDL_Renderer` (`SDL_Texture`). In WinRT, `SDL_Renderer` defaults to Direct3D 11. |
| **Launcher Renderer** | OpenGL 3.3 Core profile via `SDL_GL_*` in `game_launcher_boot.cpp` | Same | Incompatible without ANGLE or D3D11 backend | **Adapt / Replace** | Low | Either use ANGLE (GLES2) or align launcher with game runtime to use `SDL_Renderer` + `ImGui_ImplSDLRenderer2`. |
| **In-Game Menu** | `ImGui_ImplSDL2` + `ImGui_ImplSDLRenderer2` | Same | Fully compatible | **Reuse** | None | GBARecomp already uses `ImGui_ImplSDLRenderer2` over `SDL_Renderer`. |
| **Audio** | ALSA / PulseAudio via SDL2 | WASAPI / DirectSound via SDL2 | Fully compatible | **Reuse** | None | SDL2 WinRT has dedicated WASAPI backend (`SDL_wasapi_winrt.cpp`). |
| **Controller** | Linux evdev/hidapi via `SDL_GameController` | XInput / RawInput via `SDL_GameController` | Fully compatible | **Reuse** | None | SDL2 WinRT implements Windows.Gaming.Input (WGI) backend (`SDL_windows_gaming_input.c`). |
| **Keyboard / Mouse** | Fully supported | Fully supported | Supported by OS, optional | **Adapt** | None | Xbox UI must be controller-first (D-pad/stick/A/B/Start). No mandatory mouse/keyboard. |
| **Filesystem** | `std::filesystem`, `$HOME/.config`, `$HOME/.local/state` | `std::filesystem`, `%APPDATA%`, `%LOCALAPPDATA%` | Sandboxed to `ApplicationData::LocalFolder` | **Adapt** | Low | Environment variables `%APPDATA%`/`HOME` are unavailable or sandbox-restricted. Use `SDL_GetPrefPath()` or `ApplicationData::Current->LocalFolder`. |
| **ROM / BIOS Load** | File path via CLI or file picker | Same | Sandboxed; local stage or picker | **Adapt** | Low | Tester stages ROM/BIOS into `LocalState` via Xbox Device Portal or picks via in-engine browser. |
| **Save / Savestate** | Files under user save directory / config | Files beside exe or `%LOCALAPPDATA%` | Sandboxed to `LocalState` | **Adapt** | None | Persisted in `LocalState/` (survives app reboots and updates). |
| **Config / Settings** | `.ini` / `.toml` under `$HOME/.config/MZMRecompiled` | `.ini` under `%APPDATA%/MZMRecompiled` | Sandboxed to `LocalState` | **Adapt** | None | Store `mzm-config.ini`, `mzm-keybinds.ini`, `mzm-rom.cfg`, `mzm-bios.cfg` in `LocalState`. |
| **Logs** | `latest.log`, `previous.log` in user log dir | `%LOCALAPPDATA%/MZMRecompiled/logs` | Sandboxed to `LocalState/logs` | **Adapt** | None | Path redirection to `LocalState/logs/`. Redirection via standard file streams. |
| **Clipboard** | `SDL_SetClipboardText` | Same | WinRT requires DataPackage | **Adapt** | Low | SDL2 WinRT video does not hook `SetClipboardText`. Wrap via `Windows::ApplicationModel::DataTransfer::Clipboard`. |
| **Browser / URLs** | `SDL_OpenURL` (xdg-open) | `SDL_OpenURL` (ShellExecuteW) | Fully compatible | **Reuse** | None | SDL2 WinRT implements `SDL_SYS_OpenURL` using `Windows::System::Launcher::LaunchUriAsync`. |
| **Support Module** | Full (copy info, open folder, open file, open url) | Full (ShellExecuteW folder/file) | Partial / In-App | **Adapt** | None | Disable "Open Folder" / "Open latest.log in Notepad" (no desktop Explorer on Xbox). Show logs in-engine. |
| **Crash Handling** | POSIX signals (`SIGSEGV`, `SIGABRT`) + crash marker | Win32 `SetUnhandledExceptionFilter` | WinRT SEH / C++ unhandled | **Adapt** | Low | `SetUnhandledExceptionFilter` restricted in UWP. Use `__try/__except` or `CoreApplication::UnhandledErrorDetected`. Mark partial for initial milestone. |
| **Display Refresh** | `SDL_GetCurrentDisplayMode` | Same | Fully compatible | **Reuse** | None | Mode queries supported in SDL WinRT. |
| **Alt+Enter** | SDL window toggle | Same | Not applicable on Xbox | **Adapt** | None | UWP apps on Xbox run dedicated full-screen CoreWindow. |
| **File Picker** | Zenity / KDialog / tinyfiledialogs | Win32 `GetOpenFileNameW` (tinyfd) | Win32 dialogs forbidden | **Replace** | Medium | Replace desktop dialogs with in-engine ImGui file browser (`draw_builtin_rom_picker_contents`) or `Windows::Storage::Pickers::FileOpenPicker`. |

---

## 2. Core Separation: Guest vs Host Layer

The architecture of MZM Recompiled enforces a clean boundary between the guest emulation core and the host platform:

```
┌────────────────────────────────────────────────────────┐
│                      GUEST CORE                        │
│  - Generated Recompiled Units (recompiled_000..015)    │
│  - Static Dispatch Table (dispatch_table.cpp)          │
│  - Symbol Mapping (symbol_map.cpp)                     │
│  - RAM Hooks & Resolvers (mzm_ram_dispatch, chozodia)  │
│  - GBA Core Hardware (ARMv4T, PPU, APU, Timers, DMA)   │
│  - Strict-Static Dispatch Validator                    │
└──────────────────────────┬─────────────────────────────┘
                           │ Pure C++ API (no OS calls)
                           ▼
┌────────────────────────────────────────────────────────┐
│                      HOST RUNTIME                      │
│  - Runtime State & Orchestrator (runtime.cpp)          │
│  - Presentation Window (host_window.cpp)               │
│  - SDL2 Abstraction Layer (Video, Audio, Input)        │
│  - UI & Menus (recomp-ui / Dear ImGui)                 │
│  - Filesystem & Diagnostics (mzm_support, mzm_log)     │
└──────────────────────────┬─────────────────────────────┘
                           │ Platform Adapters
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
   │    Linux    │  │ Win32 (x64) │  │  UWP (x64)  │
   │  X11/Wayland│  │ Desktop API │  │ WinRT / Xbox│
   └─────────────┘  └─────────────┘  └─────────────┘
```

**Key Findings:**
1. Guest code has **zero** platform dependencies. It executes purely in guest memory structs (`gba::GbaMem`, `arm::ArmCpu`).
2. Guest execution has **no JIT** and **no runtime dynamic code generation**. Every instruction is compiled AOT by the host compiler (MSVC/Clang/GCC).
3. The boundary is purely procedural (`gbarecomp::run_game(argc, argv, opts)`).

---

## 3. SDL2 WinRT Audit

- **Audited Version**: SDL 2.30.0 (and 2.32.x source trees).
- **WinRT Sources Present**:
  - `src/core/winrt/SDL_winrtapp_direct3d.cpp`: Full `CoreApplication` / `IFrameworkView` lifecycle.
  - `src/main/winrt/SDL_winrt_main_NonXAML.cpp`: WinMain entry point forwarding to `SDL_WinRTRunApp(SDL_main, NULL)`.
  - `src/video/winrt/`: WinRT `CoreWindow` video driver, orientation, and mouse/touch handling.
  - `src/joystick/windows/SDL_windows_gaming_input.c`: Windows.Gaming.Input (WGI) backend with native Xbox controller mapping.
  - `src/audio/wasapi/SDL_wasapi_winrt.cpp`: Native low-latency WASAPI audio backend.
  - `src/filesystem/winrt/SDL_sysfilesystem.cpp`: `SDL_GetPrefPath` mapping to `ApplicationData::Current->LocalFolder`.
  - `src/misc/winrt/SDL_sysurl.cpp`: `SDL_OpenURL` mapping to `Windows::System::Launcher::LaunchUriAsync`.
- **Architectures**: Fully supports `x64` (`Debug|x64`, `Release|x64`).
- **Visual Studio Project**: `VisualC-WinRT/SDL-UWP.vcxproj` provided in SDL2 distribution.

---

## 4. Entry Point & Application Lifecycle

WinRT applications do not start from standard `int main(int argc, char** argv)`.
Instead, the entry sequence is:

1. `int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)` in `SDL_winrt_main_NonXAML.cpp`.
2. Calls `SDL_WinRTRunApp(SDL_main, NULL)`.
3. Initializes Windows Runtime multithreaded apartment: `Windows::Foundation::Initialize(RO_INIT_MULTITHREADED)`.
4. Registers `SDLApplicationSource` (`IFrameworkViewSource`) with `CoreApplication::Run()`.
5. On the UI thread, `SDL_WinRTApp::Run()` starts the event dispatcher and launches `SDL_main(argc, argv)` on a background thread.
6. The app's `SDL_PollEvent(&event)` internally triggers `CoreWindow->Dispatcher->ProcessEvents()`.

**Recommendation**: Use the official SDL2 WinRT Non-XAML entry point. Do not construct a custom WinRT framework.

---

## 5. Renderer Audit: GBARecomp & MZM Launcher

### Game Runtime (`host_window.cpp`)
- **API**: Uses `SDL_Renderer`!
- **Textures**: Creates `SDL_Texture` with `SDL_PIXELFORMAT_RGBA8888` (streaming and target access).
- **Framebuffer Upload**: Updates texture per scanline or per frame (`SDL_UpdateTexture`).
- **CRT Filter**: Implemented as "CRT Lite", an alpha-blended texture overlay (`scanline_texture`). No custom desktop GLSL shaders required.
- **In-Game Menu**: Renders through `ImGui_ImplSDLRenderer2`.
- **WinRT Backend**: Under WinRT, `SDL_Renderer` automatically selects the **Direct3D 11** backend (`src/render/direct3d11/SDL_render_d3d11.c`).

### Pre-Boot Launcher (`game_launcher_boot.cpp`)
- **Current Desktop API**: OpenGL 3.3 Core profile (`SDL_GL_*`, `ImGui_ImplOpenGL3`).
- **Textures**: `launcher_texture_load()` in `launcher_gl.c` uses basic OpenGL calls (`glGenTextures`, `glTexImage2D`).

---

## 6. ANGLE vs Direct3D 11 Evaluation

| Criterion | Option A: ANGLE (OpenGL ES 2.0 / 3.0) | Option B: Direct3D 11 (SDL_Renderer) |
| :--- | :--- | :--- |
| **Implementation Effort** | Medium: requires integrating ANGLE EGL/GLES headers & libs | Low: GBARecomp game runtime is **already** `SDL_Renderer` (D3D11) |
| **recomp-ui Compatibility** | Native: recomp-ui already has `LNG_GLES2` switch | Native: recomp-ui has `ImGui_ImplSDLRenderer2` backend |
| **External Dependencies** | High: requires building and packaging `libEGL.dll` and `libGLESv2.dll` for UWP x64 | **Zero**: Direct3D 11 is built into Windows/Xbox OS; `SDL_Renderer` is in SDL2 |
| **Shader Reuse** | Minimal benefit: neither MZM nor GBARecomp uses custom fragment shaders | Native: scanline texture blend works out of the box |
| **High Refresh Support** | Good via EGL swap interval | Excellent via DXGI swap chain (`IDXGISwapChain1`) |
| **Maintenance** | Medium: must maintain ANGLE submodule/binaries | Very Low: 100% native Microsoft DirectX stack |
| **Xbox Compatibility** | Works, but ANGLE on Xbox UWP has occasional driver quirks | **First-class native Xbox API** |

### Selected Renderer Path: `D3D11_PROTOTYPE`
**Technical Justification**:
The game engine itself (`host_window.cpp`) **already runs on `SDL_Renderer`**, which maps directly to **Direct3D 11** under UWP.
Using ANGLE would introduce two heavyweight third-party DLLs (`libEGL.dll`, `libGLESv2.dll`) solely for the pre-boot launcher.
Adapting `src/game_launcher_boot.cpp` to use `SDL_Renderer` + `ImGui_ImplSDLRenderer2` unifies the entire application stack onto Direct3D 11, eliminates all OpenGL dependencies, and guarantees flawless native execution on Xbox Series X|S.

---

## 7. Filesystem & Storage Sandbox Architecture

Under UWP, access to `C:\`, user home folders, and desktop paths is forbidden.

```
Xbox UWP Sandbox Structure:
LocalState/
  ├── configs/
  │     ├── mzm-config.ini
  │     ├── mzm-keybinds.ini
  │     ├── mzm-rom.cfg
  │     └── mzm-bios.cfg
  ├── saves/
  │     └── Metroid - Zero Mission (USA).sav
  ├── savestates/
  │     └── *.ss*
  ├── logs/
  │     ├── latest.log
  │     ├── previous.log
  │     └── last-crash.log
  ├── roms/
  │     └── Metroid - Zero Mission (USA).gba
  └── bios/
        └── gba_bios.bin
```

- **Base Directory**: `SDL_GetPrefPath(NULL, "MZMRecompiled")` or `ApplicationData::Current->LocalFolder->Path`.
- **Persistence Guarantee**:
  - App Reboots: `LocalState` is persistent.
  - Package Updates (same Identity Name & Publisher): `LocalState` is **retained**.
  - App Uninstallation: `LocalState` is wiped.
  - Development Redeployment: `LocalState` is preserved when updating package files.

---

## 8. ROM & BIOS User Experience (Dev Mode)

To comply with legal requirements:
- **NO ROM** and **NO BIOS** will ever be embedded in the package.

**Tester Ingestion Options:**
1. **Xbox Device Portal (Primary / Recommended)**:
   - The tester opens `https://<xbox-ip>:11443` on their PC browser.
   - Navigates to **File Explorer** -> `DevelopmentFiles/LocalAppData/MZMRecompiled_<id>/LocalState/`.
   - Uploads their legal `Metroid - Zero Mission (USA).gba` and `gba_bios.bin`.
   - On boot, MZM Recompiled automatically checks `LocalState/rom.gba` and `LocalState/gba_bios.bin`.
2. **In-Engine File Browser (`BuiltinRomPicker`)**:
   - recomp-ui contains `draw_builtin_rom_picker_contents()` which navigates directories via D-pad and controller.
   - Can browse mounted USB drives (`D:\`, `E:\`) or `LocalState`.

---

## 9. Controller-First Navigation

Xbox operates without keyboard or mouse:
- **Navigation Controls**:
  - D-Pad / Left Stick: Navigate UI elements (buttons, sliders, tabs).
  - A button: Activate / Select / Confirm.
  - B button: Back / Cancel.
  - Start / Menu button: Pause / Enhancements Menu.
  - Guide button: System OS overlay.
- **Dear ImGui Navigation**: Enabled via `ImGuiConfigFlags_NavEnableGamepad`.

---

## 10. Executable Memory & JIT Audit

```
HOST_EXECUTABLE_MEMORY_REQUIRED = NO
```
- **VirtualAlloc(PAGE_EXECUTE_READWRITE)**: NOT USED.
- **mmap(PROT_EXEC)**: NOT USED.
- **Dynamic Code Generation**: NONE.
- **Self-Healing / JIT**: Disabled in release builds (`GBARECOMP_STRICT_STATIC=1`).
- All code is compiled into native PE `.text` machine code. Fully compatible with Xbox UWP W^X security policies.
