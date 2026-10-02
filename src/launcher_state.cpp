#include "launcher_state.h"

#include <cstdlib>
#include <fstream>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "sha1.h"

namespace fs = std::filesystem;

namespace mzm {

namespace {
bool file_exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(path, ec);
}

fs::path user_base(const char* xdg, const char* fallback) {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return fs::path(appdata);
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
        return fs::path(local);
    return fs::temp_directory_path();
#else
    if (const char* value = std::getenv(xdg); value && *value) return fs::path(value);
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / fallback;
    return fs::temp_directory_path();
#endif
}
}  // namespace

bool LauncherState::ready() const { return file_exists(rom) && file_exists(bios); }

bool LauncherState::save(const fs::path& path) const {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "rom=" << rom << '\n' << "bios=" << bios << '\n';
        if (!out) return false;
    }
#ifdef _WIN32
    const bool moved = MoveFileExW(tmp.c_str(), path.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!moved) { std::error_code ec; fs::remove(tmp, ec); }
    return moved;
#else
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp);
    return !ec;
#endif
}

LauncherState LauncherState::load(const fs::path& path) {
    LauncherState state;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("rom=", 0) == 0) state.rom = line.substr(4);
        if (line.rfind("bios=", 0) == 0) state.bios = line.substr(5);
    }
    return state;
}

#if defined(MZM_PLATFORM_UWP)
#include <SDL.h>

fs::path uwp_local_state_dir() {
    char* pref = SDL_GetPrefPath(nullptr, "MZMRecompiled");
    if (pref) {
        fs::path p(pref);
        SDL_free(pref);
        return p;
    }
    return fs::current_path();
}

fs::path user_config_dir() { return uwp_local_state_dir() / "configs"; }
fs::path user_log_dir() { return uwp_local_state_dir() / "logs"; }
fs::path user_saves_dir() { return uwp_local_state_dir() / "saves"; }
fs::path user_roms_dir() { return uwp_local_state_dir() / "roms"; }
fs::path user_bios_dir() { return uwp_local_state_dir() / "bios"; }

void bootstrap_local_directories() {
    std::error_code ec;
    fs::create_directories(user_config_dir(), ec);
    fs::create_directories(user_log_dir(), ec);
    fs::create_directories(user_saves_dir(), ec);
    fs::create_directories(uwp_local_state_dir() / "savestates", ec);
    fs::create_directories(user_roms_dir(), ec);
    fs::create_directories(user_bios_dir(), ec);
}

DiscoveredAssets discover_local_assets() {
    DiscoveredAssets res;
    std::error_code ec;
    const auto rdir = user_roms_dir();
    if (fs::is_directory(rdir, ec)) {
        for (const auto& entry : fs::directory_iterator(rdir, ec)) {
            if (entry.is_regular_file(ec)) {
                if (validate_game_file(entry.path()).empty()) {
                    res.rom = entry.path().string();
                    break;
                }
            }
        }
    }
    const auto bdir = user_bios_dir();
    if (fs::is_directory(bdir, ec)) {
        for (const auto& entry : fs::directory_iterator(bdir, ec)) {
            if (entry.is_regular_file(ec)) {
                if (validate_bios(entry.path()).empty()) {
                    res.bios = entry.path().string();
                    break;
                }
            }
        }
    }
    return res;
}
#else
fs::path user_config_dir() { return user_base("XDG_CONFIG_HOME", ".config") / "MZMRecompiled"; }
fs::path user_log_dir() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
        return fs::path(local) / "MZMRecompiled/logs";
#endif
    return user_base("XDG_STATE_HOME", ".local/state") / "MZMRecompiled/logs";
}
fs::path user_saves_dir() { return user_config_dir() / "saves"; }
fs::path user_roms_dir() { return user_config_dir() / "roms"; }
fs::path user_bios_dir() { return user_config_dir() / "bios"; }

void bootstrap_local_directories() {
    std::error_code ec;
    fs::create_directories(user_config_dir(), ec);
    fs::create_directories(user_log_dir(), ec);
}

DiscoveredAssets discover_local_assets() {
    return {};
}
#endif

fs::path resolve_game_config(const fs::path& executable) {
    const auto beside = executable.parent_path() / "configs/mzm-us.toml";
    std::error_code ec;
    if (fs::is_regular_file(beside, ec)) return beside;
    return executable.parent_path().parent_path() / "configs/mzm-us.toml";
}

static std::string digest_file(const fs::path& path, std::uintmax_t expected_size) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) != expected_size || ec)
        return {};
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes(expected_size);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return {};
    uint8_t digest[20];
    char hex[41];
    recompui_sha1_compute(bytes.data(), bytes.size(), digest);
    recompui_sha1_hex(digest, hex);
    return hex;
}

std::string validate_game_file(const fs::path& path) {
    if (!fs::is_regular_file(path)) return "Game file is missing.";
    return digest_file(path, 8388608) == "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8"
        ? "" : "Unsupported game file (expected USA revision 0).";
}

std::string validate_bios(const fs::path& path) {
    if (!fs::is_regular_file(path)) return "GBA BIOS is missing.";
    std::error_code ec;
    if (fs::file_size(path, ec) != 16384 || ec) return "Invalid GBA BIOS (expected 16 KB).";
    return digest_file(path, 16384) == "300c20df6731a33952ded8c436f7f186d25d3492"
        ? "" : "Invalid GBA BIOS checksum.";
}

}  // namespace mzm
