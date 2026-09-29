#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gba_bios.h"
#include "gba_bus.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "self_heal.h"
#include "sha1.h"
#include "mzm_ram_dispatch.h"
#include "mzm_nes_payload_resolver.h"

namespace {
constexpr std::uint32_t kTrampoline = 0x087D8000u;
constexpr std::uint32_t kLoader = 0x087D80D4u;
constexpr std::uint32_t kPayloadEntry = 0x03007400u;
constexpr std::size_t kPayloadSize = 0x214u;
constexpr const char* kExpectedRomSha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8";
constexpr const char* kExpectedPayloadSha256 = "e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44";

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "NES-1b FAIL: %s\n", message);
    std::exit(1);
}

} // namespace

int main(int argc, char** argv) {
    require(argc >= 3, "usage: nes_payload_frontier_test ROM BIOS");
    setenv("GBARECOMP_STRICT_STATIC", "1", 1);
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(input)), {});
    require(rom.size() == 0x800000u, "expected USA 8 MiB cartridge");
    require(gba::sha1(rom.data(), rom.size()).hex() == kExpectedRomSha1,
            "wrong ROM SHA-1: expected USA 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8");

    gba::GbaBios bios;
    std::string error;
    require(bios.load_from_file(argv[2], gba::GbaBios::kExpectedSha1, &error),
            "BIOS hash/load failure");

    gba::GbaBus bus;
    gba::GbaPpu ppu;
    bus.set_rom(rom.data(), rom.size());
    bus.set_bios(&bios);
    // Same wiring as the production run loop (runtime.cpp); without it DMA
    // transfers are inert and guest RAM images are never populated.
    bus.io().set_bus(&bus);
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    gbarecomp::self_heal_reset();

    mzm_install_ram_dispatch_hook();
    mzm_set_nes_payload_frontier_stop(true);

    g_cpu = {};
    g_cpu.cpsr = 0x1Fu;
    g_cpu.R[0] = 0x08000000u;
    g_cpu.R[15] = kTrampoline;

    std::printf("1. Dispatching trampoline 0x%08X...\n", kTrampoline);
    runtime_dispatch(kTrampoline);

    std::printf("2. Loader returned to 0x%08X (R15=0x%08X)...\n", g_cpu.R[15], g_cpu.R[15]);
    require(g_cpu.R[15] == 0x087D8110u, "LZ77 did not return to loader continuation");

    std::printf("3. Dispatching loader continuation 0x%08X...\n", g_cpu.R[15]);
    runtime_dispatch(g_cpu.R[15]);

    std::printf("4. Control yielded at 0x%08X (cpsr=0x%08X)...\n", g_cpu.R[15], g_cpu.cpsr);

    // Loop dispatch until payload exits its range
    std::printf("5. Dispatching payload until exit...\n");
    for (int step = 0; step < 100; ++step) {
        std::uint32_t current_pc = g_cpu.R[15];
        std::printf("  [step %d] PC=0x%08X CPSR=0x%08X SP=0x%08X LR=0x%08X\n",
                    step, current_pc, g_cpu.cpsr, g_cpu.R[13], g_cpu.R[14]);
        if (current_pc < 0x03007400u || current_pc >= 0x03007614u) {
            std::printf("  Reached PC outside payload: 0x%08X!\n", current_pc);
            break;
        }
        runtime_dispatch(current_pc);
    }

    std::uint16_t waitcnt_after = bus.io().read16(0x204);
    std::printf("6. WAITCNT after payload: 0x%04X\n", waitcnt_after);
    require(waitcnt_after == 0x0014u, "WAITCNT was not written with 0x0014");

    std::uint64_t nes_attempts = 0, nes_matches = 0;
    mzm_nes_payload_dispatch_counts(nes_attempts, nes_matches);
    std::printf("7. NES payload dispatch counts: attempts=%llu, matches=%llu\n",
                static_cast<unsigned long long>(nes_attempts),
                static_cast<unsigned long long>(nes_matches));
    require(nes_attempts >= 1 && nes_matches >= 1, "NES payload byte gate not hit");

    constexpr std::uint32_t kExpectedFrontier = 0x06006558u;
    std::printf("8. Reached frontier PC=0x%08X (mode=%s, SP=0x%08X, LR=0x%08X)\n",
                g_cpu.R[15], (g_cpu.cpsr & CPSR_T_BIT) ? "Thumb" : "ARM",
                g_cpu.R[13], g_cpu.R[14]);
    require(g_cpu.R[15] == kExpectedFrontier,
            "expected post-payload frontier 0x06006558 in VRAM");
    require((g_cpu.cpsr & CPSR_T_BIT) == 0u,
            "expected ARM mode at post-payload frontier");
    require(runtime_call_stack_depth() == 0u,
            "leaked host return stack frame");
    require(mzm_nes_payload_frontier_hits() >= 1u,
            "post-payload frontier hook not recorded");
    require(bus.unmapped_count() == 0u && bus.io().unmapped_count() == 0u,
            "unmapped access occurred before post-payload frontier");
    require(!gbarecomp::self_heal_any_misses() &&
            gbarecomp::self_heal_interpreted_insns() == 0u,
            "dispatch miss or interpreted insn before post-payload frontier");

    std::printf("NES-1b PASS: strict-static payload execution reached frontier 0x%08X ARM with zero misses.\n",
                g_cpu.R[15]);
    return 0;
}
