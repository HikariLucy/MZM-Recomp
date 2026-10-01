#ifdef MZM_PERF_PROFILE
#include "mzm_perf_profile.h"
#include "mzm_nes_emulator_resolver.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "gba_audio.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "runtime_arm.h"

// Mirror of the generated private dispatch table (generated/dispatch_table.cpp).
struct PerfPrivateEntry { std::uint32_t addr; std::uint8_t thumb; std::uint8_t resume; std::uint16_t image; void (*fn)(void); };
extern "C" const PerfPrivateEntry kPrivateDispatchTable[];
extern "C" const unsigned kPrivateDispatchTableLen;
extern "C" bool g_runtime_tail_pending;

namespace mzm_perf {

const char* const kBucketName[B_COUNT] = {
    "harness", "generated_native", "runtime_dispatch", "hook_bookkeeping", "verify_memcmp",
    "verify_sha", "private_scan_replica", "ppu_tick", "ppu_render_scanline", "audio_tick",
    "timers_sio", "timed_dma"};

bool g_on = false;
Bucket g_cur = B_HARNESS;
std::uint64_t g_last = 0;
std::uint64_t g_acc[B_COUNT] = {};
Counters C;

namespace {
struct PairSlot { std::uint64_t ret = 0; std::uint32_t target = 0; std::uint32_t kind = 0; std::uint64_t n = 0; };
constexpr std::size_t kPairSlots = 1u << 16;
PairSlot* g_pairs = nullptr;
struct FnSlot { std::uint32_t pc = 0; std::uint64_t n = 0; };
constexpr std::size_t kFnSlots = 1u << 16;
FnSlot* g_fns = nullptr;
std::uint64_t g_fn_total = 0;
volatile std::uint32_t g_sink = 0;
double g_t0 = 0;
std::uint64_t g_tsc0 = 0;
const char* g_pairs_path = nullptr;
const char* g_fn_path = nullptr;

double now_s() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}
}  // namespace
void fn_entry(std::uint32_t pc) {
    if (!g_fns) return;
    ++g_fn_total;
    std::size_t i = (pc * 2654435761u) >> 16 & (kFnSlots - 1);
    while (g_fns[i].n && g_fns[i].pc != pc) i = (i + 1) & (kFnSlots - 1);
    g_fns[i].pc = pc; ++g_fns[i].n;
}
namespace {
int part_index(std::uint32_t pc) {
    const auto k = mzm_nes_emulator::classify_pc(pc, false);
    return k == mzm_nes_emulator::ImageKind::None ? 6 : static_cast<int>(k);
}
}  // namespace

void init() {
    const char* e = std::getenv("MZM_PERF");
    g_on = e && e[0] == '1';
    if (!g_on) return;
    g_pairs_path = std::getenv("MZM_PERF_PAIRS");
    g_fn_path = std::getenv("MZM_PERF_FN");
    g_pairs = static_cast<PairSlot*>(std::calloc(kPairSlots, sizeof(PairSlot)));
    if (g_fn_path) {
        g_fns = static_cast<FnSlot*>(std::calloc(kFnSlots, sizeof(FnSlot)));
        g_runtime_fn_entry_hook = &fn_entry;   // the harness chains to fn_entry() from its own hook
    }
    g_t0 = now_s();
    g_tsc0 = g_last = tsc();
    g_cur = B_HARNESS;
}

void record_pair(const void* ret, std::uint32_t target, int kind) {
    if (!g_pairs) return;
    const std::uint64_t r = reinterpret_cast<std::uintptr_t>(ret);
    std::size_t i = ((r * 11400714819323198485ull) ^ (target * 2654435761ull) ^ kind) >> 20 & (kPairSlots - 1);
    for (;;) {
        auto& s = g_pairs[i];
        if (!s.n) { s.ret = r; s.target = target; s.kind = kind; s.n = 1; return; }
        if (s.ret == r && s.target == target && s.kind == static_cast<std::uint32_t>(kind)) { ++s.n; return; }
        i = (i + 1) & (kPairSlots - 1);
    }
}

std::uint32_t scan_private(int image, std::uint32_t pc, int thumb) {
    static const bool off = [] { const char* e = std::getenv("MZM_PERF_SCAN"); return e && e[0] == '0'; }();
    if (off) return 0;
    ++C.scan_calls;
    std::uint32_t found = 0;
    for (unsigned i = 0; i < kPrivateDispatchTableLen; ++i) {
        const auto& e = kPrivateDispatchTable[i];
        if (e.image != static_cast<unsigned>(image) || e.addr != (pc & ~1u) ||
            e.thumb != (thumb != 0)) continue;
        found = i + 1;
        break;
    }
    C.scan_rows += found ? found : kPrivateDispatchTableLen;
    g_sink = found;
    return found;
}

void report(std::uint64_t frames) {
    if (!g_on) return;
    sw(B_HARNESS);
    const double wall = now_s() - g_t0;
    const std::uint64_t tsc_total = tsc() - g_tsc0;
    const double hz = static_cast<double>(tsc_total) / wall;   // TSC ticks per second
    const double fr = frames ? static_cast<double>(frames) : 1.0;
    auto ms = [&](std::uint64_t t) { return static_cast<double>(t) / hz * 1e3; };
    std::fprintf(stderr, "PERF frames=%llu wall_s=%.3f tsc_ghz=%.3f wall_ms_per_frame=%.3f\n",
                 (unsigned long long)frames, wall, hz / 1e9, wall * 1e3 / fr);
    std::uint64_t acc_sum = 0;
    for (int b = 0; b < B_COUNT; ++b) acc_sum += g_acc[b];
    for (int b = 0; b < B_COUNT; ++b)
        std::fprintf(stderr, "PERF time bucket=%s ms_total=%.1f ms_per_frame=%.4f pct=%.2f\n",
                     kBucketName[b], ms(g_acc[b]), ms(g_acc[b]) / fr,
                     acc_sum ? 100.0 * static_cast<double>(g_acc[b]) / static_cast<double>(acc_sum) : 0.0);
    auto pf = [&](const char* k, std::uint64_t v) {
        std::fprintf(stderr, "PERF count %s total=%llu per_frame=%.1f\n", k, (unsigned long long)v,
                     static_cast<double>(v) / fr);
    };
    pf("runtime_dispatch", C.dispatch);
    pf("runtime_dispatch_tail", C.dispatch_tail);
    pf("runtime_dispatch_with_exchange", C.exch);
    pf("runtime_dispatch_with_exchange_tail", C.exch_tail);
    pf("tail_drain_calls", C.drain_calls);
    pf("tail_drain_with_pending", C.drain_pending);
    pf("guest_instructions(runtime_tick)", C.ticks);
    pf("mzm_ram_dispatch(hook)", C.hook_calls);
    pf("runtime_invoke_private_entry_in_image", C.invoke_in_image);
    pf("runtime_invoke_private_entry_in_image_ok", C.invoke_ok);
    pf("verify_snapshot_hits", C.snapshot_hits);
    pf("verify_sha_fallbacks", C.sha_fallbacks);
    pf("verify_sha_failures", C.sha_fail);
    pf("private_scan_rows", C.scan_rows);
    pf("ppu_tick_calls", C.ppu_ticks);
    pf("render_scanline_calls", C.render_calls);
    pf("audio_tick_calls", C.audio_ticks);
    pf("timer_tick_calls", C.timer_ticks);
    pf("timed_dma_runs", C.dma_runs);
    for (int p = 0; p < 7; ++p)
        std::fprintf(stderr, "PERF part=%s dispatch=%llu tail=%llu\n",
                     p < 6 ? mzm_nes_emulator::kParts[p].name : "other",
                     (unsigned long long)C.part_dispatch[p], (unsigned long long)C.part_tail[p]);
    if (C.scan_calls)
        std::fprintf(stderr, "PERF scan_avg_rows=%.1f table_len=%u\n",
                     static_cast<double>(C.scan_rows) / static_cast<double>(C.scan_calls),
                     kPrivateDispatchTableLen);
    if (g_pairs && g_pairs_path) {
        if (std::FILE* f = std::fopen(g_pairs_path, "w")) {
            std::fprintf(f, "anchor\t%p\n", reinterpret_cast<void*>(&mzm_perf::init));
            std::vector<const PairSlot*> v;
            for (std::size_t i = 0; i < kPairSlots; ++i) if (g_pairs[i].n) v.push_back(&g_pairs[i]);
            std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->n > b->n; });
            for (auto* s : v)
                std::fprintf(f, "%llx\t%08x\t%u\t%llu\n", (unsigned long long)s->ret, s->target, s->kind,
                             (unsigned long long)s->n);
            std::fclose(f);
        }
    }
    if (g_fns && g_fn_path) {
        if (std::FILE* f = std::fopen(g_fn_path, "w")) {
            std::fprintf(f, "total\t%llu\n", (unsigned long long)g_fn_total);
            std::vector<const FnSlot*> v;
            for (std::size_t i = 0; i < kFnSlots; ++i) if (g_fns[i].n) v.push_back(&g_fns[i]);
            std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->n > b->n; });
            for (auto* s : v) std::fprintf(f, "%08x\t%llu\n", s->pc, (unsigned long long)s->n);
            std::fclose(f);
        }
        std::fprintf(stderr, "PERF count function_entries(all native fn activations) total=%llu per_frame=%.1f\n",
                     (unsigned long long)g_fn_total, static_cast<double>(g_fn_total) / fr);
    }
}

}  // namespace mzm_perf

// ---- --wrap shims (see CMake: MZM_PERF_PROFILE) -----------------------------------------
using mzm_perf::Scope;
extern "C" {
void __real_runtime_dispatch(std::uint32_t);
void __real_runtime_dispatch_tail(std::uint32_t);
void __real_runtime_dispatch_with_exchange(std::uint32_t);
void __real_runtime_dispatch_with_exchange_tail(std::uint32_t);
void __real_runtime_tail_drain(void);

void __wrap_runtime_dispatch(std::uint32_t pc) {
    ++mzm_perf::C.dispatch; ++mzm_perf::C.part_dispatch[mzm_perf::part_index(pc & ~1u)];
    mzm_perf::record_pair(__builtin_return_address(0), pc & ~1u, 0);
    Scope s(mzm_perf::B_RT); __real_runtime_dispatch(pc);
}
void __wrap_runtime_dispatch_tail(std::uint32_t pc) {
    ++mzm_perf::C.dispatch_tail; ++mzm_perf::C.part_tail[mzm_perf::part_index(pc & ~1u)];
    mzm_perf::record_pair(__builtin_return_address(0), pc & ~1u, 1);
    Scope s(mzm_perf::B_RT); __real_runtime_dispatch_tail(pc);
}
void __wrap_runtime_dispatch_with_exchange(std::uint32_t pc) {
    ++mzm_perf::C.exch; ++mzm_perf::C.part_dispatch[mzm_perf::part_index(pc & ~1u)];
    mzm_perf::record_pair(__builtin_return_address(0), pc & ~1u, 2);
    Scope s(mzm_perf::B_RT); __real_runtime_dispatch_with_exchange(pc);
}
void __wrap_runtime_dispatch_with_exchange_tail(std::uint32_t pc) {
    ++mzm_perf::C.exch_tail; ++mzm_perf::C.part_tail[mzm_perf::part_index(pc & ~1u)];
    mzm_perf::record_pair(__builtin_return_address(0), pc & ~1u, 3);
    Scope s(mzm_perf::B_RT); __real_runtime_dispatch_with_exchange_tail(pc);
}
void __wrap_runtime_tail_drain(void) {
    ++mzm_perf::C.drain_calls;
    if (g_runtime_tail_pending) ++mzm_perf::C.drain_pending;
    Scope s(mzm_perf::B_RT); __real_runtime_tail_drain();
}
#ifdef MZM_PERF_WRAP_TICK
void __real_runtime_tick(std::uint32_t);
void __wrap_runtime_tick(std::uint32_t c) { ++mzm_perf::C.ticks; __real_runtime_tick(c); }
#endif

gba::GbaPpu::TickEvents __real__ZN3gba6GbaPpu4tickEjt(gba::GbaPpu*, std::uint32_t, std::uint16_t);
gba::GbaPpu::TickEvents __wrap__ZN3gba6GbaPpu4tickEjt(gba::GbaPpu* self, std::uint32_t c, std::uint16_t v) {
    ++mzm_perf::C.ppu_ticks; Scope s(mzm_perf::B_PPU_TICK);
    return __real__ZN3gba6GbaPpu4tickEjt(self, c, v);
}
void __real__ZN3gba6GbaPpu15render_scanlineEjtPKhS2_S2_S2_(gba::GbaPpu*, std::uint32_t, std::uint16_t,
    const std::uint8_t*, const std::uint8_t*, const std::uint8_t*, const std::uint8_t*);
void __wrap__ZN3gba6GbaPpu15render_scanlineEjtPKhS2_S2_S2_(gba::GbaPpu* self, std::uint32_t y, std::uint16_t d,
    const std::uint8_t* a, const std::uint8_t* b, const std::uint8_t* c, const std::uint8_t* e) {
    ++mzm_perf::C.render_calls; Scope s(mzm_perf::B_PPU_RENDER);
    __real__ZN3gba6GbaPpu15render_scanlineEjtPKhS2_S2_S2_(self, y, d, a, b, c, e);
}
void __real__ZN3gba8GbaAudio4tickEj(gba::GbaAudio*, std::uint32_t);
void __wrap__ZN3gba8GbaAudio4tickEj(gba::GbaAudio* self, std::uint32_t c) {
    ++mzm_perf::C.audio_ticks; Scope s(mzm_perf::B_AUDIO); __real__ZN3gba8GbaAudio4tickEj(self, c);
}
void __real__ZN3gba5GbaIo11tick_timersEj(gba::GbaIo*, std::uint32_t);
void __wrap__ZN3gba5GbaIo11tick_timersEj(gba::GbaIo* self, std::uint32_t c) {
    ++mzm_perf::C.timer_ticks; Scope s(mzm_perf::B_TIMERS); __real__ZN3gba5GbaIo11tick_timersEj(self, c);
}
void __real__ZN3gba5GbaIo13run_timed_dmaEi(gba::GbaIo*, int);
void __wrap__ZN3gba5GbaIo13run_timed_dmaEi(gba::GbaIo* self, int m) {
    ++mzm_perf::C.dma_runs; Scope s(mzm_perf::B_DMA); __real__ZN3gba5GbaIo13run_timed_dmaEi(self, m);
}
}  // extern "C"
#endif  // MZM_PERF_PROFILE
