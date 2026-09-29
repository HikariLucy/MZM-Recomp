#include "mzm_ram_dispatch.h"
#include "mzm_haze_resolver.h"
#include "mzm_milestone_probe.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "runtime_arm.h"
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

int mzm_ram_dispatch(std::uint32_t pc, int thumb) {
    ++g_stats.hook_calls;
    if (!thumb) {
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
    g_stats.trace = env_enabled("MZM_TRACE_RAM_DISPATCH");
    g_stats.disable_haze = env_enabled("MZM_DISABLE_HAZE_RAM_DISPATCH");
    const char* capture = std::getenv("MZM_M4_CAPTURE_FIRST_HAZE");
    g_stats.capture_requested = capture && capture[0];
    g_runtime_ram_dispatch_hook = &mzm_ram_dispatch;
}

void mzm_report_ram_dispatch() {
    if (!g_stats.trace && !g_stats.capture_requested && !g_stats.disable_haze) {
        return;
    }
    std::fprintf(stderr,
                 "mzm_ram_dispatch_summary hook_calls=%llu haze_attempts=%llu "
                 "haze_matches=%llu",
                 static_cast<unsigned long long>(g_stats.hook_calls),
                 static_cast<unsigned long long>(g_stats.haze_attempts),
                 static_cast<unsigned long long>(g_stats.haze_matches));
    for (std::size_t i = 0; i < mzm_haze::kTemplates.size(); ++i) {
        std::fprintf(stderr, " %s=%llu", mzm_haze::kTemplates[i].name,
                     static_cast<unsigned long long>(g_stats.variant_hits[i]));
    }
    std::fprintf(stderr, "\n");
}
