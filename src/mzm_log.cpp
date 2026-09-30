#include "mzm_log.h"

#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>

#ifndef MZM_BUILD_SHA
#define MZM_BUILD_SHA "unknown"
#endif
#ifndef MZM_GBARECOMP_SHA
#define MZM_GBARECOMP_SHA "unknown"
#endif

namespace mzm {

namespace {
std::filesystem::path log_dir() {
    namespace fs = std::filesystem;
    fs::path root;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) root = local;
    else if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) root = appdata;
#endif
    if (root.empty()) {
        if (const char* xdg = std::getenv("XDG_STATE_HOME")) root = xdg;
        else if (const char* home = std::getenv("HOME")) root = fs::path(home) / ".local/state";
        else return {};
    }
    return root / "MZMRecompiled/logs";
}
}  // namespace

void redirect_console_to_log() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    const fs::path dir = log_dir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    const fs::path file = dir / "latest.log";
    if (!std::freopen(file.string().c_str(), "w", stdout)) return;
    if (_dup2(_fileno(stdout), _fileno(stderr)) != 0) return;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("MZMRecompiled version=%s build=%s gbarecomp=%s os=windows-x86_64\n",
                MZM_VERSION, MZM_BUILD_SHA, MZM_GBARECOMP_SHA);
#endif
}

void log_event(const char* event) {
    namespace fs = std::filesystem;
    const fs::path dir = log_dir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    std::ofstream out(dir / "mzm-recompiled.log", std::ios::app);
    if (!out) return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    out << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ")
        << " version=" << MZM_VERSION << " build=" << MZM_BUILD_SHA
        << " gbarecomp=" << MZM_GBARECOMP_SHA << " " << event << '\n';
}

}  // namespace mzm
