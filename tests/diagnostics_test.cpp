// Pure tests for the Support / report-problem text. No window, no files, no network.
#include "mzm_diagnostics.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)
static bool has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

using namespace mzm;

static DiagnosticInput full_input() {
    DiagnosticInput in;
    in.build = {"Beta 4", "beta", "0.4.0", "f30240a2f088a45935c91ca74131dc1b546759dc",
                "266f82f556fe995ece1bfc558cfe59496b0ef31c", "2b7e9c6140be", "GCC 13.3.0", "Release", "Windows x64"};
    in.system.os = "Windows 11 (build 22631)"; in.system.arch = "x86_64"; in.system.cpu = "Test CPU";
    in.system.gpu = "Test GPU"; in.system.gl_version = "4.6 Core"; in.system.display_refresh_hz = 144;
    in.system.controller_known = true; in.system.controller = "Test Pad";
    in.system.audio_device = "Speakers"; in.system.audio_driver = "wasapi";
    in.settings = {"monitor", "borderless", "bilinear", "integer", "soft (22%)", "off"};
    in.rom = FileState::Valid; in.bios = FileState::Valid; in.strict_static = true;
    in.config_path = "%USERPROFILE%\\AppData\\Roaming\\MZMRecompiled\\config.ini";
    in.log_path = "%USERPROFILE%\\AppData\\Local\\MZMRecompiled\\logs\\latest.log";
    in.last_session = SessionStatus::Clean; in.problem_area = "Audio";
    return in;
}

int main() {
    // --- path redaction
    CHECK(redact_user_path("/home/jesus/Games/mzm.gba", "/home/jesus") == "~/Games/mzm.gba");
    CHECK(redact_user_path("/home/jesus", "/home/jesus/") == "~");
    CHECK(redact_user_path("/home/jesus2/x", "/home/jesus") == "/home/jesus2/x");      // not a prefix on a boundary
    CHECK(redact_user_path("/opt/x", "/home/jesus") == "/opt/x");
    CHECK(redact_user_path("C:\\Users\\Jesus\\Games\\mzm.gba", "C:\\Users\\Jesus") == "%USERPROFILE%\\Games\\mzm.gba");
    CHECK(redact_user_path("c:/users/jesus/Games", "C:\\Users\\Jesus") == "%USERPROFILE%/Games");
    CHECK(redact_user_path("D:\\Games\\x.gba", "C:\\Users\\Jesus") == "D:\\Games\\x.gba");
    CHECK(redact_user_path("/anything", "") == "/anything");
    CHECK(redact_user_path("/a/b", "/") == "/a/b");
    CHECK(redact_text("open /home/jesus/Games/a.gba by jesus", "/home/jesus") == "open ~/Games/a.gba by <user>");
    CHECK(redact_text("user me at /home/me/x", "/home/me") == "user me at ~/x");           // short name kept, path redacted
    CHECK(!has(redact_text("C:\\Users\\Jesus\\x and C:\\Users\\Jesus", "C:\\Users\\Jesus"), "Users"));
    CHECK(redact_text("nothing here", "/home/jesus") == "nothing here");

    // --- urls
    CHECK(percent_encode("a b/c%") == "a%20b/c%25");
    CHECK(file_url("/home/a b/logs") == "file:///home/a%20b/logs");
    CHECK(file_url("C:\\Users\\A B\\logs") == "file:///C:/Users/A%20B/logs");
    const std::string url = issue_url();
    CHECK(url.rfind("https://github.com/HikariLucy/MZM-Recomp/issues/new?", 0) == 0);
    CHECK(has(url, "template=bug_report.yml") && has(url, "title=%5BBeta%5D%20") && url.size() < 200);
    CHECK(!has(url, "home") && !has(url, "Users"));          // never carries diagnostics

    // --- session status
    CHECK(session_status_from_log("") == SessionStatus::Unknown);
    CHECK(session_status_from_log("[mzm] t start\n[mzm] t closed result=ok\n") == SessionStatus::Clean);
    CHECK(session_status_from_log("[mzm] t start\n[mzm] t launcher_closed\n") == SessionStatus::Clean);
    CHECK(session_status_from_log("[mzm] t start\nstuff\n") == SessionStatus::Unexpected);
    CHECK(session_status_from_log("a\nMZMRecompiled CRASH: fatal signal SIGSEGV\n") == SessionStatus::Crash);
    CHECK(session_status_from_log("MZMRecompiled CRASH: x\n[mzm] t closed result=ok") == SessionStatus::Crash);
    CHECK(has(session_status_sentence(SessionStatus::Crash), "crash was detected"));
    CHECK(has(session_status_sentence(SessionStatus::Unexpected), "unexpectedly"));
    CHECK(std::string(session_status_name(SessionStatus::Unknown)) == "none");

    // --- tail
    CHECK(tail_lines("1\n2\n3\n4\n", 2) == "3\n4\n");
    CHECK(tail_lines("1\n2\n", 10) == "1\n2\n");
    CHECK(tail_lines("", 3).empty());

    // --- settings
    auto d = describe_display_settings("");
    CHECK(has(d.presentation, "default") && has(d.crt, "off"));
    d = describe_display_settings("[Display]\npresentation = monitor\nwindow_mode = borderless\nfiltering = linear\n"
                                  "scale_mode = integer\ncrt_enabled = 1\ncrt_preset = soft\ncolor_profile = gba-like\n");
    CHECK(d.presentation == "monitor" && d.window_mode == "borderless" && d.filtering == "bilinear");
    CHECK(d.scaling == "integer" && has(d.crt, "soft") && has(d.color_profile, "approximate"));
    d = describe_display_settings("[Other]\nx=1\n");
    CHECK(has(d.presentation, "default"));

    // --- build info
    const BuildInfo b = current_build_info();
    CHECK(!b.platform.empty());                                   // compiled-in, "unknown" allowed for the rest
    CHECK(short_sha("f30240a2f088a45935") == "f30240a" && short_sha("") == "unknown");
    CHECK(short_sha("f30240a2f088-dirty") == "f30240a-dirty");
    const std::string bi = format_build_info(full_input().build);
    CHECK(has(bi, "MZM SHA: f30240a2f088a45935c91ca74131dc1b546759dc") && has(bi, "GBARecomp SHA: 266f82f5"));
    CHECK(has(bi, "Build type: Release") && has(bi, "Beta 4"));
    CHECK(has(format_build_info(BuildInfo{}), "MZM SHA: Unknown"));       // missing fields

    // --- diagnostic report
    std::string r = format_diagnostic(full_input());
    for (const char* k : {"MZM Recompiled Diagnostic Report", "MZM SHA:", "GBARecomp SHA:", "Platform: Windows x64",
                          "OS: Windows 11", "CPU: Test CPU", "GPU: Test GPU", "Renderer: OpenGL 4.6 Core",
                          "Display refresh: 144 Hz", "~59.73 Hz", "Gamepad: Test Pad", "Presentation mode: monitor",
                          "Window mode: borderless", "Filter: bilinear", "CRT preset: soft (22%)", "Strict static: requested",
                          "ROM validation: configured, valid", "BIOS validation: configured, valid",
                          "Config path: %USERPROFILE%", "Log path: %USERPROFILE%", "Last session: Clean exit",
                          "No ROM/BIOS contents"})
        CHECK(has(r, k));
    CHECK(!has(r, "C:\\Users") && !has(r, "/home/"));
    DiagnosticInput empty;
    r = format_diagnostic(empty);
    CHECK(has(r, "CPU: Unknown") && has(r, "GPU: Unknown") && has(r, "Display refresh: Unknown") && has(r, "Gamepad: Unknown"));
    CHECK(has(r, "ROM validation: not configured") && has(r, "Last session: Unknown"));
    CHECK(has(r, "Strict static: not requested") && has(r, "Renderer: Unknown"));
    DiagnosticInput nopad = full_input(); nopad.system.controller.clear();
    CHECK(has(format_diagnostic(nopad), "Gamepad: not connected"));
    DiagnosticInput bad = full_input(); bad.rom = FileState::Invalid;
    CHECK(has(format_diagnostic(bad), "ROM validation: configured, INVALID"));
    DiagnosticInput crash = full_input(); crash.last_session = SessionStatus::Crash;
    CHECK(has(format_diagnostic(crash), "A previous crash was detected"));

    // --- bug report template
    r = format_bug_report(full_input());
    for (const char* k : {"## MZM Recompiled bug report", "### Build", "### Hardware", "### What happened",
                          "### Steps to reproduce", "1. ", "### Expected", "### Actual", "### Components",
                          "Launcher:", "MZM gameplay:", "NES:", "Audio:", "Enhancements:", "Save:", "### Diagnostics",
                          "Strict:", "Renderer:", "Presentation: monitor", "Window mode:", "CRT:", "### Log",
                          "latest.log", "Do not upload ROM or BIOS", "### Problem area\nAudio", "screenshot"})
        CHECK(has(r, k));
    CHECK(has(format_bug_report(empty), "### Problem area\nOther"));
    CHECK(r.size() < 3000);                                       // pasteable, never a log dump

    // --- static texts
    CHECK(has(security_guidance(), "Do NOT disable your antivirus") && has(security_guidance(), "SHA-256") &&
          has(security_guidance(), "authorized personal PC"));
    CHECK(!has(security_guidance(), "exclusion ") );
    CHECK(has(feedback_guide_text(), "Do NOT upload your ROM"));
    CHECK(problem_areas().size() == 11 && problem_areas().front() == "Launcher" && problem_areas().back() == "Other");

    std::printf(g_failed ? "mzm_diagnostics_test: %d FAILED\n" : "mzm_diagnostics_test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
