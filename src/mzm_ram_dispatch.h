#pragma once

#include <cstddef>
#include <cstdint>

// Install MZM-specific dynamic RAM dispatch support.
//
// The hook recognizes byte-identical copies of two transient stack-local
// SRAM helpers and the seven hazeCode images (six called from RAM).
void mzm_install_ram_dispatch_hook();

// Opt-in M4 diagnostics; called after run_game returns. Silent by default.
void mzm_report_ram_dispatch();

// Test/diagnostic counters for the Chozodia byte gate.
void mzm_chozodia_dispatch_counts(std::uint64_t& attempts,
                                  std::uint64_t& matches);

// Test/diagnostic counters for the NES payload byte gate and post-payload frontier.
void mzm_nes_payload_dispatch_counts(std::uint64_t& attempts,
                                     std::uint64_t& matches);
std::uint64_t mzm_nes_payload_frontier_hits();
void mzm_set_nes_payload_frontier_stop(bool stop);

// NES emulator Parts 1..6 (M4 NES-2). Counters are kept per Part and are
// separate from the NES payload counters above.
struct mzm_nes_emulator_part_stats_t {
    const char* name = nullptr;
    std::uint64_t attempts = 0;          // PC inside the Part's address range
    std::uint64_t verified = 0;          // live bytes matched the gate digest
    std::uint64_t matches = 0;           // dispatched to the exact private entry
    std::uint64_t verify_failures = 0;   // bytes did not match (fell through)
    std::uint64_t invoke_failures = 0;   // verified, but no private entry
    std::uint64_t no_corpus = 0;         // verified Part with no AOT corpus
};
size_t mzm_nes_emulator_part_stats(mzm_nes_emulator_part_stats_t* out,
                                   size_t capacity);

// Opt-in observer for a verified emulator PC that has no native entry. Used by
// tests to stop at the first real unsupported frontier; it may throw to unwind
// the native call chain. With no observer installed the transfer is a normal
// dispatch miss and Part 2 (no corpus) is not classified at all.
struct mzm_nes_emulator_event_t {
    std::uint32_t pc;
    int thumb;
    const char* part;
    const char* reason;
};
void mzm_set_nes_emulator_frontier_hook(
    void (*hook)(const mzm_nes_emulator_event_t&));

// First transitions between emulator Parts observed at dispatch boundaries
// (cross-image calls/returns). Direct calls inside one Part do not cross the
// hook and are not listed.
struct mzm_nes_emulator_transition_t {
    std::uint32_t pc;
    int thumb;
    const char* from;   // null for the first verified Part
    const char* to;
};
size_t mzm_nes_emulator_transitions(mzm_nes_emulator_transition_t* out,
                                    size_t capacity);
