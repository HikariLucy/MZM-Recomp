// Host display settings: defaults, parsing, persistence text handling, layout
// maths and meters. Header-only logic from GBARecomp's display_settings.h /
// host_perf.h — no window, no SDL, no ROM. Visual appearance of the CRT pass is
// NOT unit-tested here (see scripts/run-host-display.py for the real-window
// qualification).

#include "display_settings.h"
#include "host_perf.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace gbarecomp;

namespace {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}
#define CHECK(expr) check((expr), #expr, __LINE__)

constexpr int W = 240, H = 160;

bool same_rect(const PresentationLayout& l, int x, int y, int w, int h) {
    return l.x == x && l.y == y && l.width == w && l.height == h;
}

DisplaySettings with(ScaleMode sm, AspectMode am) {
    DisplaySettings s;
    s.scale_mode = sm;
    s.aspect = am;
    return s;
}

void test_defaults() {
    const DisplaySettings d;
    CHECK(d.window_mode == WindowMode::Windowed);
    CHECK(d.window_scale == 3);
    CHECK(d.scale_mode == ScaleMode::Fit);
    CHECK(d.aspect == AspectMode::Native);
    CHECK(d.filtering == FilterMode::Nearest);
    CHECK(!d.vsync);            // FramePacer stays the only emulation clock
    CHECK(!d.show_fps);
    CHECK(!d.crt_enabled);
    CHECK(d.color_profile == ColorProfile::Off);
    CHECK(d.scanline_strength == kDefaultScanlineStrength);
    CHECK(!d.needs_explicit_layout());   // defaults keep the historical SDL path
    DisplaySettings crt = d;
    crt.crt_enabled = true;
    CHECK(crt.needs_explicit_layout());
}

void test_parse() {
    DisplaySettings s;
    CHECK(display_settings_set(&s, "window_mode", "Borderless"));
    CHECK(s.window_mode == WindowMode::Borderless);
    CHECK(display_settings_set(&s, "window_mode", "exclusive"));
    CHECK(s.window_mode == WindowMode::Exclusive);
    CHECK(display_settings_set(&s, "filtering", "LINEAR"));
    CHECK(s.filtering == FilterMode::Linear);
    CHECK(display_settings_set(&s, "vsync", "true"));
    CHECK(s.vsync);
    CHECK(display_settings_set(&s, "scanline_strength", "75"));
    CHECK(s.scanline_strength == 75);

    // Invalid values: rejected, field untouched (safe fallback).
    CHECK(!display_settings_set(&s, "window_mode", "fullscreen-ish"));
    CHECK(s.window_mode == WindowMode::Exclusive);
    CHECK(!display_settings_set(&s, "window_scale", "0"));
    CHECK(!display_settings_set(&s, "window_scale", "7"));
    CHECK(!display_settings_set(&s, "window_scale", "-3"));
    CHECK(!display_settings_set(&s, "window_scale", "abc"));
    CHECK(!display_settings_set(&s, "window_scale", ""));
    CHECK(s.window_scale == kDefaultWindowScale);
    CHECK(!display_settings_set(&s, "scanline_strength", "101"));
    CHECK(!display_settings_set(&s, "scanline_strength", "99999999999"));
    CHECK(s.scanline_strength == 75);
    CHECK(!display_settings_set(&s, "vsync", "maybe"));
    CHECK(s.vsync);
    CHECK(!display_settings_set(&s, "color_profile", "exact-lcd"));
    CHECK(s.color_profile == ColorProfile::Off);
    // Unknown key is ignored, never an error for the caller.
    CHECK(!display_settings_set(&s, "future_option", "1"));

    DisplaySettings wild;
    wild.window_scale = 99;
    wild.scanline_strength = -50;
    sanitize_display_settings(&wild);
    CHECK(wild.window_scale == kMaxWindowScale);
    CHECK(wild.scanline_strength == 0);
}

void test_roundtrip_and_ini() {
    DisplaySettings a;
    a.window_mode = WindowMode::Borderless;
    a.window_scale = 5;
    a.scale_mode = ScaleMode::IntegerFit;
    a.aspect = AspectMode::Stretch;
    a.filtering = FilterMode::Linear;
    a.vsync = true;
    a.show_fps = true;
    a.crt_enabled = true;
    a.crt_preset = CrtPreset::Custom;   // 65 is not a preset's strength (ENHANCEMENTS-2)
    a.scanline_strength = 65;
    a.presentation = PresentationMode::MonitorRefresh;
    a.color_profile = ColorProfile::GbaLike;
    DisplaySettings b;
    int rejected = -1;
    CHECK(display_settings_parse_ini(&b, display_settings_to_ini(a), &rejected));
    CHECK(rejected == 0);
    CHECK(a == b);

    // Defaults survive a round trip too.
    DisplaySettings c, d;
    CHECK(display_settings_parse_ini(&d, display_settings_to_ini(c)));
    CHECK(c == d);

    // No section / only other sections: nothing applied, settings untouched.
    DisplaySettings e;
    CHECK(!display_settings_parse_ini(&e, "[KeyMap]\nFullscreen = F\n"));
    CHECK(e == DisplaySettings{});
    CHECK(!display_settings_parse_ini(&e, ""));

    // Partially invalid file: valid keys apply, bad ones fall back.
    DisplaySettings f;
    const std::string mixed =
        "[Display]\nwindow_scale = 9\nfiltering = linear\naspect = nonsense\n"
        "scanline_strength = 55 # comment\nbogus\n";
    int bad = 0;
    CHECK(display_settings_parse_ini(&f, mixed, &bad));
    CHECK(f.filtering == FilterMode::Linear);
    CHECK(f.window_scale == kDefaultWindowScale);
    CHECK(f.aspect == AspectMode::Native);
    CHECK(f.scanline_strength == 55);
    CHECK(bad == 3);

    // [display] header is case-insensitive; keys in other sections are ignored.
    DisplaySettings g;
    CHECK(display_settings_parse_ini(&g, "[Touch]\nfiltering = linear\n\n[DISPLAY]\nvsync = 1\n"));
    CHECK(g.vsync && g.filtering == FilterMode::Nearest);
}

void test_persistence_text() {
    const std::string original =
        "[KeyMap]\r\nFullscreen = Alt+Return\r\n\r\n[Touch]\r\npad_visible = 1\r\n";
    DisplaySettings s;
    s.crt_enabled = true;
    const std::string once = ini_replace_section(original, "Display", display_settings_to_ini(s));
    CHECK(once.find("[KeyMap]") != std::string::npos);
    CHECK(once.find("Fullscreen = Alt+Return") != std::string::npos);
    CHECK(once.find("pad_visible = 1") != std::string::npos);
    CHECK(once.find("[Display]") != std::string::npos);

    // Idempotent: saving again never duplicates the section.
    const std::string twice = ini_replace_section(once, "Display", display_settings_to_ini(s));
    CHECK(twice == once);
    std::size_t count = 0;
    for (std::size_t p = twice.find("[Display]"); p != std::string::npos;
         p = twice.find("[Display]", p + 1))
        ++count;
    CHECK(count == 1);

    // A changed value replaces, does not append.
    DisplaySettings t = s;
    t.crt_preset = CrtPreset::Custom;   // a hand-set strength is a Custom look
    t.scanline_strength = 10;
    const std::string third = ini_replace_section(twice, "Display", display_settings_to_ini(t));
    DisplaySettings back;
    CHECK(display_settings_parse_ini(&back, third));
    CHECK(back == t);
    CHECK(third.find("scanline_strength = 40") == std::string::npos);

    // Section in the middle of the file is replaced in place of removal+append.
    const std::string middle =
        "[Touch]\npad_visible = 0\n[Display]\nvsync = 1\n[KeyMap]\nPause = P\n";
    const std::string m2 = ini_replace_section(middle, "Display", display_settings_to_ini(DisplaySettings{}));
    CHECK(m2.find("Pause = P") != std::string::npos);
    CHECK(m2.find("pad_visible = 0") != std::string::npos);
    DisplaySettings m3;
    CHECK(display_settings_parse_ini(&m3, m2));
    CHECK(!m3.vsync);

    // Brand-new / empty file.
    const std::string fresh = ini_replace_section("", "Display", display_settings_to_ini(s));
    CHECK(fresh == display_settings_to_ini(s));
}

void test_layout_native_fit() {
    const DisplaySettings d;
    CHECK(same_rect(compute_display_layout(720, 480, W, H, d), 0, 0, 720, 480));
    // 16:9 window: pillarbox, 3:2 preserved (scale 4.5).
    CHECK(same_rect(compute_display_layout(1280, 720, W, H, d), 100, 0, 1080, 720));
    // Tall window: letterbox.
    CHECK(same_rect(compute_display_layout(720, 900, W, H, d), 0, 210, 720, 480));
    // Degenerate sizes never produce a layout.
    CHECK(same_rect(compute_display_layout(0, 480, W, H, d), 0, 0, 0, 0));
    CHECK(same_rect(compute_display_layout(720, 480, 0, H, d), 0, 0, 0, 0));

    // Property: always inside the drawable, centred within a pixel, and the
    // aspect is 3:2 within one-pixel rounding. No stretching by default.
    for (int dw = 100; dw <= 2600; dw += 37) {
        for (int dh = 80; dh <= 1500; dh += 53) {
            const PresentationLayout l = compute_display_layout(dw, dh, W, H, d);
            CHECK(l.width > 0 && l.height > 0);
            CHECK(l.x >= 0 && l.y >= 0);
            CHECK(l.x + l.width <= dw && l.y + l.height <= dh);
            CHECK(std::abs((dw - l.width) - 2 * l.x) <= 1);
            CHECK(std::abs((dh - l.height) - 2 * l.y) <= 1);
            const double aspect = static_cast<double>(l.width) / l.height;
            CHECK(std::fabs(aspect - 1.5) <= 1.5 / std::min(l.width, l.height) + 1e-9);
            // Fills one axis completely (letter/pillarbox, never smaller).
            CHECK(l.width == dw || l.height == dh);
        }
    }
}

void test_layout_integer() {
    const DisplaySettings d = with(ScaleMode::IntegerFit, AspectMode::Native);
    CHECK(same_rect(compute_display_layout(1920, 1080, W, H, d), 240, 60, 1440, 960));
    CHECK(compute_display_layout(1920, 1080, W, H, d).integer_scale == 6);
    CHECK(same_rect(compute_display_layout(1366, 768, W, H, d), 203, 64, 960, 640));
    CHECK(same_rect(compute_display_layout(720, 480, W, H, d), 0, 0, 720, 480));
    // Exactly 1x.
    CHECK(same_rect(compute_display_layout(240, 160, W, H, d), 0, 0, 240, 160));
    // Smaller than 1x: no whole multiple fits and cropping is forbidden, so it
    // falls back to a letterboxed fit instead of overflowing.
    const PresentationLayout small = compute_display_layout(200, 100, W, H, d);
    CHECK(small.width <= 200 && small.height <= 100 && small.width > 0);
    CHECK(small.integer_scale == 0);

    for (int dw = 240; dw <= 3000; dw += 61) {
        for (int dh = 160; dh <= 1700; dh += 47) {
            const PresentationLayout l = compute_display_layout(dw, dh, W, H, d);
            const int k = l.integer_scale;
            CHECK(k >= 1);
            CHECK(l.width == W * k && l.height == H * k);          // whole multiple
            CHECK(l.x + l.width <= dw && l.y + l.height <= dh);    // never cropped
            CHECK((W * (k + 1) > dw) || (H * (k + 1) > dh));       // largest that fits
            CHECK(std::abs((dw - l.width) - 2 * l.x) <= 1);        // centred
            CHECK(std::abs((dh - l.height) - 2 * l.y) <= 1);
        }
    }
}

void test_layout_stretch() {
    const DisplaySettings st = with(ScaleMode::Fit, AspectMode::Stretch);
    CHECK(same_rect(compute_display_layout(1280, 720, W, H, st), 0, 0, 1280, 720));
    // Stretch wins over integer scaling (the UI disables integer while stretched).
    const DisplaySettings both = with(ScaleMode::IntegerFit, AspectMode::Stretch);
    CHECK(same_rect(compute_display_layout(1280, 720, W, H, both), 0, 0, 1280, 720));
    CHECK(st.needs_explicit_layout());
}

void test_scanlines() {
    CHECK(scanline_peak_alpha(0) == 0.0);
    CHECK(std::fabs(scanline_peak_alpha(100) - kScanlinePeakAlpha) < 1e-12);
    CHECK(scanline_peak_alpha(5000) == scanline_peak_alpha(100));   // clamped
    CHECK(scanline_peak_alpha(-5) == 0.0);
    for (int s = 1; s <= 100; ++s) CHECK(scanline_peak_alpha(s) > scanline_peak_alpha(s - 1));
    CHECK(kScanlinePeakAlpha <= 0.5);   // visibility floor: never darker than 50%

    const double peak = scanline_peak_alpha(100);
    for (int scale = 2; scale <= 8; ++scale) {
        const int dh = H * scale;
        double lo = 1.0, hi = 0.0, sum = 0.0;
        for (int y = 0; y < dh; ++y) {
            const double a = scanline_row_alpha(y, dh, H, peak);
            CHECK(a >= 0.0 && a <= peak + 1e-12);
            lo = std::min(lo, a);
            hi = std::max(hi, a);
            sum += a;
        }
        CHECK(hi > peak * 0.85);    // a clearly dark row exists at every scale >= 2
        CHECK(lo < peak * 0.35);    // and a clearly light one
        CHECK(sum / dh < peak * 0.6);   // average darkening stays moderate
    }
    // At exactly 2x every guest line alternates light, dark.
    for (int line = 0; line < H; ++line) {
        const double top = scanline_row_alpha(2 * line, 2 * H, H, peak);
        const double bot = scanline_row_alpha(2 * line + 1, 2 * H, H, peak);
        CHECK(bot > top + peak * 0.5);
    }
    // 1x leaves no room for a pattern; strength 0 draws nothing.
    CHECK(scanline_row_alpha(10, H, H, peak) == 0.0);
    CHECK(scanline_row_alpha(10, 2 * H, H, 0.0) == 0.0);
    // Non-integer fit still follows the raster (pattern repeats H times).
    int dark_rows = 0;
    for (int y = 0; y < 1000; ++y)
        if (scanline_row_alpha(y, 1000, H, peak) > peak * 0.9) ++dark_rows;
    CHECK(dark_rows > 0);
}

void test_meters() {
    RateMeter m(500);
    for (int i = 0; i <= 120; ++i) m.sample(i, static_cast<std::uint32_t>(i * 1000.0 / 60.0 + 1000));
    CHECK(std::fabs(m.rate() - 60.0) < 1.5);

    // 59.7275 Hz guest cadence reads back close to native.
    RateMeter emu(500);
    for (int i = 0; i <= 300; ++i) emu.sample(i, static_cast<std::uint32_t>(i * 1000.0 / 59.7275));
    CHECK(std::fabs(emu.rate() - 59.7275) < 1.5);

    // Counter going backwards (state load) resets instead of going negative.
    RateMeter r(500);
    r.sample(1000, 0);
    r.sample(1030, 500);
    r.sample(5, 1000);
    CHECK(r.rate() >= 0.0);

    // EMU and PRESENT are independent: a presenter running at 144 Hz over a
    // 59.7 Hz guest reports both, never conflating them.
    RateMeter present(500), guest(500);
    for (int ms = 0; ms <= 2000; ++ms) {
        present.sample(static_cast<std::uint64_t>(ms * 144.0 / 1000.0), static_cast<std::uint32_t>(ms));
        guest.sample(static_cast<std::uint64_t>(ms * 59.7275 / 1000.0), static_cast<std::uint32_t>(ms));
    }
    CHECK(present.rate() > 140.0 && present.rate() < 148.0);
    CHECK(guest.rate() > 57.0 && guest.rate() < 62.0);

    IntervalMeter f(500);
    std::uint64_t t = 1000000;
    // One 500 ms window containing a single 33 ms hitch.
    for (int i = 0; i <= 31; ++i) { f.tick(t); t += (i == 10) ? 33000 : 16667; }
    CHECK(std::fabs(f.avg_ms() - 17.2) < 1.5);
    CHECK(f.max_ms() > 30.0);
}

}  // namespace

int main() {
    test_defaults();
    test_parse();
    test_roundtrip_and_ini();
    test_persistence_text();
    test_layout_native_fit();
    test_layout_integer();
    test_layout_stretch();
    test_scanlines();
    test_meters();
    if (g_failures) {
        std::fprintf(stderr, "%d display-settings check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("display settings: ok");
    return 0;
}
