#pragma once

#include <filesystem>

namespace mzm {
void log_event(const char* event);
// Per-user log directory (Windows: %LOCALAPPDATA%\MZMRecompiled\logs,
// Linux: $XDG_STATE_HOME or ~/.local/state, then MZMRecompiled/logs).
std::filesystem::path log_directory();
// Send stdout/stderr (runtime banners, renderer, display mode, strict-static
// counters, fatal reasons) to logs/latest.log, truncated per run. On Windows
// the streams are redirected; on Linux they are copied (tee) so terminal output
// and test harnesses keep working. Also installs a last-gasp crash marker.
void redirect_console_to_log();
}
