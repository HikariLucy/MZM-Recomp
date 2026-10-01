#pragma once

// NES-PERF-1 profiling support. Compiled only when MZM_PERF_PROFILE is defined
// (CMake -DMZM_PERF_PROFILE=ON); otherwise every macro is a no-op and the
// production binaries are untouched. See docs/M4-NES-PERFORMANCE.md.
//
// Time attribution is *exclusive*: one global "current bucket"; every switch
// charges the TSC delta since the previous switch to the bucket being left.
// Nested activations (the generated code recurses through the RAM hook) are
// therefore never double counted.
#include <cstdint>

#ifdef MZM_PERF_PROFILE
namespace mzm_perf {

enum Bucket : int {
    B_HARNESS = 0,   // test harness / host loop outside runtime_dispatch
    B_NATIVE,        // generated guest code (incl. bus, runtime_tick, real private-table scan)
    B_RT,            // runtime_dispatch / tail / drain machinery, dispatch_once before the hook
    B_HOOK,          // mzm_ram_dispatch bookkeeping (classify, stats, resolver glue)
    B_VERIFY_SNAP,   // live-byte memcmp against the SHA-verified snapshot
    B_VERIFY_SHA,    // prefilter + full SHA-256 fallback
    B_SCAN,          // replica of the private-table lookup (measurement of its cost)
    B_PPU_TICK,      // GbaPpu::tick
    B_PPU_RENDER,    // GbaPpu::render_scanline (host scanline compositor)
    B_AUDIO,         // GbaAudio::tick
    B_TIMERS,        // GbaIo::tick_timers / tick_sio
    B_DMA,           // GbaIo::run_timed_dma
    B_COUNT
};
extern const char* const kBucketName[B_COUNT];

extern bool g_on;
extern Bucket g_cur;
extern std::uint64_t g_last;
extern std::uint64_t g_acc[B_COUNT];

inline std::uint64_t tsc() { return __builtin_ia32_rdtsc(); }
inline Bucket sw(Bucket b) {
    const Bucket prev = g_cur;
    if (!g_on) return prev;
    const std::uint64_t t = tsc();
    g_acc[prev] += t - g_last;
    g_last = t;
    g_cur = b;
    return prev;
}
struct Scope {
    Bucket prev;
    explicit Scope(Bucket b) : prev(sw(b)) {}
    ~Scope() { sw(prev); }
};

struct Counters {
    std::uint64_t dispatch = 0, dispatch_tail = 0, exch = 0, exch_tail = 0;
    std::uint64_t drain_calls = 0, drain_pending = 0;
    std::uint64_t ticks = 0;                       // guest instructions (runtime_tick calls)
    std::uint64_t hook_calls = 0, invoke_in_image = 0, invoke_ok = 0;
    std::uint64_t snapshot_hits = 0, sha_fallbacks = 0, sha_fail = 0;
    std::uint64_t scan_rows = 0, scan_calls = 0;   // rows visited by the private-table lookup
    std::uint64_t part_dispatch[7] = {}, part_tail[7] = {};   // by target Part (index 6 = other)
    std::uint64_t ppu_ticks = 0, render_calls = 0, audio_ticks = 0, timer_ticks = 0, dma_runs = 0;
};
extern Counters C;

void init();                          // reads MZM_PERF*, installs fn-entry hook if requested
void report(std::uint64_t frames);    // PERF lines on stderr; pair/fn dumps to files
void fn_entry(std::uint32_t pc);      // per-native-function activation histogram (MZM_PERF_FN)
void record_pair(const void* ret, std::uint32_t target, int kind);
std::uint32_t scan_private(int image, std::uint32_t pc, int thumb);   // replica, returns index+1

}  // namespace mzm_perf
#define MZM_PERF_SCOPE(b) ::mzm_perf::Scope _mzm_perf_scope(::mzm_perf::b)
#define MZM_PERF_SW(b) ::mzm_perf::sw(::mzm_perf::b)
#define MZM_PERF_COUNT(field) (++::mzm_perf::C.field)
#else
#define MZM_PERF_SCOPE(b) ((void)0)
#define MZM_PERF_SW(b) ((void)0)
#define MZM_PERF_COUNT(field) ((void)0)
#endif
