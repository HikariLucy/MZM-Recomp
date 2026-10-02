#pragma once

#include <filesystem>
#include <string>

namespace mzm {

struct LauncherState {
    std::string rom;
    std::string bios;

    bool ready() const;
    bool save(const std::filesystem::path& path) const;
    static LauncherState load(const std::filesystem::path& path);
};

struct DiscoveredAssets {
    std::string rom;
    std::string bios;
};

std::filesystem::path user_config_dir();
std::filesystem::path user_log_dir();
std::filesystem::path user_saves_dir();
std::filesystem::path user_roms_dir();
std::filesystem::path user_bios_dir();
void bootstrap_local_directories();
DiscoveredAssets discover_local_assets();

std::filesystem::path resolve_game_config(const std::filesystem::path& executable);
// Empty string means the selected file matches the supported image.
std::string validate_game_file(const std::filesystem::path& path);
std::string validate_bios(const std::filesystem::path& path);

}  // namespace mzm
