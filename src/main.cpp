#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "runtime.h"
#include "mzm_ram_dispatch.h"
#include "mzm_milestone_probe.h"
#include "mzm_log.h"

#if defined(MZM_RECOMP_UI)
#include "game_launcher_boot.h"
#include "launcher_state.h"
#include "windows_executable_path.h"
#include <SDL.h>
#endif

namespace {

#if defined(MZM_RECOMP_UI)
struct RuntimeIcon {
    SDL_Surface* surface = nullptr;
    std::atomic<bool> applied{false};
};

int apply_runtime_icon(void* userdata, SDL_Event* event) {
    if (event->type != SDL_WINDOWEVENT || event->window.event != SDL_WINDOWEVENT_SHOWN)
        return 0;
    auto* icon = static_cast<RuntimeIcon*>(userdata);
    if (SDL_Window* window = SDL_GetWindowFromID(event->window.windowID)) {
        SDL_SetWindowIcon(window, icon->surface);
        if (!icon->applied.exchange(true)) mzm::log_event("game_window_icon=applied");
    }
    return 0;
}
#endif

void print_usage() {
    std::printf(
        "MZMRecomp [--bios <gba_bios.bin>] [--rom <Metroid Zero Mission USA.gba>] "
        "[--config <mzm-us.toml>] [runtime options]\n"
        "The ROM must match SHA-1 "
        "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8.\n"
        "With the optional MZM launcher build, launch with no ROM argument or "
        "--launcher to open graphical setup. --no-launcher skips it.\n");
}

}  // namespace

#if defined(MZM_PLATFORM_UWP)
extern "C" int SDL_main(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    mzm::bootstrap_local_directories();
    mzm::redirect_console_to_log();

    // Release builds run strict-static by default: a missing static entry stops with an
    // error instead of silently compiling or interpreting code at run time. Setting
    // GBARECOMP_STRICT_STATIC explicitly (e.g. =0 for development) is still honoured.
    if (!std::getenv("GBARECOMP_STRICT_STATIC")) {
#ifdef _WIN32
        _putenv_s("GBARECOMP_STRICT_STATIC", "1");
#else
        setenv("GBARECOMP_STRICT_STATIC", "1", 1);
#endif
    }
    mzm_install_ram_dispatch_hook();
    mzm::log_event("start");
    mzm::log_event((std::string("previous_session=") +
                    mzm::session_status_name(mzm::previous_session_status())).c_str());

    gbarecomp::RunOptions opts;
    mzm_configure_milestone_probe(opts);

    opts.builtin_game_name = "Metroid: Zero Mission";
    opts.builtin_rom_sha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8";
    opts.launcher_region = "USA";

    // Host-only quality-of-life surfaces. These do not alter guest ROM/RAM
    // behavior and remain separable from the faithful strict-static path.
    opts.freely_resizable_window = true;
    opts.expose_assist_tools = true;
    opts.assist_tools_enabled_by_default = true;
    opts.assist_fast_forward_multiplier_default = 4;
    opts.save_state_slot_count = 9;
    opts.rewind_history_seconds = 15;
    opts.rewind_capture_interval_frames = 15;

    // ESC opens the in-game Enhancements menu (display, graphics, performance).
    // Presentation only: guest timing stays at the GBA's 59.7275 Hz, and the
    // guest is held still while the menu is open.
    opts.expose_display_enhancements = true;
    opts.pause_when_menu_open = true;
    opts.runtime_menu_title = "MZM Recompiled";
    opts.runtime_menu_subtitle = "Enhancements";

    // Keep compatibility with GBARecomp's existing per-game host filenames.
    // The MZM launcher stores ROM/BIOS path references in the user config dir.
    opts.launcher_config_filename = "mzm-config.ini";
    opts.launcher_keybinds_filename = "mzm-keybinds.ini";
    opts.launcher_rom_cache_filename = "mzm-rom.cfg";
    opts.launcher_bios_cache_filename = "mzm-bios.cfg";

    int rc = 0;

#if defined(MZM_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    const int launcher_result = game_launcher_preboot(args, opts);
    if (launcher_result != 0) {
        mzm::log_event(launcher_result == 1 ? "launcher_closed" : "launcher_error");
        return launcher_result == 1 ? 0 : 1;
    }

#if defined(MZM_PLATFORM_UWP)
    const auto config_dir = mzm::user_config_dir();
    std::error_code config_error;
    std::filesystem::create_directories(config_dir, config_error);
    const std::string uwp_save = (mzm::user_saves_dir() / "Metroid - Zero Mission (USA).sav").string();
    opts.launcher_save_path = uwp_save.c_str();
    args.insert(args.end(), {"--save", uwp_save});
    args.front() = (config_dir / "MZMRecomp.exe").string();
#elif defined(_WIN32)
    // GBARecomp resolves input config and sidecar caches relative to argv[0].
    // Point that host convention at the writable user configuration directory.
    const auto config_dir = mzm::user_config_dir();
    std::error_code config_error;
    std::filesystem::create_directories(config_dir, config_error);
    if (config_error) {
        mzm::log_event("launcher_error=config_directory");
        return 1;
    }
    args.front() = (config_dir / "MZMRecomp.exe").string();
#endif

    // Startup diagnostics for bug reports. Results only: never file contents or paths.
    {
        auto arg_after = [&](const char* flag) -> const char* {
            for (size_t i = 1; i + 1 < args.size(); ++i)
                if (args[i] == flag) return args[i + 1].c_str();
            return nullptr;
        };
        const char* rom = arg_after("--rom");
        const char* bios = arg_after("--bios");
        mzm::log_event(!rom ? "rom_validation=not_selected"
                       : mzm::validate_game_file(rom).empty() ? "rom_validation=ok sha1=expected-usa-rev0"
                                                              : "rom_validation=FAILED");
        mzm::log_event(!bios ? "bios_validation=not_selected"
                       : mzm::validate_bios(bios).empty() ? "bios_validation=ok sha1=expected"
                                                          : "bios_validation=FAILED");
        const char* strict = std::getenv("GBARECOMP_STRICT_STATIC");
        mzm::log_event(strict && *strict == '1' ? "strict_static=requested"
                                                : "strict_static=not_requested");
    }

    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) {
        av.push_back(arg.data());
    }

    // GBARecomp creates a fresh SDL window. Apply the same native icon when
    // SDL announces that window, without modifying the runtime's window code.
    const auto icon_path =
#ifdef _WIN32
        mzm::executable_path().parent_path()
#else
        std::filesystem::absolute(args.front()).parent_path()
#endif
        / "assets/icons/mzm-recompiled.bmp";
    RuntimeIcon icon;
    icon.surface = SDL_LoadBMP(icon_path.string().c_str());
    if (icon.surface) SDL_AddEventWatch(apply_runtime_icon, &icon);
    rc = gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
    if (icon.surface) {
        SDL_DelEventWatch(apply_runtime_icon, &icon);
        SDL_FreeSurface(icon.surface);
        if (!icon.applied) mzm::log_event("game_window_icon=not_observed");
    }
#else
    rc = gbarecomp::run_game(argc, argv, opts);
#endif

    mzm_report_milestone_probe();
    mzm_report_ram_dispatch();
    if (const char* strict = std::getenv("GBARECOMP_STRICT_STATIC"); strict && *strict == '1')
        mzm::log_event(rc == 0 ? "cpu_backend=static-recompiled strict_result=ok"
                               : "cpu_backend_request=static-recompiled strict_result=error");
    mzm::log_event(rc == 0 ? "closed result=ok" : "closed result=error");
    return rc;
}
