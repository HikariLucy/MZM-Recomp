#include "mzm_chozodia_resolver.h"
#include "mzm_ram_dispatch.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "gba_bus.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"

namespace {
std::uint32_t entered_root = 0;
void on_entry(std::uint32_t pc) {
    if (pc == mzm_chozodia::kRuntimeStart) ++entered_root;
}
void check(bool condition, const char* why) {
    if (!condition) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}

void expect_strict_miss(gba::GbaBus& bus, std::uint32_t pc) {
    const pid_t child = fork();
    check(child >= 0, "fork failed");
    if (child == 0) {
        const rlimit no_core{0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        setenv("GBARECOMP_STRICT_STATIC", "1", 1);
        bus.write8(mzm_chozodia::kRuntimeStart + 0x3Fu,
                   bus.read8(mzm_chozodia::kRuntimeStart + 0x3Fu) ^ 1u);
        g_cpu = {};
        g_cpu.cpsr = 0x3Fu;
        g_cpu.R[15] = pc;
        g_runtime_fn_entry_hook = [](std::uint32_t entered) {
            if (entered == mzm_chozodia::kRuntimeStart) _exit(77);
        };
        check(g_runtime_ram_dispatch_hook(pc, 1) == 0,
              "wrong image was accepted by MZM hook");
        std::uint64_t attempts = 0, matches = 0;
        mzm_chozodia_dispatch_counts(attempts, matches);
        check(attempts == 1u && matches == 0u,
              "wrong image gate counters");
        runtime_dispatch(pc | 1u);
        _exit(78);
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child, "waitpid failed");
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
          "wrong image entered native or escaped strict miss");
}
}

int main(int argc, char** argv) {
    check(argc == 2, "local ROM path required");
    setenv("GBARECOMP_MMIO_CAP", "1", 1);
    std::ifstream stream(argv[1], std::ios::binary);
    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(stream)), {});
    check(rom.size() == 0x800000u, "expected 8 MiB USA ROM");
    gba::GbaBus bus;
    gba::GbaPpu ppu;
    bus.set_rom(rom.data(), rom.size());
    gbarecomp::set_active_bus(&bus);
    gbarecomp::set_active_ppu(&ppu);
    mzm_install_ram_dispatch_hook();
    for (std::uint32_t i = 0; i < mzm_chozodia::kCopySize; ++i)
        bus.write8(mzm_chozodia::kRuntimeStart + i,
                   rom[mzm_chozodia::kSourceStart - 0x08000000u + i]);

    check(!runtime_has_static_entry(0x03001730u, 1), "private root public");
    check(!runtime_has_static_entry(0x03001752u, 1), "private resume public");
    check(runtime_has_static_entry(0x08087938u, 1), "public ROM root absent");
    expect_strict_miss(bus, 0x03001730u);
    expect_strict_miss(bus, 0x03001752u);
    check(g_runtime_ram_dispatch_hook(0x0300175Au, 1) == 0,
          "literal data was accepted as a private resume PC");

    // Execute the real generated private entry to its guest return.
    g_cpu = {};
    g_cpu.cpsr = 0xBFu;
    g_cpu.R[13] = 0x03007F00u;
    g_cpu.R[14] = 0x08000001u;
    g_cpu.R[15] = 0x03001730u;
    g_runtime_fn_entry_hook = on_entry;
    runtime_call_push_return(0x08000000u);
    runtime_dispatch(0x03001731u);
    check(entered_root == 1u, "private root did not execute");
    check(g_cpu.R[15] == 0x08000000u, "private root return PC");
    check(runtime_call_stack_depth() == 0u, "private root return stack");

    // Resume directly at the real WIN0H instruction through the same hook.
    g_cpu = {};
    g_cpu.cpsr = 0xBFu;
    g_cpu.R[0] = 0x1234u;
    g_cpu.R[4] = 0x04000040u;
    g_cpu.R[13] = 0x03007EF8u;
    g_cpu.R[15] = 0x03001752u;
    bus.write32(0x03007EF8u, 0u);
    bus.write32(0x03007EFCu, 0x08000001u);
    check(bus.read32(0x03007EFCu) == 0x08000001u, "resume stack setup");
    runtime_call_push_return(0x08000000u);
    runtime_dispatch(0x03001753u);
    check(bus.read16(0x04000040u) == 0x1234u, "WIN0H write missing");
    check(g_cpu.R[15] == 0x08000000u, "resume return PC");
    check(runtime_call_stack_depth() == 0u, "resume call stack");
    gba::MmioCapEntry captures[32]{};
    std::uint64_t first = 0;
    const auto count = gba::gba_mmio_cap_query(0, 32, captures, first);
    bool found = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (captures[i].addr == 0x04000040u) {
            check(captures[i].pc == 0x03001752u, "WIN0H leaked ROM PC");
            found = true;
        }
    }
    check(found, "WIN0H capture missing");
    std::puts("MZM Chozodia private entry, wrong image, resume, WIN0H PC: PASS");
    gbarecomp::set_active_ppu(nullptr);
    gbarecomp::set_active_bus(nullptr);
}
