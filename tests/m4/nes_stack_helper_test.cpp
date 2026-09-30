// NES-3c: code copied to the stack (SramCheckInternal / SramWriteUncheckedInternal).
//
// Uses the legal ROM only for the helper source bytes (MZM 0x0800529C, 0x080051D4)
// and, when present, the extracted NES Part 6 image to prove the NES emulator's
// copy is byte-identical. No game run: the helper bytes are copied into IWRAM at
// several destinations (direct and mirror aliases) and entered through the
// production hook (g_runtime_ram_dispatch_hook). Policy under test:
//
//   * identity comes from the LIVE bytes at the guest PC, on every entry (no latch);
//   * the window is the PHYSICAL IWRAM stack window, so the alias 0x03827110 (the
//     NES emulator's stack) and the direct 0x03007164 run the same verified body;
//   * the guest PC stays the logical alias, the source PC never leaks, LR returns;
//   * one changed byte, a wrong mode, a wrong PC, a truncated copy are rejected;
//   * the helpers are not published in ordinary dispatch.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gba_bus.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "mzm_ram_dispatch.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "self_heal.h"

#ifndef MZM_NES_EMULATOR_DIR
#define MZM_NES_EMULATOR_DIR ".local/nes-emulator"
#endif

namespace {
int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++g_failures;
}
std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

gba::GbaBus* g_bus = nullptr;
std::uint8_t* iwram(std::uint32_t addr) { return g_bus->iwram_ptr() + (addr & 0x7FFFu); }
std::uint8_t* ewram(std::uint32_t addr) { return g_bus->ewram_ptr() + (addr & 0x3FFFFu); }

constexpr std::uint32_t kCheckSrc = 0x0800529Cu, kCheckSize = 0x30u;
constexpr std::uint32_t kWriteSrc = 0x080051D4u, kWriteSize = 0x24u;
constexpr std::uint32_t kBufA = 0x02010000u, kBufB = 0x02010100u;   // EWRAM data buffers
constexpr std::uint32_t kLr = 0x08000200u;

mzm_stack_helper_stats_t stat(const char* name) {
    mzm_stack_helper_stats_t rows[4];
    std::uint64_t rej;
    const std::size_t n = mzm_stack_helper_stats(rows, 4, &rej);
    for (std::size_t i = 0; i < n; ++i)
        if (!std::strcmp(rows[i].name, name)) return rows[i];
    std::fprintf(stderr, "no stat row %s\n", name);
    std::exit(1);
}
std::uint64_t rejects() {
    mzm_stack_helper_stats_t rows[4];
    std::uint64_t rej = 0;
    mzm_stack_helper_stats(rows, 4, &rej);
    return rej;
}

void put(std::uint32_t dest, const std::vector<std::uint8_t>& bytes) {
    std::memcpy(iwram(dest), bytes.data(), bytes.size());
}

// Enter `pc` as the call `func(a, b, n)` made by `bl _call_via_r3`: r3 = pc|1,
// sp = pc (SramCheck copies the helper to its own frame base), lr = caller.
int enter(std::uint32_t pc, int thumb, std::uint32_t a, std::uint32_t b, std::uint32_t n) {
    g_cpu = {};
    g_cpu.cpsr = 0x3Fu;
    g_cpu.R[0] = a; g_cpu.R[1] = b; g_cpu.R[2] = n;
    g_cpu.R[3] = pc | 1u;
    g_cpu.R[13] = pc;
    g_cpu.R[14] = kLr | 1u;
    g_cpu.R[15] = pc;
    runtime_call_push_return(kLr | 1u);
    const int r = g_runtime_ram_dispatch_hook(pc, thumb);
    if (!r) runtime_call_cancel_return(kLr | 1u);
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <mzm_usa.gba>\n", argv[0]); return 2; }
    const auto rom = read_file(argv[1]);
    if (rom.size() < 0x8000) { std::fprintf(stderr, "cannot read ROM\n"); return 1; }
    const std::vector<std::uint8_t> chk(rom.begin() + (kCheckSrc & 0xFFFFFF), rom.begin() + (kCheckSrc & 0xFFFFFF) + kCheckSize);
    const std::vector<std::uint8_t> wr(rom.begin() + (kWriteSrc & 0xFFFFFF), rom.begin() + (kWriteSrc & 0xFFFFFF) + kWriteSize);

    std::printf("== source identity\n");
    {
        const std::string p6 = std::string(MZM_NES_EMULATOR_DIR) + "/part6.bin";
        const auto part6 = read_file(p6);
        if (part6.size() == 0x8E0) {
            check(std::memcmp(part6.data() + 0x7BC, chk.data(), kCheckSize) == 0,
                  "NES Part 6 0x0203E7BC (SramCheckInternal, 0x30) == MZM ROM 0x0800529C");
            check(std::memcmp(part6.data() + 0x6F4, wr.data(), kWriteSize) == 0,
                  "NES Part 6 0x0203E6F4 (SramWriteUncheckedInternal, 0x24) == MZM ROM 0x080051D4");
        } else {
            std::printf("  skip NES Part 6 identity (extracted images not present)\n");
        }
    }

    gba::GbaBus bus;
    gba::GbaPpu ppu;
    g_bus = &bus;
    bus.set_rom(rom.data(), rom.size());
    bus.io().set_bus(&bus);
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    gbarecomp::self_heal_reset();
    mzm_install_ram_dispatch_hook();
    check(g_runtime_ram_dispatch_hook != nullptr, "ram dispatch hook installed");

    // Destinations: NES stack alias (EmulatorSaveToSram path), the alias one call
    // depth shallower (sub_0203E3AC path), MZM's own direct stack address.
    const std::uint32_t dests[] = {0x03827110u, 0x03827114u, 0x03007164u, 0x03A07188u};

    std::printf("== SramCheckInternal at every destination\n");
    for (std::uint32_t d : dests) {
        std::memset(iwram(d), 0, 0x40);
        put(d, chk);
        for (int i = 0; i < 16; ++i) { *ewram(kBufA + i) = static_cast<std::uint8_t>(i * 7); *ewram(kBufB + i) = static_cast<std::uint8_t>(i * 7); }
        const auto before = stat("SramCheckInternal");
        char msg[96];
        std::snprintf(msg, sizeof msg, "0x%08X exact bytes run natively (equal buffers -> r0 == 0)", d);
        check(enter(d, 1, kBufA, kBufB, 16) == 1 && g_cpu.R[0] == 0, msg);
        std::snprintf(msg, sizeof msg, "0x%08X returns to LR, logical PC is the caller's, no source PC leak", d);
        check(g_cpu.R[15] == (kLr | 1u) || g_cpu.R[15] == kLr, msg);
        *ewram(kBufB + 9) ^= 0x40;
        enter(d, 1, kBufA, kBufB, 16);
        std::snprintf(msg, sizeof msg, "0x%08X differing byte -> r0 == &dest[9] (the copied body computed it)", d);
        check(g_cpu.R[0] == kBufB + 9, msg);
        const auto after = stat("SramCheckInternal");
        const bool mirror = (d & 0x00FFFFFFu) >= 0x8000u;
        std::snprintf(msg, sizeof msg, "0x%08X counted as %s match", d, mirror ? "mirror" : "direct");
        check(after.matches == before.matches + 2 && after.mirror_matches == before.mirror_matches + (mirror ? 2 : 0), msg);
        check(runtime_has_static_entry(d, 1) == 0 && runtime_has_static_entry(d & 0x7FFF | 0x03000000u, 1) == 0,
              "helper is not published in ordinary dispatch (private only)");
    }

    std::printf("== rejections (SramCheckInternal at 0x03827110)\n");
    const std::uint32_t d = 0x03827110u;
    std::memset(iwram(d), 0, 0x60);
    put(d, chk);
    check(enter(d, 1, kBufA, kBufB, 16) == 1, "fresh copy accepted");
    bool all = true;
    for (std::uint32_t i = 0; i < kCheckSize; ++i) {
        *iwram(d + i) ^= 0x01;
        all &= enter(d, 1, kBufA, kBufB, 16) == 0;
        *iwram(d + i) ^= 0x01;
    }
    check(all, "every one-byte mutation of the 0x30-byte copy (code, literal-free tail, padding) is rejected");
    check(enter(d, 1, kBufA, kBufB, 16) == 1, "restoring the bytes re-admits the entry (no latch)");
    check(enter(d, 0, kBufA, kBufB, 16) == 0, "wrong mode (ARM entry at Thumb bytes) rejected");
    {   // truncated copy: only 0x20 bytes copied, the rest is stale non-zero
        std::memset(iwram(d), 0xFF, 0x60);
        std::memcpy(iwram(d), chk.data(), 0x20);
        check(enter(d, 1, kBufA, kBufB, 16) == 0, "wrong copy size (0x20 of 0x30 bytes) rejected");
    }
    {   // wrong PC: identical bytes below the stack window, in EWRAM, one byte off
        std::memset(iwram(0x03006F00u), 0, 0x60);
        put(0x03006F00u, chk);
        check(enter(0x03006F00u, 1, kBufA, kBufB, 16) == 0, "identical bytes below the physical stack window rejected");
        std::memcpy(ewram(0x0203F000u), chk.data(), chk.size());
        check(enter(0x0203F000u, 1, kBufA, kBufB, 16) == 0, "identical bytes in EWRAM rejected (not the stack window)");
        std::memset(iwram(d), 0, 0x60);
        put(d + 2, chk);
        check(enter(d, 1, kBufA, kBufB, 16) == 0, "bytes shifted by one halfword: entry at the wrong PC rejected");
        check(enter(d + 2, 1, kBufA, kBufB, 16) == 1, "...and accepted at the PC where the bytes really are");
    }
    check(rejects() > 0, "rejections are counted");

    std::printf("== SramWriteUncheckedInternal (the other copied helper)\n");
    for (std::uint32_t dd : {0x03827100u, 0x03007164u}) {
        std::memset(iwram(dd), 0, 0x60);
        put(dd, wr);
        for (int i = 0; i < 16; ++i) { *ewram(kBufA + i) = static_cast<std::uint8_t>(0xA0 + i); *ewram(kBufB + i) = 0; }
        char msg[96];
        std::snprintf(msg, sizeof msg, "0x%08X copies through the verified body", dd);
        check(enter(dd, 1, kBufA, kBufB, 16) == 1 && std::memcmp(ewram(kBufB), ewram(kBufA), 16) == 0, msg);
    }

    std::printf("NES-3c stack helper policy %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
