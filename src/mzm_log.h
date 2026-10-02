#pragma once

#include <filesystem>

#include "mzm_diagnostics.h"

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
// How the previous run ended, judged from the old latest.log before it is replaced.
// (That log is kept as previous.log, and as last-crash.log when it ended badly.)
SessionStatus previous_session_status();
}
