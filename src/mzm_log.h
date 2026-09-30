#pragma once

namespace mzm {
void log_event(const char* event);
// Windows only: send stdout/stderr (runtime banners, strict-static counters,
// fatal reasons) to logs/latest.log, truncated per run. No-op elsewhere.
void redirect_console_to_log();
}
