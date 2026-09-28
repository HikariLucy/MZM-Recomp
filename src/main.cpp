#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"
#include "mzm_ram_dispatch.h"
#include "mzm_milestone_probe.h"

#if defined(MZM_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {

void print_usage() {
    std::printf(
        "MZMRecomp [--bios <gba_bios.bin>] [--rom <Metroid Zero Mission USA.gba>] "
        "[--config <mzm-us.toml>] [runtime options]\n"
        "The ROM must match SHA-1 "
        "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8.\n"
        "With the optional recomp-ui build, launch with no ROM argument or "
        "--launcher to open the graphical setup.\n");
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

    // Keep MZM's player-owned launcher state isolated and portable beside the
    // executable. No ROM/BIOS/save content is copied into the repository.
    opts.launcher_config_filename = "mzm-config.ini";
    opts.launcher_keybinds_filename = "mzm-keybinds.ini";
    opts.launcher_rom_cache_filename = "mzm-rom.cfg";
    opts.launcher_bios_cache_filename = "mzm-bios.cfg";

    int rc = 0;

#if defined(MZM_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    if (game_launcher_preboot(args, opts)) {
        return 0;
    }

    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) {
        av.push_back(arg.data());
    }

    rc = gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    rc = gbarecomp::run_game(argc, argv, opts);
#endif

    mzm_report_milestone_probe();
    return rc;
}
