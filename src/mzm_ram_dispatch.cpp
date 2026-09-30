#include "mzm_ram_dispatch.h"
#include "mzm_haze_resolver.h"
#include "mzm_chozodia_resolver.h"
#include "mzm_nes_payload_resolver.h"
#include "mzm_nes_emulator_resolver.h"
#include "mzm_milestone_probe.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "gba_bus.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "recompiled.h"

namespace {

struct RamTemplate {
    std::uint32_t source_start;
    std::uint32_t size;
    void (*native_fn)();
};

// MZM USA (BMXE rev 0), byte-perfect decomp anchors.
//
// SramWriteUnchecked() copies the body in
// [SramWriteUncheckedInternal, SramWriteUnchecked) into a local stack buffer.
// SramCheck() does the same for [SramCheckInternal, SramCheck).
constexpr RamTemplate kStackHelpers[] = {
    {0x080051D4u, 0x24u, &gf_SramWriteUncheckedInternal},
    {0x0800529Cu, 0x30u, &gf_SramCheckInternal},
};

// Index order matches mzm_haze::kTemplates. BG3/BG2/BG1 is copied but
// HazeProcess calls its ROM entry directly; retaining it here makes the
// resolver cover every observed byte image without changing that control flow.
constexpr void (*kHazeNative[])() = {
    &gf_Haze_Bg3,
    &gf_Haze_Bg3StrongWeak,
    &gf_Haze_Bg3NoneWeak,
    &gf_Haze_Bg3Bg2StrongWeakMedium,
    &gf_Haze_Bg3Bg2Bg1,
    &gf_Haze_PowerBombExpanding,
    &gf_Haze_PowerBombRetracting,
};
static_assert(std::size(kHazeNative) == mzm_haze::kTemplates.size());

struct DispatchStats {
    std::uint64_t hook_calls = 0;
    std::uint64_t haze_attempts = 0;
    std::uint64_t haze_matches = 0;
    std::uint64_t chozodia_attempts = 0;
    std::uint64_t chozodia_matches = 0;
    std::uint64_t nes_attempts = 0;
    std::uint64_t nes_matches = 0;
    std::uint64_t nes_frontier_hits = 0;
    mzm_nes_emulator_part_stats_t nes_part[mzm_nes_emulator::kNumParts] = {};
    mzm_nes_emulator_scope_stats_t nes_scope[mzm_nes_emulator::kNumParts]
                                            [mzm_nes_emulator::kMaxScopes] = {};
    std::uint64_t variant_hits[mzm_haze::kTemplates.size()] = {};
    bool trace = false;
    bool capture_requested = false;
    bool disable_haze = false;
};
DispatchStats g_stats;

bool env_enabled(const char* key) {
    const char* value = std::getenv(key);
    return value && value[0] == '1' && value[1] == '\0';
}

bool guest_bytes_match(std::uint32_t runtime_pc,
                       std::uint32_t source_pc,
                       std::uint32_t size) {
    for (std::uint32_t i = 0; i < size; ++i) {
        if (bus_read_u8(runtime_pc + i) != bus_read_u8(source_pc + i)) {
            return false;
        }
    }
    return true;
}


static bool s_nes_payload_verified = false;
static constexpr std::size_t kMaxNesTransitions = 64;
static mzm_nes_emulator_transition_t s_nes_transitions[kMaxNesTransitions];
static std::size_t s_nes_transition_count = 0;
static int s_nes_last_part = -1;
static void (*s_nes_frontier_hook)(const mzm_nes_emulator_event_t&) = nullptr;
static bool s_nes_payload_frontier_stop = false;

// Entry gate for one NES emulator PC: the owning scope and its requires
// closure must match the ROM-derived bytes. Returns the first failing scope,
// -1 if all verified, -2 if `pc` belongs to no scope.
// Deliberately out of line: the generated Part 2 code recurses natively through
// this hook thousands of frames deep, so this frame's locals (byte snapshots,
// lambda) must not stay live across the native call.
__attribute__((noinline)) static int nes_verify_entry(
    mzm_nes_emulator::ImageKind emu_kind, const mzm_nes_emulator::ImageSpec& spec,
    std::uint32_t pc, int own_scope) {
    (void)pc;
    if (own_scope < 0) return -2;
    // Fast path: live bytes identical to a previously SHA-verified copy.
    static std::vector<std::uint8_t> snapshots[mzm_nes_emulator::kNumParts]
                                              [mzm_nes_emulator::kMaxScopes];
    const std::uint8_t* live = nullptr;
    if (auto* bus = gbarecomp::active_bus()) {
        switch (spec.start >> 24) {
            case 0x02: live = bus->ewram_ptr() + (spec.start & 0x3FFFFu); break;
            case 0x03: live = bus->iwram_ptr() + (spec.start & 0x7FFFu); break;
            case 0x06: live = bus->vram_ptr() + (spec.start - 0x06000000u); break;
            default: break;
        }
    }
    auto scope_ok = [&](std::size_t i) {
        const auto& sc = spec.scopes[i];
        auto& snapshot = snapshots[static_cast<std::size_t>(emu_kind)][i];
        if (live && mzm_nes_emulator::matches_snapshot(spec, sc, live, snapshot))
            return true;
        snapshot.clear();
        if (!mzm_nes_emulator::verify_scope(
                sc, [](std::uint32_t a) { return bus_read_u8(a); }))
            return false;
        for (std::size_t r = 0; r < sc.gate_count; ++r)
            for (std::uint32_t a = sc.gate[r].start; a < sc.gate[r].end; ++a)
                snapshot.push_back(bus_read_u8(a));
        return true;
    };
    // The entry scope first: it is the one a stale image must reject.
    if (!scope_ok(static_cast<std::size_t>(own_scope))) return own_scope;
    for (std::size_t i = 0; i < spec.scope_count; ++i)
        if (static_cast<int>(i) != own_scope &&
            (spec.scopes[own_scope].requires_mask >> i & 1u) && !scope_ok(i))
            return static_cast<int>(i);
    return -1;
}

int mzm_ram_dispatch(std::uint32_t pc, int thumb) {
    ++g_stats.hook_calls;

    if (!thumb && s_nes_payload_frontier_stop && pc == 0x06006558u) {
        ++g_stats.nes_frontier_hits;
        if (g_stats.trace) {
            std::fprintf(stderr,
                         "mzm_ram_dispatch kind=nes_frontier "
                         "runtime_pc=0x%08x match=1 native=0 hits=%llu\n", pc,
                         static_cast<unsigned long long>(g_stats.nes_frontier_hits));
            std::fflush(stderr);
        }
        return 1; // Controlled expected post-payload frontier for NES-1b
    }

    // NES emulator Parts 1..6. A PC inside a Part's range runs natively only if
    // the live bytes match the ROM-derived gate digest. Anything else falls
    // through to the remaining resolvers (Part 2 and Part 6 ranges alias
    // MZM's own IWRAM/EWRAM code).
    const auto emu_kind =
        mzm_nes_emulator::classify_pc(pc, s_nes_frontier_hook != nullptr);
    if (emu_kind != mzm_nes_emulator::ImageKind::None) {
        const auto& spec = mzm_nes_emulator::spec_of(emu_kind);
        auto& st = g_stats.nes_part[static_cast<std::size_t>(emu_kind)];
        ++st.attempts;
        // The entry PC selects its verification scope; the scope and the scopes
        // it requires (direct transfers generated code follows without this
        // resolver) must all match the ROM-derived bytes.
        const int own_scope = mzm_nes_emulator::scope_of(spec, pc);
        auto* ss = own_scope >= 0
                       ? &g_stats.nes_scope[static_cast<std::size_t>(emu_kind)]
                                           [static_cast<std::size_t>(own_scope)]
                       : nullptr;
        if (ss) ++ss->attempts;
        const int failed_scope = nes_verify_entry(emu_kind, spec, pc, own_scope);
        const bool verified = failed_scope == -1;
        if (ss) {
            if (verified) ++ss->verified;
            else if (failed_scope == own_scope) ++ss->verify_failures;
            else ++ss->dependency_failures;
        }
        if (!verified) {
            ++st.verify_failures;
            if (g_stats.trace) {
                std::fprintf(stderr,
                             "mzm_ram_dispatch kind=nes_emulator part=%s "
                             "runtime_pc=0x%08x mode=%s scope=%s verify=FAIL failed_scope=%s\n",
                             spec.name, pc, thumb ? "thumb" : "arm",
                             own_scope >= 0 ? spec.scopes[own_scope].id : "none",
                             failed_scope >= 0 ? spec.scopes[failed_scope].id : "none");
            }
        } else {
            ++st.verified;
            if (s_nes_last_part != static_cast<int>(emu_kind)) {
                if (s_nes_transition_count < kMaxNesTransitions) {
                    s_nes_transitions[s_nes_transition_count++] = {
                        pc, thumb, s_nes_last_part < 0 ? nullptr
                            : mzm_nes_emulator::kParts[s_nes_last_part].name,
                        spec.name};
                }
                s_nes_last_part = static_cast<int>(emu_kind);
            }
            const char* reason = nullptr;
            if (!spec.has_corpus) {
                ++st.no_corpus;
                reason = "part_has_no_corpus";
            } else {
                // Counted at entry, not on return: the native body may unwind
                // through a frontier observer with this dispatch still in flight.
                ++st.matches;
                if (ss) ++ss->matches;
                if (g_stats.trace) {
                    std::fprintf(stderr,
                                 "mzm_ram_dispatch kind=nes_emulator part=%s "
                                 "runtime_pc=0x%08x mode=%s match=1 native=1 hits=%llu\n",
                                 spec.name, pc, thumb ? "thumb" : "arm",
                                 static_cast<unsigned long long>(st.matches));
                    std::fflush(stderr);
                }
                // Explicit image identity: the verified Part selects the body.
                // The unscoped API would reach primary-world entries only.
                static int handles[mzm_nes_emulator::kNumParts];
                static bool handles_ready = false;
                if (!handles_ready) {
                    for (std::size_t i = 0; i < mzm_nes_emulator::kNumParts; ++i) {
                        char id[16];
                        std::snprintf(id, sizeof(id), "nes_%s",
                                      mzm_nes_emulator::kParts[i].name);
                        handles[i] = runtime_private_image_handle(id);
                    }
                    handles_ready = true;
                }
                const int handle = handles[static_cast<std::size_t>(emu_kind)];
                if (handle > 0 &&
                    runtime_invoke_private_entry_in_image(handle, pc, thumb)) {
                    return 1;
                }
                --st.matches;
                if (ss) --ss->matches;
                ++st.invoke_failures;
                reason = "no_private_entry";
            }
            if (g_stats.trace) {
                std::fprintf(stderr,
                             "mzm_ram_dispatch kind=nes_emulator part=%s "
                             "runtime_pc=0x%08x mode=%s verified=1 miss=%s\n",
                             spec.name, pc, thumb ? "thumb" : "arm", reason);
            }
            if (s_nes_frontier_hook) {
                // Opt-in observer (tests): may unwind by throwing; the guest
                // CPU state is exactly the state at the unresolved transfer.
                s_nes_frontier_hook({pc, thumb, spec.name, reason});
            }
            return 0;
        }
    }

    if (!thumb) {
        if (pc >= mzm_nes_payload::kRuntimeStart &&
            pc < mzm_nes_payload::kRuntimeStart + mzm_nes_payload::kPayloadSize) {
            ++g_stats.nes_attempts;
            bool identified = false;
            if (pc == mzm_nes_payload::kRuntimeStart) {
                identified = mzm_nes_payload::identify(pc, false, [](std::uint32_t addr) {
                    return bus_read_u8(addr);
                });
                s_nes_payload_verified = identified;
            } else {
                identified = s_nes_payload_verified;
            }
            if (!identified) {
                return 0;
            }
            const int invoked = runtime_invoke_private_entry(pc, thumb);
            if (!invoked) {
                return 0;
            }
            if (++g_stats.nes_matches == 1 && g_stats.trace) {
                std::fprintf(stderr,
                             "mzm_ram_dispatch kind=nes_payload "
                             "runtime_pc=0x%08x match=1 native=1 hits=1\n", pc);
                std::fflush(stderr);
            }
            return 1;
        }
        return 0;
    }

    if (pc == mzm_haze::kRuntimeStart) {
        ++g_stats.haze_attempts;
        const int variant = mzm_haze::identify(pc, true, [](std::uint32_t addr) {
            return bus_read_u8(addr);
        });
        if (variant < 0) {
            return 0;
        }
        ++g_stats.haze_matches;
        const bool first_variant_hit = ++g_stats.variant_hits[variant] == 1;
        if ((g_stats.trace && first_variant_hit) ||
            (g_stats.capture_requested && g_stats.haze_matches == 1)) {
            std::fprintf(stderr,
                         "mzm_ram_dispatch kind=haze variant=%s "
                         "runtime_pc=0x%08x source_pc=0x%08x match=1 native=%d hits=%llu\n",
                         mzm_haze::kTemplates[variant].name, pc,
                         mzm_haze::kTemplates[variant].source_start,
                         g_stats.disable_haze ? 0 : 1,
                         static_cast<unsigned long long>(g_stats.variant_hits[variant]));
            std::fflush(stderr);
        }
        if (g_stats.disable_haze) {
            return 0;
        }
        kHazeNative[variant]();
        return 1;
    }

    if (pc >= mzm_chozodia::kRuntimeStart &&
        pc < mzm_chozodia::kRuntimeStart + mzm_chozodia::kFunctionSize) {
        ++g_stats.chozodia_attempts;
        if (mzm_chozodia::identify(pc, true, [](std::uint32_t addr) {
                return bus_read_u8(addr);
            })) {
            const int invoked = runtime_invoke_private_entry(pc, thumb);
            if (!invoked) return 0; // data or un-emitted instruction PC
            if (++g_stats.chozodia_matches == 1 && g_stats.trace) {
                std::fprintf(stderr,
                             "mzm_ram_dispatch kind=chozodia_hblank "
                             "runtime_pc=0x%08x source_pc=0x%08x match=1 native=1 hits=1\n",
                             pc, mzm_chozodia::kSourceStart);
                std::fflush(stderr);
            }
            return 1;
        }
        return 0;
    }

    // MZM's normal System-mode stack begins at 0x03007E60. Restrict this
    // canonicalizer to the high-IWRAM stack/scratch window so fixed IWRAM code
    // copies continue through the normal generated dispatch table.
    if (pc < 0x03007000u || pc >= 0x03007E60u) {
        return 0;
    }

    for (const RamTemplate& helper : kStackHelpers) {
        if (!guest_bytes_match(pc, helper.source_start, helper.size)) {
            continue;
        }

        // SRAM initialization occurs before the normal Intro/Title loop. If
        // milestone tracing is enabled, this is an early post-run_game-reset
        // point where the generic function-entry hook can be armed reliably.
        mzm_arm_milestone_entry_hook();

        // The helper bodies are position-independent Thumb code. The runtime
        // bytes have been verified against the exact ROM source before we
        // canonicalize execution to the already generated native translation.
        helper.native_fn();
        return 1;
    }

    return 0;
}

}  // namespace

void mzm_install_ram_dispatch_hook() {
    g_stats = {};
    s_nes_payload_verified = false;
    s_nes_frontier_hook = nullptr;
    s_nes_transition_count = 0;
    s_nes_last_part = -1;
    g_stats.trace = env_enabled("MZM_TRACE_RAM_DISPATCH");
    g_stats.disable_haze = env_enabled("MZM_DISABLE_HAZE_RAM_DISPATCH");
    const char* capture = std::getenv("MZM_M4_CAPTURE_FIRST_HAZE");
    g_stats.capture_requested = capture && capture[0];
    g_runtime_ram_dispatch_hook = &mzm_ram_dispatch;
}

void mzm_chozodia_dispatch_counts(std::uint64_t& attempts,
                                  std::uint64_t& matches) {
    attempts = g_stats.chozodia_attempts;
    matches = g_stats.chozodia_matches;
}

void mzm_nes_payload_dispatch_counts(std::uint64_t& attempts,
                                     std::uint64_t& matches) {
    attempts = g_stats.nes_attempts;
    matches = g_stats.nes_matches;
}

std::uint64_t mzm_nes_payload_frontier_hits() {
    return g_stats.nes_frontier_hits;
}

void mzm_set_nes_payload_frontier_stop(bool stop) {
    s_nes_payload_frontier_stop = stop;
}

void mzm_set_nes_emulator_frontier_hook(
    void (*hook)(const mzm_nes_emulator_event_t&)) {
    s_nes_frontier_hook = hook;
}

std::size_t mzm_nes_emulator_transitions(mzm_nes_emulator_transition_t* out,
                                         std::size_t capacity) {
    const std::size_t n = capacity < s_nes_transition_count ? capacity : s_nes_transition_count;
    for (std::size_t i = 0; i < n; ++i) out[i] = s_nes_transitions[i];
    return n;
}

std::size_t mzm_nes_emulator_part_stats(mzm_nes_emulator_part_stats_t* out,
                                        std::size_t capacity) {
    const std::size_t n =
        capacity < mzm_nes_emulator::kNumParts ? capacity : mzm_nes_emulator::kNumParts;
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = g_stats.nes_part[i];
        out[i].name = mzm_nes_emulator::kParts[i].name;
    }
    return n;
}

std::size_t mzm_nes_emulator_scope_stats(mzm_nes_emulator_scope_stats_t* out,
                                         std::size_t capacity) {
    std::size_t n = 0;
    for (std::size_t p = 0; p < mzm_nes_emulator::kNumParts; ++p) {
        const auto& spec = mzm_nes_emulator::kParts[p];
        for (std::size_t i = 0; i < spec.scope_count && n < capacity; ++i, ++n) {
            out[n] = g_stats.nes_scope[p][i];
            out[n].part = spec.name;
            out[n].scope = spec.scopes[i].id;
        }
    }
    return n;
}

void mzm_report_ram_dispatch() {
    if (!g_stats.trace && !g_stats.capture_requested && !g_stats.disable_haze) {
        return;
    }
    std::fprintf(stderr,
                 "mzm_ram_dispatch_summary hook_calls=%llu haze_attempts=%llu "
                 "haze_matches=%llu chozodia_attempts=%llu chozodia_matches=%llu "
                 "nes_attempts=%llu nes_matches=%llu",
                 static_cast<unsigned long long>(g_stats.hook_calls),
                 static_cast<unsigned long long>(g_stats.haze_attempts),
                 static_cast<unsigned long long>(g_stats.haze_matches),
                 static_cast<unsigned long long>(g_stats.chozodia_attempts),
                 static_cast<unsigned long long>(g_stats.chozodia_matches),
                 static_cast<unsigned long long>(g_stats.nes_attempts),
                 static_cast<unsigned long long>(g_stats.nes_matches));
    for (const auto& st : g_stats.nes_part) {
        std::fprintf(stderr, " nes_%s=attempts:%llu,verified:%llu,matches:%llu,"
                     "verify_failures:%llu,invoke_failures:%llu", st.name ? st.name : "?",
                     static_cast<unsigned long long>(st.attempts),
                     static_cast<unsigned long long>(st.verified),
                     static_cast<unsigned long long>(st.matches),
                     static_cast<unsigned long long>(st.verify_failures),
                     static_cast<unsigned long long>(st.invoke_failures));
    }
    for (std::size_t i = 0; i < mzm_haze::kTemplates.size(); ++i) {
        std::fprintf(stderr, " %s=%llu", mzm_haze::kTemplates[i].name,
                     static_cast<unsigned long long>(g_stats.variant_hits[i]));
    }
    std::fprintf(stderr, "\n");
}
