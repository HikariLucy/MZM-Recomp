// HOST-STACK-1 synthetic reproduction (no Nintendo bytes).
//
// Shape of the generated code for an indirect tail transfer (`ldr pc,[...]`,
// `mov pc,lr` that is not a recorded return) inside a private/RAM image:
//     runtime_dispatch(target); return;
// and of the RAM hook (mzm_ram_dispatch): it resolves `target` and invokes the
// native body directly. Nothing returns until the whole chain ends, so the host
// stack grows with the number of guest transfers even though the guest program
// is a flat loop. `runtime_should_yield` (VBlank) is what currently bounds it.
//
//   host_stack_dispatch_test                      tail shape (current codegen), exit 0
//   host_stack_dispatch_test --bounded            assert the high-water is bounded
//   host_stack_dispatch_test --legacy-recursive   old nested shape (linear growth)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "runtime_arm.h"

namespace {
constexpr std::uint32_t kPcStep = 0x03001000u;   // IWRAM, like NES Part 2
constexpr std::uint32_t kPcDone = 0x03001100u;
std::uint64_t g_counter, g_limit;
bool g_legacy = false;
std::uintptr_t g_low;

void note_stack() {
    const auto here = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    if (here < g_low) g_low = here;
}
// Generated-shaped body: do work, then an indirect tail transfer (dispatch; return).
void step_body() {
    note_stack();
    ++g_counter;
    g_cpu.R[0] = static_cast<std::uint32_t>(g_counter);
    const std::uint32_t next = g_counter < g_limit ? kPcStep : kPcDone;
    if (g_legacy) runtime_dispatch(next);
    else          runtime_dispatch_tail(next);
    return;
}
void done_body() { note_stack(); }

// Same contract as mzm_ram_dispatch: decide, invoke natively, return 1.
int hook(std::uint32_t pc, int) {
    if (pc == kPcStep) { step_body(); return 1; }
    if (pc == kPcDone) { done_body(); return 1; }
    return 0;
}

std::uintptr_t run(std::uint64_t n) {
    g_counter = 0; g_limit = n;
    g_low = ~static_cast<std::uintptr_t>(0);
    g_cpu.cpsr = 0x1Fu;   // ARM, system mode
    const auto base = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    runtime_dispatch(kPcStep);
    return base - g_low;
}
}  // namespace

int main(int argc, char** argv) {
    const bool bounded = argc > 1 && std::strcmp(argv[1], "--bounded") == 0;
    g_legacy = argc > 1 && std::strcmp(argv[1], "--legacy-recursive") == 0;
    g_runtime_ram_dispatch_hook = hook;
    int failures = 0;
    std::uintptr_t prev = 0;
    for (std::uint64_t n : {1000ull, 10000ull, 30000ull}) {
        const std::uintptr_t hw = run(n);
        std::printf("depth=%llu guest_steps=%llu high_water=%llu bytes (%.1f bytes/step)\n",
                    (unsigned long long)n, (unsigned long long)g_counter,
                    (unsigned long long)hw, double(hw) / double(n));
        if (g_counter != n) { std::printf("FAIL guest semantics\n"); ++failures; }
        prev = hw;
    }
    if (bounded && prev > 256u * 1024u) {
        std::printf("FAIL host stack not bounded: %llu bytes for 30000 tail transfers\n",
                    (unsigned long long)prev);
        ++failures;
    }
    std::printf("host-stack dispatch %s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
