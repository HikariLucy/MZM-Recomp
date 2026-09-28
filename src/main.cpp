#include <cstdio>
#include <cstring>

#include "runtime.h"
#include "mzm_ram_dispatch.h"

namespace {

void print_usage() {
    std::printf(
        "MZMRecomp --bios <gba_bios.bin> --rom <Metroid Zero Mission USA.gba> "
        "--config <mzm-us.toml> [runtime options]\n"
        "The ROM must match SHA-1 "
        "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8.\n");
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
    opts.builtin_game_name = "Metroid: Zero Mission";
    opts.builtin_rom_sha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8";
    opts.launcher_region = "USA";

    return gbarecomp::run_game(argc, argv, opts);
}
