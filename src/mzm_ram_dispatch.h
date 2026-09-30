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

// Per verification scope (NES-3b), counted by the scope that owns the entry PC.
// verify_failures: the entry scope's own bytes did not match;
// dependency_failures: the entry scope matched but a scope in its requires
// closure did not. Both fall through (fail closed) and both are also counted
// in the Part's verify_failures.
struct mzm_nes_emulator_scope_stats_t {
    const char* part = nullptr;
    const char* scope = nullptr;
    std::uint64_t attempts = 0;
    std::uint64_t verified = 0;
    std::uint64_t matches = 0;
    std::uint64_t verify_failures = 0;
    std::uint64_t dependency_failures = 0;
};
// Fills one row per (Part, scope) in Part/scope order; returns the row count.
size_t mzm_nes_emulator_scope_stats(mzm_nes_emulator_scope_stats_t* out,
                                    size_t capacity);

// Code copied to the stack (SramWriteUnchecked/SramCheck helpers), NES-3c.
// attempts: Thumb entries in the stack window tried against this helper;
// matches: live bytes equal the ROM source (native run); mirror_matches: the
// subset entered through an IWRAM alias above 0x0300FFFF (e.g. 0x03827110).
struct mzm_stack_helper_stats_t {
    const char* name = nullptr;
    std::uint64_t attempts = 0;
    std::uint64_t matches = 0;
    std::uint64_t mirror_matches = 0;
};
size_t mzm_stack_helper_stats(mzm_stack_helper_stats_t* out, size_t capacity,
                              std::uint64_t* rejects);

// Optional read-only observer around each helper run: called once at entry
// (exit=false, the argument registers) and once after the native body returns
// (exit=true, r0 = return value).
struct mzm_stack_helper_event_t {
    const char* name;
    std::uint32_t pc;
    bool exit;
    std::uint32_t r0, r1, r2, r3, sp, lr;
};
// Lowest host stack address (frame of this hook) seen since install; ~0 if none.
std::uintptr_t mzm_ram_dispatch_stack_low();

void mzm_set_stack_helper_observer(void (*observer)(const mzm_stack_helper_event_t&));

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
