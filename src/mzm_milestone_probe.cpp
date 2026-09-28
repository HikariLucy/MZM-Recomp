#include "mzm_milestone_probe.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "runtime.h"
#include "runtime_arm.h"

namespace {

bool g_enabled = false;
bool g_hook_installed = false;
bool g_intro_handler = false;
bool g_title_handler = false;
std::uint64_t g_intro_hits = 0;
std::uint64_t g_title_hits = 0;

constexpr std::uint32_t kIntroHandler = 0x0808117Cu;
constexpr std::uint32_t kTitleScreenHandler = 0x080771A0u;

void milestone_entry(std::uint32_t pc) {
    if (pc == kIntroHandler) {
        g_intro_handler = true;
        ++g_intro_hits;
    } else if (pc == kTitleScreenHandler) {
        g_title_handler = true;
        ++g_title_hits;
    }
}

std::uint16_t milestone_input_frame(const gbarecomp::TouchFrameInfo*) {
    // run_game() intentionally clears the generic entry hook during startup.
    // input_frame runs on the guest thread once per frame, so installing here
    // happens after runtime initialization while remaining before the later
    // intro/title flow. 0x03FF is the inactive active-low GBA keypad mask:
    // this callback synthesizes no input and therefore does not change play.
    if (!g_hook_installed) {
        g_runtime_fn_entry_hook = &milestone_entry;
        g_hook_installed = true;
    }
    return 0x03FFu;
}

bool env_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

}  // namespace

void mzm_configure_milestone_probe(gbarecomp::RunOptions& opts) {
    g_enabled = env_enabled("MZM_MILESTONE_TRACE");
    if (g_enabled) {
        opts.input_frame = &milestone_input_frame;
    }
}

void mzm_report_milestone_probe() {
    if (!g_enabled) {
        return;
    }

    std::printf(
        "mzm_milestones intro_handler=%s intro_hits=%llu "
        "title_handler=%s title_hits=%llu\n",
        g_intro_handler ? "YES" : "NO",
        static_cast<unsigned long long>(g_intro_hits),
        g_title_handler ? "YES" : "NO",
        static_cast<unsigned long long>(g_title_hits));
}
