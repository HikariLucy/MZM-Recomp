#pragma once

#include <string>
#include <vector>

#include "runtime.h"

// Run recomp-ui's GBA pre-boot launcher. Returns non-zero when the user chose
// to quit instead of booting the game. Committed launcher settings are appended
// to args as ordinary GBARecomp CLI arguments.
int game_launcher_preboot(std::vector<std::string>& args,
                          const gbarecomp::RunOptions& opts);
