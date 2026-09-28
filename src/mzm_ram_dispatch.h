#pragma once

// Install MZM-specific dynamic RAM dispatch support.
//
// The current hook recognizes byte-identical copies of the two position-
// independent SRAM helper routines that the original cartridge copies into
// transient stack-local buffers before calling them.
void mzm_install_ram_dispatch_hook();
