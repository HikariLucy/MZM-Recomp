#include "launcher_state.h"

#include <cstdlib>
#include <fstream>
#include <vector>

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
    if (const char* appdata = std::getenv("APPDATA")) return fs::path(appdata);
#endif
    if (const char* value = std::getenv(xdg); value && *value) return fs::path(value);
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / fallback;
    return fs::temp_directory_path();
}
}  // namespace

bool LauncherState::ready() const { return file_exists(rom) && file_exists(bios); }

bool LauncherState::save(const fs::path& path) const {
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "rom=" << rom << '\n' << "bios=" << bios << '\n';
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp);
    return !ec;
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

fs::path user_config_dir() { return user_base("XDG_CONFIG_HOME", ".config") / "MZMRecompiled"; }
fs::path user_log_dir() { return user_base("XDG_STATE_HOME", ".local/state") / "MZMRecompiled/logs"; }

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
