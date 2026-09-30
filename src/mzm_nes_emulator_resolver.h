#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mzm_nes_emulator_map.h"
#include "mzm_nes_payload_resolver.h"

// Identity gate for the six NES emulator executable images (M4 NES-2).
//
// Each Part is a distinct byte image that the NES payload copies to a fixed
// guest address. A PC inside a Part's address range is executed natively only
// if the *live* guest bytes of that Part hash to the ROM-derived digest.
//
// Gate scope: each image is tiled by one or more verification scopes (canonical
// map [[image.scope]]; Parts without explicit scopes have one scope, "image").
// A scope's gate is its ranges minus runs proven mutable by runtime evidence.
// Literal pools are gated because generated code bakes their values in as
// constants. A PC selects its scope; the scope's `requires` closure (direct
// transfers that generated code follows without the resolver) is verified too.
// Part 1 has init/resident/menu scopes: the init half is overwritten by
// graphics after boot, so init and menu PCs fail closed from then on while
// resident PCs keep running (NES-3b). Being verified once never allows a PC
// whose own scope bytes no longer match.
//
// Lifecycle: there is deliberately NO permanent "verified" latch. The bus
// exposes no write notification, so a cached verdict could go stale if guest
// code overwrote an image; every entry re-hashes the gate runs. Performance is
// traded for correctness until an upstream write epoch exists.
namespace mzm_nes_emulator {

enum class ImageKind : int {
    None = -1,
    Part1 = 0,
    Part2,
    Part3,
    Part4,
    Part5,
    Part6,
};
constexpr std::size_t kNumParts = 6;
constexpr std::size_t kMaxScopes = 4;   // generator MAX_SCOPES

struct ImageSpec {
    ImageKind kind;
    const char* name;
    std::uint32_t start;
    std::size_t size;
    const ScopeSpec* scopes;
    std::size_t scope_count;
    // False when no AOT corpus exists for this Part. Such a Part is only
    // *identified*, never run.
    bool has_corpus;
    // Only classify this Part when a frontier observer is installed.
    bool observer_only;
};
// Each scope's `prefilter_sha256` covers its first kPrefilterBytes gated bytes:
// a cheap first stage so a PC that merely lies in the Part's address range
// (Part 2 aliases MZM's own IWRAM code) does not pay a full-scope hash.
constexpr std::size_t kPrefilterBytes = 32;

#define MZM_NES_PART(kind, name, start, size, has_corpus, observer_only)     \
    ImageSpec{ImageKind::kind, #name, start, size,                           \
              mzm_nes_emulator::k_##name##_scopes,                           \
              sizeof(mzm_nes_emulator::k_##name##_scopes) / sizeof(ScopeSpec), \
              has_corpus, observer_only}

inline const ImageSpec kParts[kNumParts] = {
    MZM_NES_PART(Part1, part1, 0x06006000u, 0x1240u, true, false),
    MZM_NES_PART(Part2, part2, 0x03000000u, 0x5A4Cu, true, false),
    MZM_NES_PART(Part3, part3, 0x0600B000u, 0x150u, true, false),
    MZM_NES_PART(Part4, part4, 0x0600C000u, 0x60u, true, false),
    MZM_NES_PART(Part5, part5, 0x0600E000u, 0xD88u, true, false),
    MZM_NES_PART(Part6, part6, 0x0203E000u, 0x8E0u, true, false),
};
#undef MZM_NES_PART

inline ImageKind classify_pc(std::uint32_t pc, bool observer_installed) {
    for (const auto& spec : kParts) {
        if (spec.observer_only && !observer_installed) continue;
        if (pc >= spec.start && pc < spec.start + spec.size) return spec.kind;
    }
    return ImageKind::None;
}

inline const ImageSpec& spec_of(ImageKind kind) {
    return kParts[static_cast<std::size_t>(kind)];
}

// Decomp code runs (test classification only; not used by the gate).
inline const CodeRun* code_runs(ImageKind kind) {
    switch (kind) {
        case ImageKind::Part1: return k_part1_code;
        case ImageKind::Part2: return k_part2_code;
        case ImageKind::Part3: return k_part3_code;
        case ImageKind::Part4: return k_part4_code;
        case ImageKind::Part5: return k_part5_code;
        case ImageKind::Part6: return k_part6_code;
        default: return nullptr;
    }
}
inline std::size_t code_run_count(ImageKind kind) {
    switch (kind) {
        case ImageKind::Part1: return sizeof(k_part1_code) / sizeof(CodeRun);
        case ImageKind::Part2: return sizeof(k_part2_code) / sizeof(CodeRun);
        case ImageKind::Part3: return sizeof(k_part3_code) / sizeof(CodeRun);
        case ImageKind::Part4: return sizeof(k_part4_code) / sizeof(CodeRun);
        case ImageKind::Part5: return sizeof(k_part5_code) / sizeof(CodeRun);
        case ImageKind::Part6: return sizeof(k_part6_code) / sizeof(CodeRun);
        default: return 0;
    }
}

// Index of the scope owning `pc` (scopes tile the image), or -1.
inline int scope_of(const ImageSpec& spec, std::uint32_t pc) {
    for (std::size_t i = 0; i < spec.scope_count; ++i)
        for (std::size_t r = 0; r < spec.scopes[i].pc_count; ++r)
            if (pc >= spec.scopes[i].pc[r].start && pc < spec.scopes[i].pc[r].end)
                return static_cast<int>(i);
    return -1;
}

// Full gate of one scope: cheap prefilter, then the complete SHA-256.
template <typename ReadByte>
bool verify_scope(const ScopeSpec& sc, ReadByte read_byte) {
    // Stage 1: the first kPrefilterBytes gated bytes.
    {
        std::vector<std::uint8_t> head;
        for (std::size_t r = 0; r < sc.gate_count && head.size() < kPrefilterBytes; ++r)
            for (std::uint32_t a = sc.gate[r].start;
                 a < sc.gate[r].end && head.size() < kPrefilterBytes; ++a)
                head.push_back(read_byte(a));
        if (mzm_nes_payload::sha256_hex(head.data(), head.size()) !=
            sc.prefilter_sha256)
            return false;
    }
    // Stage 2: the complete gate.
    std::vector<std::uint8_t> buffer;
    for (std::size_t r = 0; r < sc.gate_count; ++r) {
        for (std::uint32_t a = sc.gate[r].start; a < sc.gate[r].end; ++a) {
            buffer.push_back(read_byte(a));
        }
    }
    return mzm_nes_payload::sha256_hex(buffer.data(), buffer.size()) ==
           sc.gate_sha256;
}

// Entry gate: the scope owning `pc` and every scope in its requires closure must
// verify. Returns the index of the first failing scope, -1 if all verified, or
// -2 if `pc` belongs to no scope of the image.
template <typename ReadByte>
int verify_entry(const ImageSpec& spec, std::uint32_t pc, ReadByte read_byte) {
    const int own = scope_of(spec, pc);
    if (own < 0) return -2;
    // The entry scope first: it is the one a stale image must reject.
    if (!verify_scope(spec.scopes[own], read_byte)) return own;
    for (std::size_t i = 0; i < spec.scope_count; ++i)
        if (static_cast<int>(i) != own &&
            (spec.scopes[own].requires_mask >> i & 1u) &&
            !verify_scope(spec.scopes[i], read_byte))
            return static_cast<int>(i);
    return -1;
}

// Every scope of the image verifies (e.g. the freshly loaded image).
template <typename ReadByte>
bool verify_image(const ImageSpec& spec, ReadByte read_byte) {
    for (std::size_t i = 0; i < spec.scope_count; ++i)
        if (!verify_scope(spec.scopes[i], read_byte)) return false;
    return true;
}

// Verified-copy fast path. `snapshot` holds the gate bytes of a scope that was
// previously accepted by verify_scope() (the full SHA-256 gate). A later entry
// whose live bytes are identical to that snapshot is, by transitivity, the
// same ROM-derived scope; any difference falls back to the full gate. This is
// not a latch: every entry re-compares the live bytes.
inline bool matches_snapshot(const ImageSpec& spec, const ScopeSpec& sc,
                             const std::uint8_t* live_base,
                             const std::vector<std::uint8_t>& snapshot) {
    if (snapshot.empty()) return false;
    std::size_t off = 0;
    for (std::size_t r = 0; r < sc.gate_count; ++r) {
        const std::size_t n = sc.gate[r].end - sc.gate[r].start;
        if (off + n > snapshot.size() ||
            std::memcmp(live_base + (sc.gate[r].start - spec.start),
                        snapshot.data() + off, n) != 0)
            return false;
        off += n;
    }
    return off == snapshot.size();
}

}  // namespace mzm_nes_emulator
