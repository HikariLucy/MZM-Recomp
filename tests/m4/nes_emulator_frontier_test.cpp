// NES-2: strict-static execution of the real NES emulator initialisation.
//
//   0x087D8000 ARM trampoline -> ROM loader -> BIOS LZ77 -> 0x03007400 payload
//   -> 0x06006558 (Part 1) -> cross-image initialisation -> first real frontier
//
// Phase A reproduces NES-1b exactly (stop at 0x06006558) and proves that the
// six emulator images the payload wrote into guest memory are byte-identical
// to the images reconstructed from the legal ROM by
// scripts/extract-nes-emulator.py. Phase B lifts the NES-1b stop and runs
// natively until a verified emulator PC has no native entry. The fixture drives
// the runtime the way runtime.cpp's run loop does (SWI returns and HALT hand
// control back to the outer loop), so "PC unchanged" is not a stall criterion.
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gba_bios.h"
#include "gba_bus.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "mzm_nes_emulator_resolver.h"
#include "mzm_ram_dispatch.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "self_heal.h"
#include "sha1.h"

extern "C" uint32_t g_irq_nest_depth;

#ifndef MZM_NES_EMULATOR_DIR
#define MZM_NES_EMULATOR_DIR ".local/nes-emulator"
#endif

namespace {
constexpr std::uint32_t kTrampoline = 0x087D8000u;
constexpr std::uint32_t kPart1Entry = 0x06006558u;
constexpr const char* kExpectedRomSha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8";
constexpr std::uint64_t kMaxFrames = 600;

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "NES-2 FAIL: %s\n", message);
    std::exit(1);
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

struct EmulatorFrontier {
    mzm_nes_emulator_event_t event;
};

void on_frontier(const mzm_nes_emulator_event_t& e) { throw EmulatorFrontier{e}; }

std::uint64_t g_irq_vector_entries = 0;
void on_function_entry(std::uint32_t pc) {
    if (pc == 0x00000018u) ++g_irq_vector_entries;
}

const char* mode_name() { return (g_cpu.cpsr & CPSR_T_BIT) ? "Thumb" : "ARM"; }

const std::uint8_t* region_ptr(gba::GbaBus& bus, std::uint32_t addr) {
    switch (addr >> 24) {
        case 0x02: return bus.ewram_ptr() + (addr & 0x3FFFFu);
        case 0x03: return bus.iwram_ptr() + (addr & 0x7FFFu);
        case 0x06: return bus.vram_ptr() + (addr - 0x06000000u);
        default: return nullptr;
    }
}

struct PartAudit {
    std::vector<std::uint8_t> pristine;
    std::bitset<0x5A4C> dirty;  // largest Part
};

bool in_runs(const mzm_nes_emulator::CodeRun* runs, std::size_t n, std::uint32_t a) {
    for (std::size_t i = 0; i < n; ++i)
        if (a >= runs[i].start && a < runs[i].end) return true;
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <mzm_usa.gba> <gba_bios.bin> [nes-emulator-dir]\n",
                     argv[0]);
        return 2;
    }
    const std::string emu_dir = argc > 3 ? argv[3] : MZM_NES_EMULATOR_DIR;

    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(input)), {});
    require(rom.size() == 0x800000u, "expected USA 8 MiB cartridge");
    require(gba::sha1(rom.data(), rom.size()).hex() == kExpectedRomSha1,
            "wrong ROM SHA-1: expected USA 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8");

    gba::GbaBios bios;
    std::string error;
    require(bios.load_from_file(argv[2], gba::GbaBios::kExpectedSha1, &error),
            "BIOS hash/load failure");

    // Host-side reference images, reconstructed only from the legal ROM.
    PartAudit audit[mzm_nes_emulator::kNumParts];
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
        const auto& spec = mzm_nes_emulator::kParts[i];
        audit[i].pristine = read_file(emu_dir + "/" + spec.name + ".bin");
        require(audit[i].pristine.size() == spec.size,
                "missing/short local NES image (run scripts/extract-nes-emulator.py)");
    }

    // Direct gate test: exact bytes verify; a flipped gated byte fails closed; a
    // flipped byte inside the proven-mutable exclusion (Part 1, 0x06006700) does not.
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
        const auto& spec = mzm_nes_emulator::kParts[i];
        auto reader = [&](std::int64_t flip) {
            return [&, flip](std::uint32_t a) -> std::uint8_t {
                std::uint8_t b = audit[i].pristine[a - spec.start];
                return static_cast<std::int64_t>(a) == flip ? static_cast<std::uint8_t>(b ^ 0xFF) : b;
            };
        };
        require(mzm_nes_emulator::verify_image(spec, reader(-1)),
                "gate rejects the exact ROM-derived image");
        require(!mzm_nes_emulator::verify_image(spec, reader(spec.gate[0].start)),
                "gate accepts a flipped gated byte");
    }
    {
        const auto& p1 = mzm_nes_emulator::spec_of(mzm_nes_emulator::ImageKind::Part1);
        auto flip_excluded = [&](std::uint32_t a) -> std::uint8_t {
            std::uint8_t b = audit[0].pristine[a - p1.start];
            return a == 0x06006700u ? static_cast<std::uint8_t>(b ^ 0xFF) : b;
        };
        require(mzm_nes_emulator::verify_image(p1, flip_excluded),
                "gate must ignore the proven-mutable Part 1 tilemap bytes");
        auto flip_neighbour = [&](std::uint32_t a) -> std::uint8_t {
            std::uint8_t b = audit[0].pristine[a - p1.start];
            return a == 0x06006702u ? static_cast<std::uint8_t>(b ^ 0xFF) : b;
        };
        require(!mzm_nes_emulator::verify_image(p1, flip_neighbour),
                "gate must still cover the byte after the exclusion");
    }

    gba::GbaBus bus;
    gba::GbaPpu ppu;
    bus.set_rom(rom.data(), rom.size());
    bus.set_bios(&bios);
    // Same wiring as the production run loop (runtime.cpp); without it DMA
    // transfers are inert and guest RAM images are never populated.
    bus.io().set_bus(&bus);
    // MZM USA carries 32 KiB SRAM (fresh cartridge: erased 0xFF). The emulator
    // probes SRAM (0x0E007FB0..) during initialisation.
    bus.save().configure_sram(32 * 1024);
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    gbarecomp::self_heal_reset();

    mzm_install_ram_dispatch_hook();
    g_runtime_fn_entry_hook = on_function_entry;

    // ------------------------------------------------------------------
    // Phase A: NES-1a + NES-1b chain, stopping at 0x06006558.
    // ------------------------------------------------------------------
    mzm_set_nes_payload_frontier_stop(true);
    g_cpu = {};
    g_cpu.cpsr = 0x1Fu;
    g_cpu.R[0] = 0x08000000u;
    g_cpu.R[15] = kTrampoline;

    std::printf("A1. ROM trampoline 0x%08X -> loader -> BIOS LZ77\n", kTrampoline);
    runtime_dispatch(kTrampoline);
    require(g_cpu.R[15] == 0x087D8110u, "LZ77 did not return to loader continuation");
    runtime_dispatch(g_cpu.R[15]);
    for (int step = 0; step < 100; ++step) {
        const std::uint32_t pc = g_cpu.R[15];
        if (pc < 0x03007400u || pc >= 0x03007614u) break;
        runtime_dispatch(pc);
    }
    require(g_cpu.R[15] == kPart1Entry, "payload did not reach 0x06006558");
    require((g_cpu.cpsr & CPSR_T_BIT) == 0u, "expected ARM at 0x06006558");
    require(bus.io().read16(0x204) == 0x0014u, "WAITCNT was not 0x0014");
    require(runtime_call_stack_depth() == 0u, "host return stack not empty at NES-1b frontier");
    require(bus.unmapped_count() == 0u && bus.io().unmapped_count() == 0u,
            "unmapped access before 0x06006558");
    require(!gbarecomp::self_heal_any_misses() &&
            gbarecomp::self_heal_interpreted_insns() == 0u,
            "dispatch miss or interpreted insn before 0x06006558");
    std::printf("A2. NES-1b frontier: PC=0x%08X ARM SP=0x%08X LR=0x%08X CPSR=0x%08X\n",
                g_cpu.R[15], g_cpu.R[13], g_cpu.R[14], g_cpu.cpsr);

    // Oracle: guest bytes written by the payload vs ROM-derived images.
    std::printf("A3. Guest image vs ROM-derived image at the frontier:\n");
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
        const auto& spec = mzm_nes_emulator::kParts[i];
        const std::uint8_t* guest = region_ptr(bus, spec.start);
        const bool match = std::memcmp(guest, audit[i].pristine.data(), spec.size) == 0;
        std::printf("    %s 0x%08X..0x%08X size=0x%zX guest_match=%s\n", spec.name,
                    spec.start, static_cast<std::uint32_t>(spec.start + spec.size),
                    spec.size, match ? "yes" : "NO");
        if (!match) {
            std::size_t shown = 0;
            for (std::size_t o = 0; o < spec.size && shown < 16; ++o)
                if (guest[o] != audit[i].pristine[o]) {
                    std::printf("      diff +0x%zX guest=%02X rom=%02X\n", o, guest[o],
                                audit[i].pristine[o]);
                    ++shown;
                }
        }
        require(match, "guest image differs from ROM-derived image");
    }

    // ------------------------------------------------------------------
    // Phase B: lift the NES-1b stop and run natively to the next frontier.
    // ------------------------------------------------------------------
    mzm_set_nes_payload_frontier_stop(false);
    mzm_set_nes_emulator_frontier_hook(on_frontier);

    auto snapshot_dirty = [&]() {
        for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
            const auto& spec = mzm_nes_emulator::kParts[i];
            const std::uint8_t* guest = region_ptr(bus, spec.start);
            for (std::size_t o = 0; o < spec.size; ++o)
                if (guest[o] != audit[i].pristine[o]) audit[i].dirty.set(o);
        }
    };

    std::uint64_t dispatches = 0, halt_pumps = 0;
    int no_progress = 0;
    bool have_frontier = false;
    EmulatorFrontier frontier{};
    bool stalled = false;
    std::uint32_t stall_pc = 0;

    std::printf("B1. Native execution from 0x%08X until the first unsupported frontier\n",
                kPart1Entry);
    try {
        while (ppu.frame_count() < kMaxFrames) {
            if (bus.io().halted()) {
                // runtime.cpp step_once: advance the clock while halted.
                std::uint32_t budget = gba::GbaPpu::kCyclesPerFrame;
                while (bus.io().halted() && budget != 0) {
                    std::uint32_t chunk = ppu.cycles_until_next_event();
                    const std::uint32_t t = bus.io().cycles_until_next_timer_event();
                    const std::uint32_t a = bus.audio().cycles_until_next_sample();
                    if (t < chunk) chunk = t;
                    if (a < chunk) chunk = a;
                    if (chunk == 0 || chunk == 0xFFFFFFFFu) chunk = 1;
                    if (chunk > budget) chunk = budget;
                    runtime_tick(chunk);
                    budget -= chunk;
                    ++halt_pumps;
                }
                snapshot_dirty();
                continue;
            }
            const std::uint32_t pc = g_cpu.R[15];
            const auto before = g_cpu;
            const auto cycles_before = g_runtime_cycles;
            runtime_dispatch(pc);
            ++dispatches;
            snapshot_dirty();
            if (gbarecomp::self_heal_any_misses()) {
                stalled = true;
                stall_pc = pc;
                break;
            }
            // A guest spin (e.g. a BIOS wait loop) leaves the CPU state
            // unchanged but consumes time; only a dispatch that changes neither
            // the state nor the clock is a stall.
            // A VBlank present-yield is itself a no-op dispatch (the yield
            // latches, so the next dispatch proceeds); require several in a row.
            if (std::memcmp(&before, &g_cpu, sizeof(g_cpu)) == 0 &&
                g_runtime_cycles == cycles_before && !bus.io().halted()) {
                ++no_progress;
            } else {
                no_progress = 0;
            }
            if (no_progress >= 4) {
                stalled = true;
                stall_pc = pc;
                break;
            }
        }
    } catch (const EmulatorFrontier& f) {
        have_frontier = true;
        frontier = f;
        snapshot_dirty();
    }

    // ------------------------------------------------------------------
    // Report.
    // ------------------------------------------------------------------
    mzm_nes_emulator_part_stats_t stats[mzm_nes_emulator::kNumParts];
    mzm_nes_emulator_part_stats(stats, mzm_nes_emulator::kNumParts);
    std::printf("B2. Per-Part dispatch counters (attempts/verified/matches/verify_fail/invoke_fail/no_corpus):\n");
    std::uint64_t total_matches = 0;
    for (const auto& st : stats) {
        total_matches += st.matches;
        std::printf("    %s %llu/%llu/%llu/%llu/%llu/%llu\n", st.name,
                    (unsigned long long)st.attempts, (unsigned long long)st.verified,
                    (unsigned long long)st.matches, (unsigned long long)st.verify_failures,
                    (unsigned long long)st.invoke_failures, (unsigned long long)st.no_corpus);
    }

    mzm_nes_emulator_transition_t trans[64];
    const std::size_t nt = mzm_nes_emulator_transitions(trans, 64);
    std::printf("B3. Cross-image transitions (%zu):\n", nt);
    for (std::size_t i = 0; i < nt; ++i)
        std::printf("    #%zu PC=0x%08X %s  %s -> %s\n", i + 1, trans[i].pc,
                    trans[i].thumb ? "Thumb" : "ARM", trans[i].from ? trans[i].from : "(entry)",
                    trans[i].to);

    std::uint64_t pay_att = 0, pay_match = 0;
    mzm_nes_payload_dispatch_counts(pay_att, pay_match);
    std::printf("B4. NES payload gate (separate counters): attempts=%llu matches=%llu\n",
                (unsigned long long)pay_att, (unsigned long long)pay_match);

    std::printf("B5. Mutability audit over %llu top-level steps (guest vs ROM-derived image):\n",
                (unsigned long long)(dispatches + halt_pumps));
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
        const auto& spec = mzm_nes_emulator::kParts[i];
        std::size_t total = 0, in_code = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
        for (std::size_t o = 0; o < spec.size; ++o) {
            if (!audit[i].dirty.test(o)) continue;
            ++total;
            const std::uint32_t a = spec.start + static_cast<std::uint32_t>(o);
            const bool code = in_runs(mzm_nes_emulator::code_runs(spec.kind),
                                      mzm_nes_emulator::code_run_count(spec.kind), a);
            if (code) ++in_code;
            if (!ranges.empty() && ranges.back().second == a) ranges.back().second = a + 1;
            else ranges.push_back({a, a + 1});
        }
        const char* cls = total == 0 ? "IMMUTABLE AFTER LOAD (observed)"
                          : in_code == 0 ? "MUTABLE DATA ONLY"
                                         : "EXECUTABLE OVERLAY / SELF-MODIFYING";
        std::printf("    %s: %zu dirty bytes (%zu in code runs) => %s\n", spec.name, total,
                    in_code, cls);
        for (std::size_t r = 0; r < ranges.size() && r < 12; ++r)
            std::printf("        dirty [0x%08X,0x%08X)\n", ranges[r].first, ranges[r].second);
    }

    const std::uint32_t irq_handler = bus.read32(0x03007FFCu);
    std::printf("B6. IRQ: handler@0x03007FFC=0x%08X IME=%d IE=0x%04X irq_depth=%u "
                "vector_entries_observed=%llu\n",
                irq_handler, bus.io().ime() ? 1 : 0, bus.io().ie(), g_irq_nest_depth,
                (unsigned long long)g_irq_vector_entries);

    std::printf("B7. Frame: ppu_frames=%llu vblank_starts=%llu top_level_dispatches=%llu halt_pumps=%llu\n",
                (unsigned long long)ppu.frame_count(),
                (unsigned long long)g_runtime_vblank_starts,
                (unsigned long long)dispatches, (unsigned long long)halt_pumps);

    if (stalled)
        std::printf("B8. STALL at 0x%08X (self_heal_miss=%d)\n", stall_pc,
                    gbarecomp::self_heal_any_misses() ? 1 : 0);
    require(!stalled, "unexpected stall/dispatch miss");

    const std::uint32_t fpc = g_cpu.R[15];
    if (have_frontier) {
        std::printf("B8. FRONTIER: target PC=0x%08X %s part=%s reason=%s\n",
                    frontier.event.pc, frontier.event.thumb ? "Thumb" : "ARM",
                    frontier.event.part, frontier.event.reason);
    } else {
        std::printf("B8. NO FRONTIER within %llu frames: emulator ran natively to the "
                    "frame budget\n", (unsigned long long)kMaxFrames);
    }
    std::printf("    CPU: R15=0x%08X mode=%s CPSR=0x%08X SP=0x%08X LR=0x%08X\n", fpc,
                mode_name(), g_cpu.cpsr, g_cpu.R[13], g_cpu.R[14]);
    for (int r = 0; r < 13; ++r)
        std::printf("    R%d=0x%08X%s", r, g_cpu.R[r], (r % 4 == 3) ? "\n" : "");
    std::printf("\n    host_return_depth=%u irq_depth=%u dispcnt=0x%04X\n",
                runtime_call_stack_depth(), g_irq_nest_depth, bus.io().read16(0x0));

    // Frame evidence: hash of the last complete latched frame (and an optional
    // PPM dump for manual inspection).
    if (ppu.has_latched_framebuffer()) {
        const std::uint8_t* fb = ppu.latched_framebuffer();
        const std::size_t n = gba::GbaPpu::kScreenWidth * gba::GbaPpu::kScreenHeight * 3u;
        std::uint64_t h = 1469598103934665603ull;
        std::size_t nonzero = 0;
        for (std::size_t i = 0; i < n; ++i) { h = (h ^ fb[i]) * 1099511628211ull; nonzero += fb[i] != 0; }
        std::printf("B8b. Latched frame: fnv1a64=0x%016llX nonzero_bytes=%zu/%zu\n",
                    (unsigned long long)h, nonzero, n);
        if (const char* path = std::getenv("MZM_NES_FRAME_DUMP")) {
            std::ofstream out(path, std::ios::binary);
            out << "P6\n240 160\n255\n";
            out.write(reinterpret_cast<const char*>(fb), static_cast<std::streamsize>(n));
        }
    }

    const bool unmapped = bus.unmapped_count() != 0u || bus.io().unmapped_count() != 0u;
    std::printf("B9. Strict counters: dispatch_misses=%d interpreted_insns=%llu "
                "unmapped=%llu io_unhandled=%llu self_heal=disabled\n",
                gbarecomp::self_heal_any_misses() ? 1 : 0,
                (unsigned long long)gbarecomp::self_heal_interpreted_insns(),
                (unsigned long long)bus.unmapped_count(),
                (unsigned long long)bus.io().unmapped_count());
    require(!unmapped, "unmapped access occurred");
    require(!gbarecomp::self_heal_any_misses() &&
            gbarecomp::self_heal_interpreted_insns() == 0u,
            "dispatch miss or interpreted insn");
    // Pinned outcome (NES-2b): all six images have native entries, no Part ever
    // fails verification, and the emulator runs for the whole frame budget with
    // the IRQ handler in Part 2 being delivered. A frontier appearing here is a
    // regression or new evidence: update docs/M4-NES-METROID.md and this pin.
    require(!have_frontier, "unexpected emulator frontier (update the pin and docs)");
    for (const auto& st : stats) {
        require(st.matches >= 1, "every Part must have native entries");
        require(st.verify_failures == 0 && st.invoke_failures == 0,
                "verification/invoke failure");
    }
    require(g_irq_vector_entries >= 1, "IRQ vector never entered");
    require(pay_att >= 1 && pay_match >= 1, "NES payload byte gate not hit");
    // Mutability evidence the gate exclusions rely on (up to the frame budget).
    require(audit[0].dirty.test(0x700) && audit[0].dirty.test(0x701),
            "expected Part 1 tilemap overwrite at 0x06006700");
    require(audit[2].dirty.none() && audit[4].dirty.none() && audit[5].dirty.none(),
            "Parts 3, 5 and 6 must be immutable after load");
    for (std::size_t o = 0; o < 0x5A4C; ++o) {
        if (!audit[1].dirty.test(o)) continue;
        const std::uint32_t a = 0x03000000u + static_cast<std::uint32_t>(o);
        require(!in_runs(mzm_nes_emulator::code_runs(mzm_nes_emulator::ImageKind::Part2),
                         mzm_nes_emulator::code_run_count(mzm_nes_emulator::ImageKind::Part2), a),
                "Part 2 code bytes must never change");
    }

    std::printf("NES-2b PASS: native emulator ran %llu frames strict-static "
                "(part2 entries=%llu, IRQ vector entries=%llu)\n",
                (unsigned long long)ppu.frame_count(),
                (unsigned long long)stats[1].matches,
                (unsigned long long)g_irq_vector_entries);
    return 0;
}
