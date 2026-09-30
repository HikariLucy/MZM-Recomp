// NES-3: behavioural qualification of the native NES emulator (soak, input,
// post-title state, audio/SRAM audit). Production behaviour is not modified:
// the fixture drives the runtime like runtime.cpp's frame loop and injects input
// through the same call the run loop uses (bus.io().set_keyinput(active-low
// mask), runtime.cpp host-poll / GBARECOMP_INPUT_REPLAY path). Guest memory is
// never written by the harness.
//
// Configuration (environment, all optional):
//   MZM_NES_FRAMES            total PPU frames to run             (default 1300)
//   MZM_NES_CHECKPOINT        summary line every N frames         (default 500)
//   MZM_NES_HASH_FRAMES       comma list of frames to hash+report (default 600,1200)
//   MZM_NES_INPUT             "frame:KEY[+KEY]:hold,..."          (default none)
//                             KEY in A B SELECT START RIGHT LEFT UP DOWN R L
//   MZM_NES_FRAME_DUMP_DIR    write PPMs of the latched frame at hash frames
//   MZM_NES_STOP_ON_FRONTIER  1 (default): a frontier/miss ends the run and is
//                             reported as the new frontier (exit 3)
// All lines starting with "NES3 " are deterministic (no timing) and comparable
// between runs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "gba_bios.h"
#include "gba_bus.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "mzm_nes_emulator_resolver.h"
#include "mzm_ram_dispatch.h"
#include "bios_hle.h"
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
constexpr std::uint32_t kIrqHandler = 0x030057A8u;      // Part 2 IRQ handler
constexpr std::uint32_t kApuWrite = 0x03000408u;        // EmulatorAudio_WriteToApu
constexpr std::uint32_t kAudioInit = 0x03000488u;       // EmulatorAudio_Initialize
constexpr std::uint32_t kSetupOutput = 0x030002A4u;     // EmulatorAudio_SetupOutput
constexpr std::uint32_t kTimer1Cb = 0x030004E0u;        // EmulatorAudio_Timer1Callback
constexpr std::uint32_t kMixBuffers = 0x03001D30u;      // ProcessApuAndMixBuffers

const std::map<std::uint32_t, const char*> kWatched = {
    {kIrqHandler, "irq_handler"},
    {kApuWrite, "apu_write"},
    {kAudioInit, "audio_init"},
    {kSetupOutput, "audio_setup_output"},
    {kTimer1Cb, "audio_timer1_cb"},
    {kMixBuffers, "audio_mix_buffers"},
    {0x0203E000u, "sram_RetrieveGameOverPassword"},
    {0x0203E118u, "sram_FillPasswordWithSaved"},
    {0x0203E24Cu, "sram_SaveToPasswordBytes"},
    {0x0203E374u, "sram_LoadFromPasswordBytes"},
    {0x0203E3DCu, "sram_SaveToSram"},
    {0x0203E414u, "sram_LoadFromSram"},
};

// Diagnostic (MZM_NES_WATCH_IMAGE_WRITES=1): records CPU stores that change bytes
// of the Part 1 image (VRAM) and of the Part 6 image (EWRAM). Part 1 is reported
// per 0x40-byte chunk: first frame, writing PC, widths, count. Part 6 is reported
// per store address (few distinct ones): first/last frame, writing PC, old/new.
// Read-only observer; never suppresses a store. Evidence for the Part 1
// init/resident lifecycle and the Part 6 mutable words (docs/M4-NES-METROID.md).
struct Part1WriteWatch final : gba::BusWriteObserver {
    struct Chunk { std::uint64_t first_frame = 0, count = 0; std::uint32_t first_pc = 0, widths = 0; };
    std::map<std::uint32_t, Chunk> chunks;   // keyed by chunk base address
    std::map<std::uint32_t, std::uint64_t> writer_pcs;
    std::uint32_t min_addr = ~0u, max_addr = 0;   // inclusive byte range of changed stores
    struct Word { std::uint64_t first_frame = 0, last_frame = 0, count = 0;
                  std::uint32_t first_pc = 0, first_old = 0, first_new = 0, last_new = 0, width = 0; };
    std::map<std::uint32_t, Word> p6_words;       // keyed by store address
    std::map<std::uint32_t, std::uint64_t> p6_writer_pcs;
    std::uint64_t (*frame)() = nullptr;
    bool on_bus_write(gba::BusWriteRegion region, std::uint32_t, std::uint32_t addr,
                      std::uint8_t width, std::uint32_t old_value, std::uint32_t new_value) override {
        if (old_value == new_value) return false;
        if (region == gba::BusWriteRegion::Ewram) {
            if (addr < 0x0203E000u || addr >= 0x0203E8E0u) return false;
            auto& w = p6_words[addr];
            if (!w.count) { w.first_frame = frame(); w.first_pc = g_cpu.R[15]; w.first_old = old_value;
                            w.first_new = new_value; w.width = width;
                            std::fprintf(stderr, "NES3 p6_first_write addr=0x%08X width=%u frame=%llu pc=0x%08X lr=0x%08X old=0x%X new=0x%X\n",
                                         addr, width, (unsigned long long)frame(), g_cpu.R[15], g_cpu.R[14], old_value, new_value); }
            w.last_frame = frame(); w.last_new = new_value; ++w.count;
            ++p6_writer_pcs[g_cpu.R[15]];
            return false;
        }
        if (region != gba::BusWriteRegion::Vram) return false;
        if (addr < 0x06006000u || addr >= 0x06007240u) return false;
        min_addr = std::min(min_addr, addr);
        max_addr = std::max(max_addr, addr + width - 1u);
        auto& c = chunks[addr & ~0x3Fu];
        if (!c.count) { c.first_frame = frame(); c.first_pc = g_cpu.R[15]; }
        ++c.count;
        c.widths |= width;
        ++writer_pcs[g_cpu.R[15]];
        return false;
    }
};
Part1WriteWatch g_p1_watch;
gba::GbaPpu* g_watch_ppu = nullptr;

// NES-3c: SRAM helper copied to the stack. Read-only observer around each run;
// prints one line per entry/exit (a handful per session) and keeps the last
// return value. SRAM contents themselves are audited through save().dirty().
gba::GbaPpu* g_log_ppu = nullptr;
std::uint64_t g_helper_events = 0;
void on_stack_helper(const mzm_stack_helper_event_t& e) {
    ++g_helper_events;
    std::printf("NES3 stack_helper %s frame=%llu %s pc=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X sp=0x%08X lr=0x%08X\n",
                e.name, (unsigned long long)(g_log_ppu ? g_log_ppu->frame_count() : 0), e.exit ? "exit" : "enter",
                e.pc, e.r0, e.r1, e.r2, e.r3, e.sp, e.lr);
}

void fail(const char* message) {
    std::fprintf(stderr, "NES-3 FAIL: %s\n", message);
    std::exit(1);
}
void require(bool c, const char* m) { if (!c) fail(m); }

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

struct EmulatorFrontier { mzm_nes_emulator_event_t event; };
void on_frontier(const mzm_nes_emulator_event_t& e) { throw EmulatorFrontier{e}; }

std::uint64_t g_irq_vector_entries = 0;
std::map<std::uint32_t, std::uint64_t> g_entries;
std::uint32_t g_last_sram_pc = 0;
std::uint64_t g_apu_logged = 0;
std::map<std::uint32_t, std::uint64_t> g_apu_regs;  // NES APU register -> writes
bool g_audio_diag = false;
// ---- NES save/load observation (read-only; nothing here writes guest memory) ----
constexpr std::uint32_t kSaveToSram = 0x0203E3DCu;      // EmulatorSaveToSram(sp, src, dst)
constexpr std::uint32_t kLoadFromSram = 0x0203E414u;    // EmulatorLoadFromSram(src, dst, sp)
constexpr std::uint32_t kSaveToPwBytes = 0x0203E24Cu;   // EmulatorSaveToPasswordBytes(src)
constexpr std::uint32_t kLoadFromPwBytes = 0x0203E374u; // EmulatorLoadFromPasswordBytes(sp, dst)
constexpr std::uint32_t kPasswordBytes = 0x0203E43Cu;   // sPasswordBytes[18]
constexpr std::uint16_t kPasswordPleaseTiles[15] = {0x19, 0x0A, 0x1C, 0x1C, 0x20, 0x18, 0x1B, 0x0D,
                                                      0xFF, 0x19, 0x15, 0x0E, 0x0A, 0x1C, 0x0E};  // "PASSWORD PLEASE"
constexpr std::uint32_t kSramSaveBase = 0x7FB0u;        // SRAM_BASE + 0x7FB0 (two 0x28-byte copies)
gba::GbaBus* g_save_bus = nullptr;
bool g_save_log = false;
struct SaveCall {
    const char* name;
    std::uint64_t frame;
    std::uint32_t r[4];
    std::vector<std::uint8_t> payload;   // bytes the call intends to move (src for save, SRAM for load)
};
std::vector<SaveCall> g_save_calls;      // every call, in order
std::size_t g_save_checked = 0;          // calls already verified at a frame boundary
std::string hex_bytes(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string o;
    for (std::size_t i = 0; i < n; ++i) { o += d[p[i] >> 4]; o += d[p[i] & 15]; }
    return o;
}
std::vector<std::uint8_t> read_guest(std::uint32_t addr, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = g_save_bus->read8(addr + static_cast<std::uint32_t>(i));
    return v;
}
void on_save_call(std::uint32_t pc) {
    if (!g_save_bus) return;
    SaveCall c{};
    c.frame = g_log_ppu ? g_log_ppu->frame_count() : 0;
    for (int i = 0; i < 4; ++i) c.r[i] = g_cpu.R[i];
    switch (pc) {
        case kSaveToSram: c.name = "SaveToSram"; c.payload = read_guest(g_cpu.R[1], 0x28); break;
        case kLoadFromSram: {
            c.name = "LoadFromSram";
            const std::uint32_t src = g_cpu.R[0] ? g_cpu.R[0] : 0x0E000000u + kSramSaveBase;
            c.payload = read_guest(src, 0x28);
            break;
        }
        case kSaveToPwBytes: c.name = "SaveToPasswordBytes"; c.payload = read_guest(g_cpu.R[0] + 0x10, 18); break;
        case kLoadFromPwBytes: c.name = "LoadFromPasswordBytes"; c.payload = read_guest(kPasswordBytes, 18); break;
        default: return;
    }
    g_save_calls.push_back(c);
    if (g_save_log)
        std::printf("NES3 save_call %s frame=%llu r0=0x%08X r1=0x%08X r2=0x%08X lr=0x%08X payload=%s pw=%s\n",
                    c.name, (unsigned long long)c.frame, c.r[0], c.r[1], c.r[2], g_cpu.R[14],
                    hex_bytes(c.payload.data(), c.payload.size()).c_str(),
                    hex_bytes(read_guest(kPasswordBytes, 18).data(), 18).c_str());
}
void on_function_entry(std::uint32_t pc) {
    if (pc == kSaveToSram || pc == kLoadFromSram || pc == kSaveToPwBytes || pc == kLoadFromPwBytes)
        on_save_call(pc);
    if (pc == 0x00000018u) ++g_irq_vector_entries;
    if (pc == kApuWrite && g_audio_diag) {
        ++g_apu_regs[g_cpu.R[0]];
        if (g_apu_logged < 24) {
            ++g_apu_logged;
            std::printf("NES3 apu_write#%llu R0=0x%08X R1=0x%08X\n", (unsigned long long)g_apu_logged,
                        g_cpu.R[0], g_cpu.R[1]);
        }
    }
    auto it = kWatched.find(pc);
    if (it != kWatched.end()) ++g_entries[pc];
}

std::uint64_t fnv(const std::uint8_t* p, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

std::string env_or(const char* k, const char* d) {
    const char* v = std::getenv(k);
    return v && v[0] ? v : d;
}

std::uint16_t key_bit(const std::string& k) {
    static const std::map<std::string, int> bits = {
        {"A", 0}, {"B", 1}, {"SELECT", 2}, {"START", 3}, {"RIGHT", 4},
        {"LEFT", 5}, {"UP", 6}, {"DOWN", 7}, {"R", 8}, {"L", 9}};
    auto it = bits.find(k);
    if (it == bits.end()) fail("unknown key name in MZM_NES_INPUT");
    return static_cast<std::uint16_t>(1u << it->second);
}

struct InputEvent { std::uint64_t frame; std::uint16_t pressed; std::uint64_t hold; std::string name; };

std::vector<InputEvent> parse_input(const std::string& s) {
    std::vector<InputEvent> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        const auto c1 = item.find(':'), c2 = item.rfind(':');
        if (c1 == std::string::npos || c1 == c2) fail("bad MZM_NES_INPUT item");
        InputEvent e{};
        e.frame = std::strtoull(item.substr(0, c1).c_str(), nullptr, 10);
        e.name = item.substr(c1 + 1, c2 - c1 - 1);
        e.hold = std::strtoull(item.substr(c2 + 1).c_str(), nullptr, 10);
        std::stringstream ks(e.name);
        std::string k;
        while (std::getline(ks, k, '+')) e.pressed |= key_bit(k);
        out.push_back(e);
    }
    return out;
}

const std::uint8_t* region_ptr(gba::GbaBus& bus, std::uint32_t addr) {
    switch (addr >> 24) {
        case 0x02: return bus.ewram_ptr() + (addr & 0x3FFFFu);
        case 0x03: return bus.iwram_ptr() + (addr & 0x7FFFu);
        case 0x06: return bus.vram_ptr() + (addr - 0x06000000u);
        default: return nullptr;
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <mzm_usa.gba> <gba_bios.bin> [nes-emulator-dir]\n", argv[0]);
        return 2;
    }
    const std::uintptr_t stack_base = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    const std::string emu_dir = argc > 3 ? argv[3] : MZM_NES_EMULATOR_DIR;
    std::uint64_t diag_from = 0, diag_count = 0;
    {
        const std::string d = env_or("MZM_NES_DIAG_FRAMES", "");
        if (!d.empty()) std::sscanf(d.c_str(), "%llu,%llu", (unsigned long long*)&diag_from, (unsigned long long*)&diag_count);
    }
    const std::uint64_t max_frames = std::strtoull(env_or("MZM_NES_FRAMES", "1300").c_str(), nullptr, 10);
    const std::uint64_t ckpt_every = std::strtoull(env_or("MZM_NES_CHECKPOINT", "500").c_str(), nullptr, 10);
    const bool stop_on_frontier = env_or("MZM_NES_STOP_ON_FRONTIER", "1") == "1";
    std::set<std::uint64_t> hash_frames;
    {
        std::stringstream ss(env_or("MZM_NES_HASH_FRAMES", "600,1200"));
        std::string t;
        while (std::getline(ss, t, ',')) hash_frames.insert(std::strtoull(t.c_str(), nullptr, 10));
    }
    const std::vector<InputEvent> input = parse_input(env_or("MZM_NES_INPUT", ""));
    const std::string dump_dir = env_or("MZM_NES_FRAME_DUMP_DIR", "");
    g_audio_diag = env_or("MZM_NES_AUDIO_DIAG", "0") == "1";
    g_save_log = env_or("MZM_NES_SAVE_LOG", "0") == "1";

    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
    require(rom.size() == 0x800000u, "expected USA 8 MiB cartridge");
    require(gba::sha1(rom.data(), rom.size()).hex() == kExpectedRomSha1, "wrong ROM SHA-1");
    gba::GbaBios bios;
    std::string error;
    require(bios.load_from_file(argv[2], gba::GbaBios::kExpectedSha1, &error), "BIOS hash/load failure");

    std::vector<std::uint8_t> pristine[mzm_nes_emulator::kNumParts];
    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
        const auto& spec = mzm_nes_emulator::kParts[i];
        pristine[i] = read_file(emu_dir + "/" + spec.name + ".bin");
        require(pristine[i].size() == spec.size, "missing NES image (run scripts/extract-nes-emulator.py)");
    }

    gba::GbaBus bus;
    gba::GbaPpu ppu;
    bus.set_rom(rom.data(), rom.size());
    bus.set_bios(&bios);
    bus.io().set_ppu(&ppu);   // as runtime.cpp does: VCOUNT/DISPSTAT reads need the PPU (MZM SetupSoundTransfer spins on VCOUNT)
    bus.io().set_bus(&bus);
    bus.save().configure_sram(32 * 1024);
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    gbarecomp::self_heal_reset();
    mzm_install_ram_dispatch_hook();
    g_runtime_fn_entry_hook = on_function_entry;
    g_save_bus = &bus;

    // Loader entry (same as NES-2): trampoline -> loader -> payload -> 0x06006558.
    // This is what MZM's OptionsNesMetroidHandler stage 4 does (IME=0, IF=FFFF,
    // r0=ROM_BASE, call sNesEmuBootLoader); the menu path itself is gated behind
    // gFileScreenOptionsUnlocked.soundTestAndOrigMetroid (a completed game) and
    // is not exercised. Used for the first entry and any scheduled re-entry.
    auto enter_nes_loader = [&]() {
        mzm_set_nes_payload_frontier_stop(true);
        bus.io().write16(0x208, 0);       // REG_IME = FALSE
        bus.io().write16(0x202, 0xFFFF);  // REG_IF  = USHORT_MAX
        bus.io().clear_halt();
        g_cpu = {};
        g_cpu.cpsr = 0x1Fu;
        g_cpu.R[0] = 0x08000000u;
        g_cpu.R[15] = kTrampoline;
        runtime_dispatch(kTrampoline);
        require(g_cpu.R[15] == 0x087D8110u, "LZ77 did not return to loader continuation");
        runtime_dispatch(g_cpu.R[15]);
        for (int step = 0; step < 100; ++step) {
            const std::uint32_t pc = g_cpu.R[15];
            if (pc < 0x03007400u || pc >= 0x03007614u) break;
            runtime_dispatch(pc);
        }
        require(g_cpu.R[15] == kPart1Entry, "payload did not reach 0x06006558");
        for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
            const auto& spec = mzm_nes_emulator::kParts[i];
            require(std::memcmp(region_ptr(bus, spec.start), pristine[i].data(), spec.size) == 0,
                    "guest image differs from ROM-derived image");
        }
        mzm_set_nes_payload_frontier_stop(false);
    };
    // MZM_NES_START=mzm: cold-boot MZM from the ROM entry first (its own boot
    // initialises its SRAM), then enter the NES at the frames in MZM_NES_ENTER_AT.
    const bool start_mzm = env_or("MZM_NES_START", "nes") == "mzm";
    std::vector<std::uint64_t> enter_at;
    {
        std::stringstream ss(env_or("MZM_NES_ENTER_AT", ""));
        std::string t;
        while (std::getline(ss, t, ',')) if (!t.empty()) enter_at.push_back(std::strtoull(t.c_str(), nullptr, 10));
    }
    std::size_t next_enter = 0;
    if (start_mzm) {
        g_cpu = {};
        g_cpu.banked_sp[ARM_BANK_SUPERVISOR] = 0x03007FE0u;
        g_cpu.banked_sp[ARM_BANK_IRQ] = 0x03007FA0u;
        g_cpu.banked_sp[ARM_BANK_USER] = 0x03007F00u;
        gba::bios_hle_boot_skip(0x08000000u);   // the state the BIOS hands the cart at cold boot
    } else {
        enter_nes_loader();
    }
    mzm_set_nes_payload_frontier_stop(false);
    mzm_set_nes_emulator_frontier_hook(on_frontier);
    g_log_ppu = &ppu;
    mzm_set_stack_helper_observer(on_stack_helper);
    // Installed only after the payload loaded the images (Phase A), so the
    // recorded stores are the emulator's own, not the load.
    const bool watch_p1_writes = env_or("MZM_NES_WATCH_IMAGE_WRITES", "0") == "1";
    if (watch_p1_writes) {
        g_watch_ppu = &ppu;
        g_p1_watch.frame = [] { return g_watch_ppu->frame_count(); };
        bus.set_write_observer(&g_p1_watch);
    }

    // MZM_NES_SAVE_LOAD=<file>: start from a cartridge save written by an earlier
    // process (runtime.cpp loads the .sav with this same GbaSave call).
    const std::string save_load = env_or("MZM_NES_SAVE_LOAD", "");
    if (!save_load.empty()) {
        const auto bytes = read_file(save_load);
        require(bus.save().load_sram_bytes(bytes.data(), bytes.size()), "MZM_NES_SAVE_LOAD: bad SRAM image");
        bus.save().clear_dirty();
        std::printf("NES3 save_loaded file_bytes=%zu\n", bytes.size());
    }
    // The SRAM starts erased (0xFF); snapshot to audit guest writes.
    const std::vector<std::uint8_t> sram0 = bus.save().sram_bytes();

    // ---- state ----
    std::uint64_t dispatches = 0, halt_pumps = 0;
    int no_progress = 0;
    bool have_frontier = false, stalled = false;
    EmulatorFrontier frontier{};
    std::uint32_t stall_pc = 0;
    std::uint64_t audio_hash = 1469598103934665603ull, audio_next = 0, audio_nonzero = 0,
                  audio_direct_nonzero = 0;
    // Host-audio qualification stats over the final mono mix (CapSample::mixed).
    std::uint64_t au_a_nz = 0, au_b_nz = 0, au_n = 0, au_clip = 0, au_changes = 0, au_windows = 0, au_active_windows = 0;
    std::int64_t au_min = 0, au_max = 0;
    long double au_sumsq = 0.0L, au_sum = 0.0L;
    std::int16_t au_prev = 0;
    std::int16_t au_win_min = 0, au_win_max = 0;
    std::uint64_t au_win_n = 0;
    std::set<std::int16_t> au_distinct;
    std::vector<std::int16_t> au_wav;
    const std::string au_wav_path = env_or("MZM_NES_AUDIO_WAV", "");
    std::uint64_t prev_frame = ~0ull;
    std::uint16_t cur_keys = 0x03FFu;
    bool keys_pressed_logged = false;
    std::uint64_t last_screen_hash = 0, screen_changes = 0;
    std::uint64_t last_mem_hash = 0, mem_changes = 0;
    std::uint64_t frame_first_dma = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> screen_log;  // (frame, fb hash)
    bool ran_input = false;
    std::uint64_t max_call_depth = 0, max_irq_depth = 0;
    std::uint64_t sram_write_frames = 0;
    std::vector<std::uint8_t> sram_prev = sram0;
    const bool sram_trace = env_or("MZM_NES_SRAM_TRACE", "0") == "1";
    const std::uint64_t sram_every = sram_trace ? 1 : 30;

    struct TraceEnt { std::uint64_t frame; std::uint32_t pc, lr, sp, cpsr; };
    std::vector<TraceEnt> trace_ring(24);
    const bool watch_verify = env_or("MZM_NES_WATCH_VERIFY", "1") == "1";
    std::uint64_t last_verify_fail = 0;
    const bool watch_part1 = env_or("MZM_NES_WATCH_PART1", "0") == "1";
    const std::uint64_t watch_from = std::strtoull(env_or("MZM_NES_WATCH_FROM", "0").c_str(), nullptr, 10);
    std::size_t last_p1_dirty = 0;
    std::size_t trace_pos = 0;
    auto fb_hash = [&]() -> std::uint64_t {
        if (!ppu.has_latched_framebuffer()) return 0;
        return fnv(ppu.latched_framebuffer(), gba::GbaPpu::kScreenWidth * gba::GbaPpu::kScreenHeight * 3u);
    };
    auto mem_hash = [&]() -> std::uint64_t {
        std::uint64_t h = fnv(bus.vram_ptr(), 0x18000);
        h = fnv(bus.pal_ptr(), 0x400, h);
        return fnv(bus.oam_ptr(), 0x400, h);
    };
    auto resolver_failures = [&]() -> std::uint64_t {
        mzm_nes_emulator_part_stats_t st[mzm_nes_emulator::kNumParts];
        mzm_nes_emulator_part_stats(st, mzm_nes_emulator::kNumParts);
        std::uint64_t f = 0;
        for (const auto& s : st) f += s.verify_failures + s.invoke_failures + s.no_corpus;
        return f;
    };
    auto strict_counters = [&](std::uint64_t& misses, std::uint64_t& interp, std::uint64_t& unm,
                               std::uint64_t& iou) {
        misses = gbarecomp::self_heal_any_misses() ? 1 : 0;
        interp = gbarecomp::self_heal_interpreted_insns();
        unm = bus.unmapped_count();
        iou = bus.io().unmapped_count();
    };
    auto strict_ok = [&]() {
        std::uint64_t a, b, c, d;
        strict_counters(a, b, c, d);
        return a == 0 && b == 0 && c == 0 && d == 0;
    };
    auto checkpoint = [&](const char* tag, std::uint64_t frame) {
        std::uint64_t m, i, u, io;
        strict_counters(m, i, u, io);
        std::printf("NES3 %s frame=%llu pc=0x%08X cpsr=0x%08X misses=%llu interp=%llu unmapped=%llu "
                    "io_unhandled=%llu irq_vec=%llu irq_handler=%llu host_depth=%u irq_depth=%u "
                    "resolver_fail=%llu keyinput=0x%04X fb=%016llX\n",
                    tag, (unsigned long long)frame, g_cpu.R[15], g_cpu.cpsr, (unsigned long long)m,
                    (unsigned long long)i, (unsigned long long)u, (unsigned long long)io,
                    (unsigned long long)g_irq_vector_entries,
                    (unsigned long long)g_entries[kIrqHandler], runtime_call_stack_depth(),
                    g_irq_nest_depth, (unsigned long long)resolver_failures(),
                    bus.io().read16(0x130), (unsigned long long)fb_hash());
        std::fflush(stdout);
    };
    auto sram_report = [&](const char* tag, std::uint64_t frame) {
        const auto now = bus.save().sram_bytes();
        std::printf("NES3 sram %s frame=%llu hash=%016llX nes_slot_a=%s nes_slot_b=%s mzm_check=%s\n", tag,
                    (unsigned long long)frame, (unsigned long long)fnv(now.data(), now.size()),
                    hex_bytes(now.data() + 0x7FB0, 0x28).c_str(), hex_bytes(now.data() + 0x7FD8, 0x28).c_str(),
                    hex_bytes(now.data() + 0x7F80, 16).c_str());
    };
    auto nes_report = [&](std::uint64_t frame) {
        // sPasswordBytes and the password screen's 24 character tiles
        // (nametable 0x06002000, entries 0x109+{0..5,7..12,0x40..0x45,0x47..0x4C}).
        const std::vector<std::uint8_t> pw = read_guest(kPasswordBytes, 18);
        const std::uint16_t* nt = reinterpret_cast<const std::uint16_t*>(bus.vram_ptr() + 0x2000);
        std::string chars;
        char b[8];
        static const int idx[24] = {0,1,2,3,4,5, 7,8,9,10,11,12, 0x40,0x41,0x42,0x43,0x44,0x45, 0x47,0x48,0x49,0x4A,0x4B,0x4C};
        for (int i = 0; i < 24; ++i) { std::snprintf(b, sizeof b, "%02X", nt[0x109 + idx[i]] & 0xFF); chars += b; }
        const bool pw_screen = std::memcmp(nt + 0xA8, kPasswordPleaseTiles, sizeof kPasswordPleaseTiles) == 0;
        std::printf("NES3 nes_password frame=%llu pw_please_screen=%d password_bytes=%s screen_chars=%s\n",
                    (unsigned long long)frame, pw_screen ? 1 : 0, hex_bytes(pw.data(), 18).c_str(), chars.c_str());
    };
    // NON-QUALIFYING corruption control (MZM_NES_CORRUPT_SRAM="frame:offset:xormask" in hex/dec):
    // flips one byte of the harness' local SRAM copy. Never set by the qualifying runs.
    std::uint64_t corrupt_frame = ~0ull; std::uint32_t corrupt_off = 0; std::uint32_t corrupt_xor = 0;
    {
        const std::string c = env_or("MZM_NES_CORRUPT_SRAM", "");
        if (!c.empty()) {
            unsigned long long f; unsigned o, x;
            if (std::sscanf(c.c_str(), "%llu:%x:%x", &f, &o, &x) == 3) { corrupt_frame = f; corrupt_off = o; corrupt_xor = x; }
        }
    }
    // Frame-boundary work: input injection, audio capture window, sampling.
    auto on_new_frame = [&](std::uint64_t frame) {
        // Diagnostic (MZM_NES_DIAG_FRAMES="from,count"): guest state at each frame boundary.
        if (diag_count && frame >= diag_from && frame < diag_from + diag_count)
            std::printf("NES3 diag frame=%llu pc=0x%08X cpsr=0x%08X r0=0x%08X r1=0x%08X vcount=%u dispstat=0x%04X halted=%d\n",
                        (unsigned long long)frame, g_cpu.R[15], g_cpu.cpsr, g_cpu.R[0], g_cpu.R[1],
                        (unsigned)ppu.vcount(), (unsigned)bus.read16(0x04000004u), bus.io().halted() ? 1 : 0);
        // Verify save/load calls of the previous frame against what they intended to move.
        for (; g_save_checked < g_save_calls.size(); ++g_save_checked) {
            const SaveCall& c = g_save_calls[g_save_checked];
            const std::string n = c.name;
            if (n == "SaveToSram") {
                const auto sram = bus.save().sram_bytes();
                const std::uint32_t off = c.r[2] - 0x0E000000u;
                const bool ok = off + 0x28 <= sram.size() &&
                                std::memcmp(sram.data() + off, c.payload.data(), 0x28) == 0;
                std::printf("NES3 save_verify SaveToSram call_frame=%llu sram_off=0x%04X bytes_match=%d\n",
                            (unsigned long long)c.frame, off, ok ? 1 : 0);
            }
        }
        // Input: same set_keyinput call as the production loop, one poll/frame.
        std::uint16_t pressed = 0;
        for (const auto& e : input)
            if (frame >= e.frame && frame < e.frame + e.hold) pressed |= e.pressed;
        const std::uint16_t keys = static_cast<std::uint16_t>(~pressed & 0x03FFu);
        if (keys != cur_keys) {
            bus.io().set_keyinput(keys);
            std::printf("NES3 input frame=%llu keyinput=0x%04X (host set_keyinput)\n",
                        (unsigned long long)frame, keys);
            cur_keys = keys;
            keys_pressed_logged = true;
        }
        max_call_depth = std::max<std::uint64_t>(max_call_depth, runtime_call_stack_depth());
        max_irq_depth = std::max<std::uint64_t>(max_irq_depth, g_irq_nest_depth);
        // Audio capture window since last frame.
        const std::uint64_t gen = bus.audio().samples_generated();
        std::uint64_t start = audio_next;
        if (start < bus.audio().capture_oldest_index()) start = bus.audio().capture_oldest_index();
        std::vector<gba::GbaAudio::CapSample> buf(4096);
        while (start < gen) {
            std::uint64_t first = 0;
            const std::size_t n = bus.audio().query_capture(start, std::min<std::uint64_t>(gen - start, buf.size()),
                                                            buf.data(), first);
            if (n == 0) break;
            for (std::size_t k = 0; k < n; ++k) {
                audio_hash = fnv(reinterpret_cast<const std::uint8_t*>(&buf[k]), sizeof(buf[k]), audio_hash);
                if (buf[k].mixed != 0) ++audio_nonzero;
                if (buf[k].direct_a != 0 || buf[k].direct_b != 0) ++audio_direct_nonzero;
                {
                    const std::int16_t v = buf[k].mixed;
                    if (buf[k].direct_a != 0) ++au_a_nz;
                    if (buf[k].direct_b != 0) ++au_b_nz;
                    if (au_n == 0) { au_min = au_max = v; }
                    au_min = std::min<std::int64_t>(au_min, v);
                    au_max = std::max<std::int64_t>(au_max, v);
                    if (v >= 32767 || v <= -32768) ++au_clip;
                    if (au_n && v != au_prev) ++au_changes;
                    au_prev = v;
                    au_sum += v;
                    au_sumsq += static_cast<long double>(v) * v;
                    if (au_distinct.size() < 65536) au_distinct.insert(v);
                    if (!au_wav_path.empty()) au_wav.push_back(v);
                    // 32768-sample windows (1 s): does the output vary inside them?
                    if (au_win_n == 0) au_win_min = au_win_max = v;
                    au_win_min = std::min(au_win_min, v);
                    au_win_max = std::max(au_win_max, v);
                    if (++au_win_n == 32768) {
                        ++au_windows;
                        if (au_win_min != au_win_max) ++au_active_windows;
                        au_win_n = 0;
                    }
                    ++au_n;
                }
            }
            start = first + n;
        }
        audio_next = gen;
        if (frame % 10 == 0) {
            const std::uint64_t sh = fb_hash(), mh = mem_hash();
            if (sh != last_screen_hash) { ++screen_changes; last_screen_hash = sh; screen_log.push_back({frame, sh}); }
            if (mh != last_mem_hash) { ++mem_changes; last_mem_hash = mh; }
        }
        // SRAM audit (cheap: compare every 30 frames).
        if (frame % sram_every == 0) {
            const auto now = bus.save().sram_bytes();
            if (now != sram_prev) {
                ++sram_write_frames;
                if (sram_trace) {
                    for (std::size_t o = 0; o < now.size();) {
                        if (now[o] == sram_prev[o]) { ++o; continue; }
                        std::size_t e = o;
                        while (e < now.size() && now[e] != sram_prev[e]) ++e;
                        std::printf("NES3 sram_range frame=%llu [0x%04zX,0x%04zX) %zu bytes\n",
                                    (unsigned long long)frame, o, e, e - o);
                        o = e;
                    }
                }
                for (std::size_t o = 0; o < now.size(); ++o)
                    if (now[o] != sram_prev[o]) {
                        std::printf("NES3 sram_write frame=%llu off=0x%04zX 0x%02X->0x%02X\n",
                                    (unsigned long long)frame, o, sram_prev[o], now[o]);
                        break;
                    }
                sram_prev = now;
            }
        }
        if (watch_part1 && frame >= watch_from) {
            const auto& spec = mzm_nes_emulator::kParts[0];
            const std::uint8_t* guest = region_ptr(bus, spec.start);
            std::size_t total = 0;
            for (std::size_t o = 0; o < spec.size; ++o)
                if (guest[o] != pristine[0][o] && !(o >= 0x700 && o < 0x702)) ++total;
            if (total != last_p1_dirty) {
                std::printf("NES3 part1_dirty frame=%llu dirty_bytes=%zu (excl 0x06006700..02) pc=0x%08X\n",
                            (unsigned long long)frame, total, g_cpu.R[15]);
                last_p1_dirty = total;
            }
        }
        if (hash_frames.count(frame)) {
            checkpoint("hash", frame);
            if (env_or("MZM_NES_SRAM_REPORT", "0") == "1") { sram_report("hash", frame); nes_report(frame); }
            std::printf("NES3 hashinfo frame=%llu mem=%016llX audio_samples=%llu audio_hash=%016llX\n",
                        (unsigned long long)frame, (unsigned long long)mem_hash(),
                        (unsigned long long)gen, (unsigned long long)audio_hash);
            if (!dump_dir.empty() && ppu.has_latched_framebuffer()) {
                char name[64];
                std::snprintf(name, sizeof(name), "/nes3_frame_%05llu.ppm", (unsigned long long)frame);
                std::ofstream out(dump_dir + name, std::ios::binary);
                out << "P6\n240 160\n255\n";
                out.write(reinterpret_cast<const char*>(ppu.latched_framebuffer()),
                          gba::GbaPpu::kScreenWidth * gba::GbaPpu::kScreenHeight * 3u);
            }
        } else if (ckpt_every && frame % ckpt_every == 0) {
            checkpoint("ckpt", frame);
        }
    };

    std::printf("NES3 begin frames=%llu ckpt=%llu inputs=%zu\n", (unsigned long long)max_frames,
                (unsigned long long)ckpt_every, input.size());
    try {
        while (ppu.frame_count() < max_frames) {
            const std::uint64_t frame = ppu.frame_count();
            if (frame != prev_frame) {
                prev_frame = frame;
                on_new_frame(frame);
                if (!strict_ok()) { stalled = true; stall_pc = g_cpu.R[15]; break; }
            }
            if (frame >= corrupt_frame && corrupt_frame != ~0ull && !bus.io().halted()) {
                const std::uint8_t before = bus.save().sram_read(corrupt_off);
                bus.save().sram_write(corrupt_off, static_cast<std::uint8_t>(before ^ corrupt_xor));
                std::printf("NES3 CORRUPTION_CONTROL (non-qualifying) frame=%llu sram[0x%04X] 0x%02X->0x%02X\n",
                            (unsigned long long)frame, corrupt_off, before,
                            (unsigned)bus.save().sram_read(corrupt_off));
                corrupt_frame = ~0ull;
            }
            if (next_enter < enter_at.size() && frame >= enter_at[next_enter] && !bus.io().halted()) {
                sram_report("pre_enter", frame);
                std::printf("NES3 enter_nes frame=%llu (loader entry, as OptionsNesMetroidHandler stage 4)\n",
                            (unsigned long long)frame);
                enter_nes_loader();
                ++next_enter;
                no_progress = 0;
                continue;
            }
            if (bus.io().halted()) {
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
                continue;
            }
            const std::uint32_t pc = g_cpu.R[15];
            const auto before = g_cpu;
            const auto cycles_before = g_runtime_cycles;
            trace_ring[trace_pos++ % trace_ring.size()] = {frame, pc, g_cpu.R[14], g_cpu.R[13], g_cpu.cpsr};
            runtime_dispatch(pc);
            ++dispatches;
            if (watch_verify) {
                mzm_nes_emulator_part_stats_t st[mzm_nes_emulator::kNumParts];
                mzm_nes_emulator_part_stats(st, mzm_nes_emulator::kNumParts);
                std::uint64_t vf = 0;
                for (const auto& x : st) vf += x.verify_failures;
                if (vf != last_verify_fail) {
                    last_verify_fail = vf;
                    std::printf("NES3 verify_fail frame=%llu dispatch_from_pc=0x%08X lr=0x%08X now_pc=0x%08X\n",
                                (unsigned long long)frame, pc, before.R[14], g_cpu.R[15]);
                    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
                        const auto& spec = mzm_nes_emulator::kParts[i];
                        const std::uint8_t* guest = region_ptr(bus, spec.start);
                        std::size_t total = 0;
                        std::uint32_t first = 0;
                        for (std::size_t o = 0; o < spec.size; ++o)
                            if (guest[o] != pristine[i][o]) { if (!total) first = spec.start + o; ++total; }
                        std::printf("NES3   %s dirty=%zu first=0x%08X\n", spec.name, total, first);
                    }
                }
            }
            if (gbarecomp::self_heal_any_misses()) { stalled = true; stall_pc = pc; break; }
            if (std::memcmp(&before, &g_cpu, sizeof(g_cpu)) == 0 && g_runtime_cycles == cycles_before &&
                !bus.io().halted())
                ++no_progress;
            else
                no_progress = 0;
            if (no_progress >= 4) { stalled = true; stall_pc = pc; break; }
        }
    } catch (const EmulatorFrontier& f) {
        have_frontier = true;
        frontier = f;
    }
    (void)ran_input; (void)keys_pressed_logged; (void)frame_first_dma; (void)g_last_sram_pc;

    const std::uint64_t final_frame = ppu.frame_count();
    checkpoint("final", final_frame);

    // ---- report ----
    std::uint64_t m, ii, u, io;
    strict_counters(m, ii, u, io);
    mzm_nes_emulator_part_stats_t stats[mzm_nes_emulator::kNumParts];
    mzm_nes_emulator_part_stats(stats, mzm_nes_emulator::kNumParts);
    for (const auto& st : stats)
        std::printf("NES3 part %s attempts=%llu verified=%llu matches=%llu verify_fail=%llu "
                    "invoke_fail=%llu no_corpus=%llu\n", st.name, (unsigned long long)st.attempts,
                    (unsigned long long)st.verified, (unsigned long long)st.matches,
                    (unsigned long long)st.verify_failures, (unsigned long long)st.invoke_failures,
                    (unsigned long long)st.no_corpus);
    for (const auto& kv : kWatched)
        std::printf("NES3 entries %s(0x%08X)=%llu\n", kv.second, kv.first,
                    (unsigned long long)g_entries[kv.first]);
    std::printf("NES3 screen_changes=%llu mem_changes=%llu screen_log_size=%zu\n",
                (unsigned long long)screen_changes, (unsigned long long)mem_changes, screen_log.size());
    for (std::size_t k = 0; k < screen_log.size() && k < 4000; ++k)
        if (env_or("MZM_NES_SCREEN_LOG", "0") == "1")
            std::printf("NES3 screen frame=%llu fb=%016llX\n", (unsigned long long)screen_log[k].first,
                        (unsigned long long)screen_log[k].second);
    std::printf("NES3 audio samples=%llu nonzero_mixed=%llu direct_nonzero=%llu hash=%016llX "
                "soundcnt_h=0x%04X dma1_runs=%zu dma1_words=%zu dma1_sad=0x%08X dma1_dad=0x%08X "
                "dma1_cnt=0x%08X timer0_cnt=0x%08X timer1_cnt=0x%08X\n",
                (unsigned long long)bus.audio().samples_generated(),
                (unsigned long long)audio_nonzero, (unsigned long long)audio_direct_nonzero,
                (unsigned long long)audio_hash, bus.io().read16(0x082), bus.io().dma_runs(1),
                bus.io().dma_words(1), bus.io().read32(0x0BC), bus.io().read32(0x0C0),
                bus.io().read32(0x0C4), bus.io().read32(0x100), bus.io().read32(0x104));
    {
        const long double mean = au_n ? au_sum / au_n : 0.0L;
        const long double rms = au_n ? std::sqrt(static_cast<double>(au_sumsq / au_n)) : 0.0L;
        const long double var = au_n ? au_sumsq / au_n - mean * mean : 0.0L;
        std::printf("NES3 audio_stats rate=%u channels=1 n=%llu nonzero=%llu min=%lld max=%lld "
                    "rms=%.3f mean=%.3f stddev=%.3f clip=%llu changes=%llu distinct=%llu "
                    "windows=%llu active_windows=%llu dma1_words=%llu dma2_words=%llu "
                    "dma1_runs=%llu dma2_runs=%llu fifoA_nonzero=%llu fifoB_nonzero=%llu dma1_cnt_h=0x%04X dma2_cnt_h=0x%04X\n",
                    bus.audio().sample_rate(), (unsigned long long)au_n,
                    (unsigned long long)audio_nonzero, (long long)au_min, (long long)au_max,
                    static_cast<double>(rms), static_cast<double>(mean),
                    std::sqrt(static_cast<double>(var > 0 ? var : 0)), (unsigned long long)au_clip,
                    (unsigned long long)au_changes, (unsigned long long)au_distinct.size(),
                    (unsigned long long)au_windows, (unsigned long long)au_active_windows,
                    (unsigned long long)bus.io().dma_words(1), (unsigned long long)bus.io().dma_words(2),
                    (unsigned long long)bus.io().dma_runs(1), (unsigned long long)bus.io().dma_runs(2),
                    (unsigned long long)au_a_nz, (unsigned long long)au_b_nz,
                    bus.io().read16(0xC6), bus.io().read16(0xD2));
    }
    if (!au_wav_path.empty()) {
        // Local QA artifact only (game-derived audio is never committed).
        std::ofstream w(au_wav_path, std::ios::binary);
        auto le32 = [&](std::uint32_t v) { w.write(reinterpret_cast<const char*>(&v), 4); };
        auto le16 = [&](std::uint16_t v) { w.write(reinterpret_cast<const char*>(&v), 2); };
        const std::uint32_t bytes = static_cast<std::uint32_t>(au_wav.size() * 2);
        w.write("RIFF", 4); le32(36 + bytes); w.write("WAVEfmt ", 8); le32(16); le16(1); le16(1);
        le32(bus.audio().sample_rate()); le32(bus.audio().sample_rate() * 2); le16(2); le16(16);
        w.write("data", 4); le32(bytes);
        w.write(reinterpret_cast<const char*>(au_wav.data()), bytes);
    }
    if (g_audio_diag) {
        for (const auto& kv : g_apu_regs)
            std::printf("NES3 apu_reg 0x%08X writes=%llu\n", kv.first, (unsigned long long)kv.second);
        const std::uint8_t* b = bus.iwram_ptr() + 0x5DD0;
        std::size_t nz = 0;
        for (int k = 0; k < 0x300; ++k) nz += b[k] != 0;
        std::printf("NES3 audio_buf nonzero_bytes=%zu/768 (IWRAM 0x03005DD0..)\n", nz);
        const auto fa = bus.audio().debug_fifo_state(0);
        std::printf("NES3 audio_regs soundcnt_l=0x%04X soundcnt_h=0x%04X soundcnt_x=0x%04X "
                    "soundbias=0x%04X fifoA count=%u w=%u r=%u samples_per_event=%u\n",
                    bus.io().read16(0x080), bus.io().read16(0x082), bus.io().read16(0x084),
                    bus.audio().debug_soundbias(), fa.count, fa.write, fa.read,
                    bus.audio().debug_samples_per_event());
        const std::uint8_t* c = bus.iwram_ptr() + 0x5F10;
        nz = 0;
        for (int k = 0; k < 0x100; ++k) nz += c[k] != 0;
        std::printf("NES3 audio_buf2 nonzero_bytes=%zu/256 (IWRAM 0x03005F10..)\n", nz);
    }
    if (watch_p1_writes) {
        for (const auto& kv : g_p1_watch.chunks)
            std::printf("NES3 p1_write chunk=0x%08X first_frame=%llu first_pc=0x%08X widths=0x%X changes=%llu\n",
                        kv.first, (unsigned long long)kv.second.first_frame, kv.second.first_pc,
                        kv.second.widths, (unsigned long long)kv.second.count);
        std::printf("NES3 p1_write_range min=0x%08X max=0x%08X\n", g_p1_watch.min_addr, g_p1_watch.max_addr);
        for (const auto& kv : g_p1_watch.writer_pcs)
            std::printf("NES3 p1_writer pc=0x%08X changes=%llu\n", kv.first, (unsigned long long)kv.second);
        for (const auto& kv : g_p1_watch.p6_words)
            std::printf("NES3 p6_write addr=0x%08X width=%u first_frame=%llu last_frame=%llu first_pc=0x%08X "
                        "first_old=0x%X first_new=0x%X last_new=0x%X changes=%llu\n",
                        kv.first, kv.second.width, (unsigned long long)kv.second.first_frame,
                        (unsigned long long)kv.second.last_frame, kv.second.first_pc, kv.second.first_old,
                        kv.second.first_new, kv.second.last_new, (unsigned long long)kv.second.count);
        for (const auto& kv : g_p1_watch.p6_writer_pcs)
            std::printf("NES3 p6_writer pc=0x%08X changes=%llu\n", kv.first, (unsigned long long)kv.second);
    }
    {   // Final SRAM diff against the erased (0xFF) start: which bytes the save wrote.
        const auto now = bus.save().sram_bytes();
        std::printf("NES3 sram_diff");
        std::size_t total = 0;
        for (std::size_t i = 0; i < now.size() && i < sram0.size();) {
            if (now[i] == sram0[i]) { ++i; continue; }
            std::size_t j = i;
            while (j < now.size() && j < sram0.size() && now[j] != sram0[j]) ++j;
            std::printf(" [0x%04zX,0x%04zX)", i, j);
            total += j - i;
            i = j;
        }
        std::printf(" changed_bytes=%zu\n", total);
    }
    std::printf("NES3 sram write_frames=%llu dirty=%d\n", (unsigned long long)sram_write_frames,
                bus.save().dirty() ? 1 : 0);
    std::printf("NES3 depth max_host=%llu max_irq=%llu final_host=%u final_irq=%u\n",
                (unsigned long long)max_call_depth, (unsigned long long)max_irq_depth,
                runtime_call_stack_depth(), g_irq_nest_depth);
    {
        mzm_stack_helper_stats_t hs[4];
        std::uint64_t rejects = 0;
        const std::size_t nh = mzm_stack_helper_stats(hs, 4, &rejects);
        for (std::size_t i = 0; i < nh; ++i)
            std::printf("NES3 stack_helper_stats %s attempts=%llu matches=%llu mirror_matches=%llu\n", hs[i].name,
                        (unsigned long long)hs[i].attempts, (unsigned long long)hs[i].matches,
                        (unsigned long long)hs[i].mirror_matches);
        std::printf("NES3 stack_helper_rejects=%llu\n", (unsigned long long)rejects);
        rlimit rl{};
        getrlimit(RLIMIT_STACK, &rl);
        const std::uintptr_t low = mzm_ram_dispatch_stack_low();
        std::printf("NES3 host_stack limit=%llu hook_depth_bytes=%llu\n", (unsigned long long)rl.rlim_cur,
                    low == ~static_cast<std::uintptr_t>(0) ? 0ull : (unsigned long long)(stack_base - low));
    }
    {
        const std::string save_dump = env_or("MZM_NES_SAVE_DUMP", "");
        if (!save_dump.empty()) {
            const auto bytes = bus.save().sram_bytes();
            std::ofstream o(save_dump, std::ios::binary);
            o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            std::printf("NES3 save_dumped bytes=%zu hash=%016llX\n", bytes.size(),
                        (unsigned long long)fnv(bytes.data(), bytes.size()));
        }
        sram_report("end", ppu.frame_count());
    }
    std::printf("NES3 strict dispatch_misses=%llu interpreted_insns=%llu unmapped=%llu "
                "io_unhandled=%llu self_heal=disabled\n", (unsigned long long)m,
                (unsigned long long)ii, (unsigned long long)u, (unsigned long long)io);

    if (have_frontier || stalled) {
        // Frontier evidence: how each Part differs from the ROM-derived image,
        // the recorded misses, and the frame at the stop.
        for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
            const auto& spec = mzm_nes_emulator::kParts[i];
            const std::uint8_t* guest = region_ptr(bus, spec.start);
            std::size_t total = 0;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
            for (std::size_t o = 0; o < spec.size; ++o) {
                if (guest[o] == pristine[i][o]) continue;
                ++total;
                const std::uint32_t a = spec.start + static_cast<std::uint32_t>(o);
                if (!runs.empty() && runs.back().second == a) runs.back().second = a + 1;
                else runs.push_back({a, a + 1});
            }
            std::printf("NES3 stop_diff %s dirty_bytes=%zu runs=%zu", spec.name, total, runs.size());
            for (std::size_t r = 0; r < runs.size() && r < 10; ++r)
                std::printf(" [0x%08X,0x%08X)", runs[r].first, runs[r].second);
            std::printf("\n");
            std::printf("NES3 stop_buckets %s dirty per 0x100:", spec.name);
            for (std::size_t b = 0; b < spec.size; b += 0x100) {
                std::size_t n = 0;
                for (std::size_t o = b; o < b + 0x100 && o < spec.size; ++o) n += guest[o] != pristine[i][o];
                std::printf(" %03zX:%zu", b, n);
            }
            std::printf("\n");
        }
        for (std::size_t k = 0; k < trace_ring.size(); ++k) {
            const auto& t = trace_ring[(trace_pos + k) % trace_ring.size()];
            if (trace_pos < trace_ring.size() && k < trace_ring.size() - trace_pos) continue;
            std::printf("NES3 stop_trace frame=%llu pc=0x%08X lr=0x%08X sp=0x%08X cpsr=0x%08X\n",
                        (unsigned long long)t.frame, t.pc, t.lr, t.sp, t.cpsr);
        }
        std::printf("NES3 stop_misses %s\n", gbarecomp::self_heal_misses_json().c_str());
        // First miss: guest CPU state and the bytes at its PC. If the bytes are a
        // copy of a Part image window (code copied to RAM at run time, e.g. the
        // SRAM routines copied to the stack), name the source.
        std::printf("NES3 stop_cpu");
        for (int r = 0; r < 16; ++r) std::printf(" r%d=0x%08X", r, g_cpu.R[r]);
        std::printf(" cpsr=0x%08X\n", g_cpu.cpsr);
        {
            const std::string js = gbarecomp::self_heal_misses_json();
            const auto at = js.find("\"pc\":\"0x");
            if (at != std::string::npos) {
                const std::uint32_t mpc = static_cast<std::uint32_t>(std::strtoul(js.c_str() + at + 8, nullptr, 16));
                std::vector<std::uint8_t> win(0x40);
                for (std::size_t k = 0; k < win.size(); ++k) win[k] = bus_read_u8(mpc + static_cast<std::uint32_t>(k));
                std::printf("NES3 stop_miss_bytes pc=0x%08X:", mpc);
                for (auto b : win) std::printf(" %02X", b);
                std::printf("\n");
                for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
                    const auto& img = pristine[i];
                    const auto it = std::search(img.begin(), img.end(), win.begin(), win.begin() + 0x20);
                    if (it != img.end())
                        std::printf("NES3 stop_miss_source first 0x20 bytes equal %s image offset 0x%zX (guest 0x%08X)\n",
                                    mzm_nes_emulator::kParts[i].name, static_cast<std::size_t>(it - img.begin()),
                                    mzm_nes_emulator::kParts[i].start + static_cast<std::uint32_t>(it - img.begin()));
                }
            }
        }
        if (!dump_dir.empty() && ppu.has_latched_framebuffer()) {
            std::ofstream out(dump_dir + "/nes3_stop.ppm", std::ios::binary);
            out << "P6\n240 160\n255\n";
            out.write(reinterpret_cast<const char*>(ppu.latched_framebuffer()),
                      gba::GbaPpu::kScreenWidth * gba::GbaPpu::kScreenHeight * 3u);
        }
    }
    if (have_frontier) {
        std::printf("NES3 FRONTIER frame=%llu target_pc=0x%08X %s part=%s reason=%s cpu_pc=0x%08X cpsr=0x%08X\n",
                    (unsigned long long)final_frame, frontier.event.pc,
                    frontier.event.thumb ? "Thumb" : "ARM", frontier.event.part,
                    frontier.event.reason, g_cpu.R[15], g_cpu.cpsr);
        return stop_on_frontier ? 3 : 0;
    }
    if (stalled) {
        std::printf("NES3 STALL frame=%llu pc=0x%08X misses=%llu interp=%llu unmapped=%llu io_unhandled=%llu\n",
                    (unsigned long long)final_frame, stall_pc, (unsigned long long)m,
                    (unsigned long long)ii, (unsigned long long)u, (unsigned long long)io);
        return 3;
    }
    std::printf("NES3 DONE frames=%llu no_frontier strict=clean\n", (unsigned long long)final_frame);
    return 0;
}
