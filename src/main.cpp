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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    mzm_install_ram_dispatch_hook();
    mzm::log_event("start");

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

    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) {
        av.push_back(arg.data());
    }

    // GBARecomp creates a fresh SDL window. Apply the same native icon when
    // SDL announces that window, without modifying the runtime's window code.
    const auto icon_path = std::filesystem::absolute(args.front()).parent_path()
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
    if (const char* strict = std::getenv("GBARECOMP_STRICT_STATIC"); strict && *strict == '1')
        mzm::log_event(rc == 0 ? "cpu_backend=static-recompiled strict_result=ok"
                               : "cpu_backend_request=static-recompiled strict_result=error");
    mzm::log_event(rc == 0 ? "closed result=ok" : "closed result=error");
    return rc;
}
