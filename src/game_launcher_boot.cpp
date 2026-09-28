// Isolated recomp-ui launcher wrapper.
//
// Keeping this in its own static library prevents RECOMP_LAUNCHER and the UI
// dependency graph from touching the large generated MZM translation units.

#include "launcher_seam.h"
#include "game_launcher_boot.h"

int game_launcher_preboot(std::vector<std::string>& args,
                          const gbarecomp::RunOptions& opts) {
    return gbarecomp_launcher_preboot(args, opts);
}
