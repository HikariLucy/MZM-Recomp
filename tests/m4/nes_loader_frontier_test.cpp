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

namespace {
constexpr std::uint32_t kTrampoline = 0x087D8000u;
constexpr std::uint32_t kLoader = 0x087D80D4u;
constexpr std::uint32_t kFrontier = 0x03007400u;
constexpr std::size_t kPayloadSize = 0x214u;
constexpr const char* kExpectedRomSha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8";
constexpr const char* kExpectedPayloadSha256 = "e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44";

bool reached = false;
std::uint32_t first_ram_pc = 0;
ArmCpuState frontier_cpu{};
std::array<std::uint8_t, kPayloadSize> guest_bytes{};
unsigned trampoline_entries = 0, loader_entries = 0;
bool stop_at_reset_reentry = false;
struct ResetReentry {};

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "NES-1a FAIL: %s\n", message);
    std::exit(1);
}

std::string sha256_hex(const std::uint8_t* data, std::size_t len) {
    auto rotr = [](std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); };
    std::uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    std::vector<std::uint8_t> msg(data, data + len);
    const std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8u;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[64];
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h_val + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;

            h_val = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
    }

    char buf[65];
    std::snprintf(buf, sizeof(buf),
                  "%08x%08x%08x%08x%08x%08x%08x%08x",
                  h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
    return std::string(buf);
}

int capture_frontier(std::uint32_t pc, int thumb) {
    if (!first_ram_pc) first_ram_pc = pc;
    require(pc == kFrontier && thumb == 0, "unexpected RAM dispatch before frontier");
    require(!reached, "frontier entered twice");
    reached = true;
    frontier_cpu = g_cpu;
    for (std::size_t i = 0; i < guest_bytes.size(); ++i)
        guest_bytes[i] = bus_read_u8(kFrontier + static_cast<std::uint32_t>(i));
    return 1; // Controlled expected unsupported frontier; payload is not run.
}

void record_entry(std::uint32_t pc) {
    if (stop_at_reset_reentry && pc == 0x08000000u) throw ResetReentry{};
    if (pc == kTrampoline) ++trampoline_entries;
    if (pc == kLoader) ++loader_entries;
}

std::vector<std::uint8_t> reconstruct(const std::vector<std::uint8_t>& rom) {
    std::size_t cursor = 0x7D8150u;
    require(cursor + 4u <= rom.size() && rom[cursor] == 0x10u,
            "wrong ROM LZ77 stream");
    const std::size_t size = static_cast<std::size_t>(rom[cursor + 1]) |
        (static_cast<std::size_t>(rom[cursor + 2]) << 8u) |
        (static_cast<std::size_t>(rom[cursor + 3]) << 16u);
    require(size == kPayloadSize, "wrong decompressed length");
    cursor += 4u;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    while (out.size() < size) {
        require(cursor < rom.size(), "truncated LZ77 flags");
        const std::uint8_t flags = rom[cursor++];
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            require(cursor < rom.size(), "truncated LZ77 token");
            if ((flags & (1u << bit)) == 0u) {
                out.push_back(rom[cursor++]);
                continue;
            }
            require(cursor + 1u < rom.size(), "truncated LZ77 back-reference");
            const auto hi = rom[cursor++];
            const auto lo = rom[cursor++];
            const std::size_t length = (hi >> 4u) + 3u;
            const std::size_t distance = ((hi & 15u) << 8u) + lo + 1u;
            require(distance <= out.size(), "invalid LZ77 back-reference");
            for (std::size_t j = 0; j < length && out.size() < size; ++j)
                out.push_back(out[out.size() - distance]);
        }
    }
    return out;
}

void write_bytes(const std::string& path, const std::uint8_t* data,
                 std::size_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data),
                 static_cast<std::streamsize>(size));
    require(static_cast<bool>(stream), "failed to save payload evidence");
}
} // namespace

int main(int argc, char** argv) {
    require(argc == 3 || argc == 4, "usage: nes_loader_frontier_test ROM BIOS [OUTPUT_PREFIX]");
    setenv("GBARECOMP_STRICT_STATIC", "1", 1);
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(input)), {});
    require(rom.size() == 0x800000u, "expected USA 8 MiB cartridge");
    require(gba::sha1(rom.data(), rom.size()).hex() == kExpectedRomSha1,
            "wrong ROM SHA-1: expected USA 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8");
    const auto expected = reconstruct(rom);
    require(expected.size() == kPayloadSize, "wrong reconstructed payload size");
    require(sha256_hex(expected.data(), expected.size()) == kExpectedPayloadSha256,
            "wrong reconstructed payload SHA-256");

    gba::GbaBios bios;
    std::string error;
    require(bios.load_from_file(argv[2], gba::GbaBios::kExpectedSha1, &error),
            "BIOS hash/load failure");
    gba::GbaBus bus;
    gba::GbaPpu ppu;
    bus.set_rom(rom.data(), rom.size());
    bus.set_bios(&bios);
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    gbarecomp::self_heal_reset();
    g_runtime_ram_dispatch_hook = capture_frontier;
    g_runtime_fn_entry_hook = record_entry;

    // Verify static entries generated for the NES islands
    require(runtime_has_static_entry(kTrampoline, 0), "trampoline missing (ARM)");
    require(runtime_has_static_entry(kLoader, 0), "loader missing (ARM)");
    require(runtime_has_static_entry(0x087D8110u, 0), "loader continuation missing (ARM)");
    require(runtime_has_static_entry(0x087D8124u, 0), "reset ARM entry missing (ARM)");
    require(runtime_has_static_entry(0x087D812Cu, 1), "reset Thumb stub missing (Thumb)");
    require(runtime_has_static_entry(0x087D813Eu, 1), "reset continuation missing (Thumb)");

    // Verify negative boundaries (data ranges, literal pools, dynamic frontier)
    require(!runtime_has_static_entry(kTrampoline + 4u, 0), "data entry published (ARM)");
    require(!runtime_has_static_entry(kTrampoline + 4u, 1), "data entry published (Thumb)");
    require(!runtime_has_static_entry(0x087D8114u, 0), "loader literal pool published (ARM)");
    require(!runtime_has_static_entry(0x087D8114u, 1), "loader literal pool published (Thumb)");
    require(!runtime_has_static_entry(kFrontier, 0), "payload unexpectedly static (ARM)");
    require(!runtime_has_static_entry(kFrontier, 1), "payload unexpectedly static (Thumb)");

    g_cpu = {};
    g_cpu.cpsr = 0x1Fu;
    g_cpu.R[0] = 0x08000000u;
    g_cpu.R[15] = kTrampoline;
    require(runtime_call_stack_depth() == 0u, "dirty initial host call stack");
    runtime_dispatch(kTrampoline);

    // SWI 0x11 exits its generated host body at the architecturally valid
    // return PC. The outer execution loop re-dispatches that continuation.
    require(g_cpu.R[15] == 0x087D8110u,
            "LZ77 did not return to loader continuation");
    require(runtime_call_stack_depth() == 0u,
            "LZ77 continuation retained a false host call frame");
    runtime_dispatch(g_cpu.R[15]);

    require(reached && first_ram_pc == kFrontier,
            "first unsupported PC was not 0x03007400 ARM");
    require(trampoline_entries == 1u && loader_entries == 1u,
            "ROM loader chain not entered exactly once");
    require(runtime_call_stack_depth() == 0u, "false host return frame remains");
    require(frontier_cpu.R[15] == kFrontier &&
            (frontier_cpu.cpsr & CPSR_T_BIT) == 0u,
            "wrong frontier PC or instruction mode");

    for (std::size_t i = 0; i < expected.size(); ++i)
        require(guest_bytes[i] == expected[i], "guest payload differs from local ROM reconstruction");

    require(sha256_hex(guest_bytes.data(), guest_bytes.size()) == kExpectedPayloadSha256,
            "guest payload SHA-256 mismatch");

    require(bus.unmapped_count() == 0u && bus.io().unmapped_count() == 0u,
            "unmapped or unhandled IO before frontier");
    require(!gbarecomp::self_heal_any_misses() &&
            gbarecomp::self_heal_interpreted_insns() == 0u,
            "dispatch miss or interpreted instruction before frontier");

    if (argc >= 4) {
        const std::string prefix = argv[3];
        write_bytes(prefix + "-guest.bin", guest_bytes.data(), guest_bytes.size());
        write_bytes(prefix + "-reconstructed.bin", expected.data(), expected.size());
    }

    std::printf("EXPECTED_UNSUPPORTED_FRONTIER pc=%08X mode=ARM ", kFrontier);
    std::printf("cpsr=%08X sp=%08X lr=%08X host_stack=%u\n",
                frontier_cpu.cpsr, frontier_cpu.R[13], frontier_cpu.R[14],
                runtime_call_stack_depth());
    std::printf("dispatch_misses=0 interpreted_insns=0 unmapped=%zu "
                "io_unhandled=%zu self_heal=disabled\n",
                bus.unmapped_count(), bus.io().unmapped_count());
    for (unsigned i = 0; i <= 12; ++i)
        std::printf("r%u=%08X%c", i, frontier_cpu.R[i], i == 12 ? '\n' : ' ');
    std::puts("NES-1a ROM loader and LZ77 payload extraction: PASS");

    // The quit stub first returns from RegisterRamReset at 0x087D813E,
    // then SoftReset must transfer through BIOS to cartridge re-entry.
    g_cpu = frontier_cpu;
    g_cpu.R[15] = 0x087D8124u;
    runtime_dispatch(g_cpu.R[15]);
    for (unsigned i = 0; i < 10000u && g_cpu.R[15] != 0x087D813Eu; ++i) {
        require(g_cpu.R[15] < 0x00004000u ||
                    (g_cpu.R[15] >= 0x087D8124u && g_cpu.R[15] < 0x087D8140u),
                "unexpected PC while servicing RegisterRamReset");
        runtime_dispatch(g_cpu.R[15]);
    }
    require(g_cpu.R[15] == 0x087D813Eu &&
            (g_cpu.cpsr & CPSR_T_BIT) != 0u,
            "RegisterRamReset failed to resume at Thumb SoftReset");
    stop_at_reset_reentry = true;
    bool reset_reentered = false;
    try { runtime_dispatch(g_cpu.R[15]); }
    catch (const ResetReentry&) { reset_reentered = true; }
    require(reset_reentered && g_cpu.R[15] == 0x08000000u &&
            (g_cpu.cpsr & CPSR_T_BIT) == 0u,
            "SoftReset failed to re-enter ROM in ARM mode");
    require(!runtime_has_static_entry(0x087D8140u, 1),
            "SoftReset literal pool published as code (Thumb)");
    require(!runtime_has_static_entry(0x087D8140u, 0),
            "SoftReset literal pool published as code (ARM)");
    std::puts("NES-1a quit stub RegisterRamReset -> SoftReset -> ROM re-entry: PASS");
    g_runtime_ram_dispatch_hook = nullptr;
    g_runtime_fn_entry_hook = nullptr;
    gbarecomp::set_active_ppu(nullptr);
    gbarecomp::set_active_bus(nullptr);
}
