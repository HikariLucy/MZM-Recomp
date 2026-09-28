#include "mzm_ram_dispatch.h"
#include "mzm_milestone_probe.h"

#include <cstddef>
#include <cstdint>

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
    if (!thumb) {
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
    g_runtime_ram_dispatch_hook = &mzm_ram_dispatch;
}
