#pragma once
// Platform side of the Support module: system queries, clipboard, and opening
// folders / files / URLs. Every action is user-initiated; nothing is uploaded.
// MZM_SUPPORT_TEST_DIR (tests only): instead of opening anything, record the action in
// <dir>/opener.txt, and mirror every clipboard write (with an SDL read-back) in <dir>/clipboard.txt.

#include <filesystem>
#include <string>

#include "mzm_diagnostics.h"

struct SDL_Window;

namespace mzm {

std::string home_directory();                        // "" when unknown
SystemInfo collect_system_info(SDL_Window* window);   // on demand, never per frame
bool clipboard_copy(const std::string& text);
bool open_folder(const std::filesystem::path& dir);
bool open_file(const std::filesystem::path& file);
bool open_url(const std::string& url);
std::string read_text_file(const std::filesystem::path& file, size_t max_bytes);   // tail-limited
std::filesystem::path config_ini_path(const std::filesystem::path& executable);

}  // namespace mzm
