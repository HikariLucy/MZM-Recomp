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

std::filesystem::path user_config_dir();
std::filesystem::path user_log_dir();
std::filesystem::path resolve_game_config(const std::filesystem::path& executable);
// Empty string means the selected file matches the supported image.
std::string validate_game_file(const std::filesystem::path& path);
std::string validate_bios(const std::filesystem::path& path);

}  // namespace mzm
