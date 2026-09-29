#pragma once

// Install MZM-specific dynamic RAM dispatch support.
//
// The hook recognizes byte-identical copies of two transient stack-local
// SRAM helpers and the seven hazeCode images (six called from RAM).
void mzm_install_ram_dispatch_hook();

// Opt-in M4 diagnostics; called after run_game returns. Silent by default.
void mzm_report_ram_dispatch();
