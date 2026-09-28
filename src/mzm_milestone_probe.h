#pragma once

namespace gbarecomp {
struct RunOptions;
}

// Optional M2 observability probe. When MZM_MILESTONE_TRACE=1, installs a
// no-input per-frame callback which enables the generic generated function-entry
// hook and records selected semantic milestones. Default execution is unchanged.
void mzm_configure_milestone_probe(gbarecomp::RunOptions& opts);
void mzm_report_milestone_probe();

// Re-arm the function-entry hook after run_game() has completed its startup
// reset. Safe to call repeatedly; it is a no-op unless tracing is enabled.
void mzm_arm_milestone_entry_hook();
