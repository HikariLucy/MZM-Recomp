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
    std::printf("== BitFill [0x080032B4,0x08003380) static entries (M4-RESUME-2)\n");
    check(runtime_has_static_entry(0x080032B4u, 1) == 1, "root 0x080032B4 Thumb is an entry");
    check(runtime_has_static_entry(0x0800331Au, 1) == 1, "0x0800331A (ldr after the DMA start, frontier) resumes");
    check(runtime_has_static_entry(0x0800331Au, 0) == 0, "wrong mode: 0x0800331A is not an ARM entry");
    check(runtime_has_static_entry(0x080032E2u, 1) == 0 && runtime_has_static_entry(0x080032E4u, 1) == 0 &&
              runtime_has_static_entry(0x08003328u, 1) == 0 && runtime_has_static_entry(0x0800333Cu, 1) == 0,
          "the literal pools inside BitFill are not published");
    check(runtime_has_static_entry(0x08003380u, 1) == 1, "the next function (DMA2IntrCode) is unchanged");
    // Frontier rows added one reviewed unit at a time (M4-RESUME-2): the unit root
    // stays an entry, the PC where a VBlank yield first landed now resumes, in the
    // Thumb mode only.
    struct Row { const char* unit; std::uint32_t root, frontier; };
    static const Row rows[] = {
        {"RoomSetInitialTilemap", 0x08056B28u, 0x08056C50u},
        {"RoomRleDecompress", 0x08056D18u, 0x08056D90u},
        {"InitAndLoadGenerics", 0x0800CBACu, 0x0800CC68u},
    };
    for (const Row& r : rows) {
        std::printf("== %s frontier 0x%08X\n", r.unit, r.frontier);
        char msg[96];
        std::snprintf(msg, sizeof msg, "%s root 0x%08X is still an entry", r.unit, r.root);
        check(runtime_has_static_entry(r.root, 1) == 1, msg);
        std::snprintf(msg, sizeof msg, "%s 0x%08X resumes (Thumb)", r.unit, r.frontier);
        check(runtime_has_static_entry(r.frontier, 1) == 1, msg);
        std::snprintf(msg, sizeof msg, "%s 0x%08X is not an ARM entry", r.unit, r.frontier);
        check(runtime_has_static_entry(r.frontier, 0) == 0, msg);
    }
    std::printf("M4-RESUME-1 entries %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
