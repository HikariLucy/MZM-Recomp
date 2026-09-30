// M4-RESUME-1: static resume entries of the InitializeGame region, through the
// real runtime API (runtime_has_static_entry) and the real dispatch table.
// No ROM, no game run. The Python test (tests/m4/test_resume.py) checks the
// complete instruction set against the generated bodies; this checks the runtime
// view: right mode succeeds, wrong mode / data / mid-instruction do not.
#include <cstdint>
#include <cstdio>

#include "runtime_arm.h"

namespace {
int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++g_failures;
}
}  // namespace

int main() {
    std::printf("== InitializeGame [0x080006A0,0x080007C4) static entries\n");
    check(runtime_has_static_entry(0x080006A0u, 1) == 1, "root 0x080006A0 Thumb is an entry");
    check(runtime_has_static_entry(0x080006A0u, 0) == 0, "root is not an ARM entry");
    // The two DMA3 starts (EWRAM / IWRAM clears) and the instruction after each.
    check(runtime_has_static_entry(0x080006C8u, 1) == 1, "0x080006C8 (str r1,[r0,#8]: DMA3 start) resumes");
    check(runtime_has_static_entry(0x080006CAu, 1) == 1, "0x080006CA (first post-NES frontier) resumes");
    check(runtime_has_static_entry(0x080006CAu, 0) == 0, "wrong mode: 0x080006CA is not an ARM entry");
    check(runtime_has_static_entry(0x080006DCu, 1) == 1, "0x080006DC (after the IWRAM DMA3 start) resumes");
    check(runtime_has_static_entry(0x080006DEu, 1) == 1 && runtime_has_static_entry(0x080006E0u, 1) == 1,
          "both halves of the Thumb BL at 0x080006DE resume (state lives in R14)");
    check(runtime_has_static_entry(0x080006F6u, 1) == 1, "interior of a continuation function (0x080006F4 root) resumes");
    check(runtime_has_static_entry(0x08000734u, 1) == 0 && runtime_has_static_entry(0x08000740u, 1) == 0 &&
              runtime_has_static_entry(0x08000748u, 1) == 0,
          "literal pool words are not published");
    check(runtime_has_static_entry(0x080007A4u, 1) == 0, "bytes after the last instruction are not published");
    check(runtime_has_static_entry(0x080007C4u, 1) == 1, "the next function (SoftResetVBlankCallback) is unchanged");
    std::printf("== InitializeAudio [0x08002564,0x080027F8) static entries (M4-RESUME-2)\n");
    check(runtime_has_static_entry(0x08002564u, 1) == 1, "root 0x08002564 Thumb is an entry");
    check(runtime_has_static_entry(0x0800271Cu, 1) == 1, "0x0800271C (post-NES boot frontier) resumes");
    check(runtime_has_static_entry(0x0800271Cu, 0) == 0, "wrong mode: 0x0800271C is not an ARM entry");
    check(runtime_has_static_entry(0x080026B8u, 1) == 1 && runtime_has_static_entry(0x080026BAu, 1) == 1,
          "both halves of its Thumb BL resume");
    check(runtime_has_static_entry(0x0800276Au, 1) == 1, "last instruction (bx r0) resumes");
    check(runtime_has_static_entry(0x0800276Cu, 1) == 0 && runtime_has_static_entry(0x08002770u, 1) == 0 &&
              runtime_has_static_entry(0x080027F4u, 1) == 0,
          "literal pool words are not published");
    check(runtime_has_static_entry(0x080027F8u, 1) == 1, "the next function (DoSoundAction) is unchanged");
    std::printf("M4-RESUME-1 entries %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
