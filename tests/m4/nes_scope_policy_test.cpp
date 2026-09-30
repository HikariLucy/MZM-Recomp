// NES-3b: verification-scope policy for Part 1 (init / resident / menu).
//
// Uses only the ROM-derived Part images under .local/nes-emulator (no ROM run):
// the images are copied into a bare bus, live bytes are mutated, and the
// production dispatch hook (g_runtime_ram_dispatch_hook, the call runtime_dispatch
// makes) plus the pure resolver functions are checked. Policy under test:
//
//   * an entry PC verifies the scope that owns it and that scope's requires
//     closure, never "the Part was verified once";
//   * after the init half is overwritten, resident PCs still run natively and
//     init/menu PCs fail closed;
//   * a changed resident instruction or resident literal rejects resident PCs;
//   * a wrong-mode entry is not run;
//   * other Parts are unaffected by Part 1 mutations.
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
#include "mzm_nes_emulator_resolver.h"
#include "mzm_ram_dispatch.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "self_heal.h"

#ifndef MZM_NES_EMULATOR_DIR
#define MZM_NES_EMULATOR_DIR ".local/nes-emulator"
#endif

namespace {
using mzm_nes_emulator::ImageKind;
using mzm_nes_emulator::kParts;

int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++g_failures;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

std::uint8_t* region(gba::GbaBus& bus, std::uint32_t addr) {
    switch (addr >> 24) {
        case 0x02: return bus.ewram_ptr() + (addr & 0x3FFFFu);
        case 0x03: return bus.iwram_ptr() + (addr & 0x7FFFu);
        default: return bus.vram_ptr() + (addr - 0x06000000u);
    }
}

std::vector<std::uint8_t> g_pristine[mzm_nes_emulator::kNumParts];

// Scope stats row for (part1, scope id).
mzm_nes_emulator_scope_stats_t scope_row(const char* part, const char* scope) {
    mzm_nes_emulator_scope_stats_t rows[32];
    const std::size_t n = mzm_nes_emulator_scope_stats(rows, 32);
    for (std::size_t i = 0; i < n; ++i)
        if (!std::strcmp(rows[i].part, part) && !std::strcmp(rows[i].scope, scope)) return rows[i];
    std::fprintf(stderr, "no scope row %s/%s\n", part, scope);
    std::exit(1);
}

void load_fresh(gba::GbaBus& bus) {
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i)
        std::memcpy(region(bus, kParts[i].start), g_pristine[i].data(), kParts[i].size);
}

// Resident entry through the production hook. R0=0 makes sub_06006E08 return
// through `bx lr` without touching memory.
int enter(std::uint32_t pc, int thumb) {
    g_cpu = {};
    g_cpu.cpsr = 0x1Fu;
    g_cpu.R[0] = 0;
    g_cpu.R[14] = 0x08000100u;
    g_cpu.R[15] = pc;
    return g_runtime_ram_dispatch_hook(pc, thumb);
}

constexpr std::uint32_t kInitByte = 0x06006100u;        // init code/data (not the 0x6700 exclusion)
constexpr std::uint32_t kResidentInsn = 0x06006E08u;    // first resident instruction
constexpr std::uint32_t kResidentLit = 0x06006E00u;     // literal loaded by 0x06006E10
constexpr std::uint32_t kResidentLit2 = 0x06007000u;    // literal loaded by 0x06006FF0
constexpr std::uint32_t kMenuInsn = 0x0600713Cu;
constexpr std::uint32_t kMenuLiteralInResident = 0x06007234u;  // Part 6 pointer, resident scope
}  // namespace

int main() {
    const std::string emu_dir = MZM_NES_EMULATOR_DIR;
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
        g_pristine[i] = read_file(emu_dir + "/" + kParts[i].name + ".bin");
        if (g_pristine[i].size() != kParts[i].size) {
            std::fprintf(stderr, "missing NES image (run scripts/extract-nes-emulator.py)\n");
            return 1;
        }
    }
    const auto& p1 = mzm_nes_emulator::spec_of(ImageKind::Part1);
    auto reader_of = [&](std::size_t part, std::uint32_t flip) {
        return [&, part, flip](std::uint32_t a) -> std::uint8_t {
            std::uint8_t b = g_pristine[part][a - kParts[part].start];
            return a == flip ? static_cast<std::uint8_t>(b ^ 0xFF) : b;
        };
    };

    std::printf("== scope table\n");
    check(p1.scope_count == 3, "Part 1 has three scopes");
    int init = -1, resident = -1, menu = -1;
    for (std::size_t i = 0; i < p1.scope_count; ++i) {
        if (!std::strcmp(p1.scopes[i].id, "init")) init = static_cast<int>(i);
        if (!std::strcmp(p1.scopes[i].id, "resident")) resident = static_cast<int>(i);
        if (!std::strcmp(p1.scopes[i].id, "menu")) menu = static_cast<int>(i);
    }
    check(init >= 0 && resident >= 0 && menu >= 0, "init/resident/menu scopes present");
    check(mzm_nes_emulator::scope_of(p1, 0x06006000u) == init &&
              mzm_nes_emulator::scope_of(p1, 0x06006558u) == init &&
              mzm_nes_emulator::scope_of(p1, 0x06006DFFu) == init,
          "init owns [0x06006000,0x06006E00)");
    check(mzm_nes_emulator::scope_of(p1, 0x06006E00u) == resident &&
              mzm_nes_emulator::scope_of(p1, 0x06006E08u) == resident &&
              mzm_nes_emulator::scope_of(p1, 0x06006FB0u) == resident &&
              mzm_nes_emulator::scope_of(p1, 0x06007210u) == resident &&
              mzm_nes_emulator::scope_of(p1, 0x0600723Fu) == resident,
          "resident owns the handlers, poll and their literals");
    check(mzm_nes_emulator::scope_of(p1, 0x0600713Cu) == menu &&
              mzm_nes_emulator::scope_of(p1, 0x0600720Fu) == menu,
          "menu owns [0x0600713C,0x06007210)");
    check(mzm_nes_emulator::scope_of(p1, 0x06007240u) == -1 &&
              mzm_nes_emulator::scope_of(p1, 0x06005FFFu) == -1,
          "PCs outside the image own no scope");
    check(p1.scopes[resident].requires_mask == (1u << resident),
          "resident requires nothing but itself");
    check(p1.scopes[init].requires_mask == ((1u << init) | (1u << resident)),
          "init requires resident (direct bl into the handlers)");
    check(p1.scopes[menu].requires_mask == ((1u << menu) | (1u << init) | (1u << resident)),
          "menu requires init and resident (tail branches)");

    std::printf("== pure resolver policy\n");
    // A) fresh image
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, ~0u)) == -1, "A fresh: init entry valid");
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, ~0u)) == -1, "A fresh: resident entry valid");
    check(mzm_nes_emulator::verify_entry(p1, kMenuInsn, reader_of(0, ~0u)) == -1, "A fresh: menu entry valid");
    check(mzm_nes_emulator::verify_image(p1, reader_of(0, ~0u)), "A fresh: every scope verifies");
    // B) init byte mutated
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, kInitByte)) == -1,
          "B init byte mutated: resident entry still valid");
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, kInitByte)) == init,
          "B init byte mutated: init entry rejected (init scope)");
    check(mzm_nes_emulator::verify_entry(p1, kMenuInsn, reader_of(0, kInitByte)) == init,
          "B init byte mutated: menu entry rejected via its init requirement");
    check(!mzm_nes_emulator::verify_image(p1, reader_of(0, kInitByte)), "B init byte mutated: whole image no longer verifies");
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, 0x06006700u)) == -1,
          "B proven-mutable halfword 0x06006700 is still excluded");
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, 0x06006702u)) == init,
          "B the byte after the exclusion is still gated");
    // last init byte / first resident byte are on the right sides of the boundary
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, 0x06006DFFu)) == -1 &&
              mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, 0x06006DFFu)) == init,
          "B last init byte 0x06006DFF only affects init");
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, 0x06006E00u)) == resident,
          "B first resident byte 0x06006E00 is required by init entries too");
    // C) resident instruction mutated
    check(mzm_nes_emulator::verify_entry(p1, 0x06006E08u, reader_of(0, kResidentInsn)) == resident,
          "C resident instruction mutated: resident entry rejected");
    check(mzm_nes_emulator::verify_entry(p1, 0x06006FB0u, reader_of(0, 0x06006FB0u)) == resident &&
              mzm_nes_emulator::verify_entry(p1, 0x06007210u, reader_of(0, 0x06007210u)) == resident,
          "C mutating sub_06006FB0 / sub_06007210 rejects resident entries");
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, reader_of(0, kResidentInsn)) == resident,
          "C resident instruction mutated: init entry rejected (direct bl into it)");
    // D) resident literal dependencies
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, kResidentLit)) == resident,
          "D resident literal 0x06006E00 mutated: resident entry rejected");
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, kResidentLit + 4)) == resident,
          "D resident literal 0x06006E04 mutated: resident entry rejected");
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, kResidentLit2)) == resident,
          "D resident literal 0x06007000 mutated: resident entry rejected");
    check(mzm_nes_emulator::verify_entry(p1, 0x06007210u, reader_of(0, 0x06007238u)) == resident,
          "D resident literal 0x06007238 (KEYINPUT address) mutated: poll rejected");
    check(mzm_nes_emulator::verify_entry(p1, kMenuInsn, reader_of(0, kMenuLiteralInResident)) == resident,
          "D literal 0x06007234 used by the menu lives in resident: menu rejected");
    // menu scope mutation must not disturb the resident handlers
    check(mzm_nes_emulator::verify_entry(p1, kResidentInsn, reader_of(0, kMenuInsn)) == -1 &&
              mzm_nes_emulator::verify_entry(p1, kMenuInsn, reader_of(0, kMenuInsn)) == menu,
          "menu scope mutation rejects only the menu");
    // F) unrelated Parts: one shared view of guest memory with Part 1 dirty.
    auto dirty_view = [&](std::uint32_t a) -> std::uint8_t {
        for (std::size_t part = 0; part < mzm_nes_emulator::kNumParts; ++part)
            if (a >= kParts[part].start && a < kParts[part].start + kParts[part].size) {
                std::uint8_t b = g_pristine[part][a - kParts[part].start];
                return a == kInitByte ? static_cast<std::uint8_t>(b ^ 0xFF) : b;
            }
        return 0;
    };
    check(mzm_nes_emulator::verify_entry(p1, 0x06006558u, dirty_view) == init, "F Part 1 is dirty in the shared view");
    for (std::size_t part = 1; part < mzm_nes_emulator::kNumParts; ++part) {
        const auto& spec = kParts[part];
        check(spec.scope_count == 1 && mzm_nes_emulator::verify_image(spec, dirty_view) &&
                  mzm_nes_emulator::verify_entry(spec, spec.start, dirty_view) == -1,
              "F unrelated Part (single 'image' scope) unaffected by Part 1 mutation");
        check(mzm_nes_emulator::verify_entry(spec, spec.start, reader_of(part, spec.start)) == 0,
              "F unrelated Part still rejects its own flipped byte");
    }

    // G) Part 6: sPasswordBytes[18] = [0x0203E43C,0x0203E44E) is proven mutable
    // (NES-3b, first store frame 3068); everything around it is still gated.
    {
        const auto& p6 = kParts[5];
        const std::uint32_t entry = 0x0203E390u;
        check(mzm_nes_emulator::verify_entry(p6, entry, reader_of(5, ~0u)) == -1, "G Part 6 fresh entry valid");
        check(mzm_nes_emulator::verify_entry(p6, entry, reader_of(5, 0x0203E43Cu)) == -1 &&
                  mzm_nes_emulator::verify_entry(p6, entry, reader_of(5, 0x0203E44Du)) == -1,
              "G Part 6 first/last byte of sPasswordBytes may change");
        check(mzm_nes_emulator::verify_entry(p6, entry, reader_of(5, 0x0203E43Bu)) == 0 &&
                  mzm_nes_emulator::verify_entry(p6, entry, reader_of(5, 0x0203E44Eu)) == 0,
              "G Part 6 bytes next to sPasswordBytes are still gated");
        check(mzm_nes_emulator::verify_entry(p6, entry, reader_of(5, 0x0203E390u)) == 0,
              "G Part 6 code byte mutated: entry rejected");
    }

    std::printf("== production hook (g_runtime_ram_dispatch_hook)\n");
    gba::GbaBus bus;
    gba::GbaPpu ppu;
    bus.io().set_bus(&bus);
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    mzm_install_ram_dispatch_hook();
    check(g_runtime_ram_dispatch_hook != nullptr, "ram dispatch hook installed");

    load_fresh(bus);
    check(enter(kResidentInsn, 0) == 1, "A fresh image: resident entry runs natively");
    auto r = scope_row("part1", "resident");
    check(r.attempts == 1 && r.verified == 1 && r.matches == 1 && r.verify_failures == 0,
          "scope stats: resident 1 attempt / 1 verified / 1 match");

    // B) overwrite init like the graphics do
    for (std::uint32_t a = 0x06006000u; a < 0x06006DC0u; ++a) *region(bus, a) ^= 0x5A;
    check(enter(kResidentInsn, 0) == 1, "B init overwritten: resident entry still runs natively");
    check(enter(0x06006558u, 0) == 0, "B init overwritten: init entry (0x06006558) rejected");
    check(enter(0x06006000u, 0) == 0, "B init overwritten: init entry (0x06006000) rejected");
    check(enter(kMenuInsn, 0) == 0, "B init overwritten: menu entry rejected (requires init)");
    r = scope_row("part1", "resident");
    auto i = scope_row("part1", "init");
    auto m = scope_row("part1", "menu");
    check(r.matches == 2 && r.verify_failures == 0, "B stats: resident 2 matches, 0 failures");
    check(i.attempts == 2 && i.verified == 0 && i.verify_failures == 2 && i.dependency_failures == 0,
          "B stats: init 2 attempts / 2 own-scope failures");
    check(m.attempts == 1 && m.verify_failures == 0 && m.dependency_failures == 1,
          "B stats: menu failure attributed to its init dependency");

    // E) wrong mode: verified bytes, but no Thumb entry exists at that PC
    const auto before = scope_row("part1", "resident");
    check(enter(kResidentInsn, 1) == 0, "E wrong mode (Thumb at an ARM entry) is not run");
    check(scope_row("part1", "resident").matches == before.matches, "E wrong mode is not counted as a native match");

    // C) mutate a resident instruction
    *region(bus, kResidentInsn) ^= 0x01;
    check(enter(kResidentInsn, 0) == 0, "C resident instruction mutated: resident entry rejected");
    *region(bus, kResidentInsn) ^= 0x01;
    check(enter(kResidentInsn, 0) == 1, "C restoring the byte re-admits the entry (no latch)");
    // D) mutate resident literals
    for (std::uint32_t lit : {kResidentLit, kResidentLit + 4, kResidentLit2}) {
        *region(bus, lit) ^= 0x01;
        check(enter(kResidentInsn, 0) == 0, "D resident literal mutated: resident entry rejected");
        *region(bus, lit) ^= 0x01;
    }
    check(enter(kResidentInsn, 0) == 1, "D literals restored: resident entry runs again");

    // the 0x06006700 halfword mutation and a fresh-image init entry are checked in the pure section above
    std::printf("NES-3b scope policy %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
