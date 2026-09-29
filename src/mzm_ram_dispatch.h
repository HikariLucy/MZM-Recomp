#pragma once

// Install MZM-specific dynamic RAM dispatch support.
//
// The hook recognizes byte-identical copies of two transient stack-local
// SRAM helpers and the seven hazeCode images (six called from RAM).
void mzm_install_ram_dispatch_hook();
