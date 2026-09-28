#pragma once

#include <string>
#include <vector>

#include "runtime.h"

// Run the MZM pre-boot launcher. Returns 0 for PLAY or CLI bypass, 1 when the
// user closes it, and 2 on launcher failure. PLAY appends ordinary runtime CLI
// arguments without changing the backend.
int game_launcher_preboot(std::vector<std::string>& args,
                          const gbarecomp::RunOptions& opts);
