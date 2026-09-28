#include "mzm_log.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace mzm {

void log_event(const char* event) {
    namespace fs = std::filesystem;
    fs::path root;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) root = appdata;
#endif
    if (root.empty()) {
        if (const char* xdg = std::getenv("XDG_STATE_HOME")) root = xdg;
        else if (const char* home = std::getenv("HOME")) root = fs::path(home) / ".local/state";
        else return;
    }
    const fs::path dir = root / "MZMRecompiled/logs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    std::ofstream out(dir / "mzm-recompiled.log", std::ios::app);
    if (!out) return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    out << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ")
        << " version=" << MZM_VERSION << " " << event << '\n';
}

}  // namespace mzm
