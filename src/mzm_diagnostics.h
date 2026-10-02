#pragma once
// Support / report-problem text generation. Pure functions: no SDL, no window,
// no file or network access (except session_status_from_file), so everything
// here is unit-testable. Nothing in this module uploads anything.

#include <string>
#include <vector>

namespace mzm {

struct BuildInfo {
    std::string release_label;   // "Beta 4"
    std::string channel;         // development | beta | rc | stable
    std::string version;         // "0.4.0"
    std::string mzm_sha;
    std::string gbarecomp_sha;
    std::string recomp_ui_sha;
    std::string compiler;
    std::string build_type;
    std::string platform;        // "Windows x64" / "Linux x86_64"
};

struct SystemInfo {
    std::string os, arch, cpu, gpu, gl_version;
    int display_refresh_hz = 0;  // 0 = unknown
    std::string controller;      // empty = not connected / unknown
    bool controller_known = false;
    std::string audio_driver, audio_device;
};

struct SettingsInfo {            // from config.ini [Display]; empty = default/unknown
    std::string presentation, window_mode, filtering, scaling, crt, color_profile;
};

enum class SessionStatus { Unknown, Clean, Unexpected, Crash };
enum class FileState { NotConfigured, Valid, Invalid };

struct DiagnosticInput {
    BuildInfo build;
    SystemInfo system;
    SettingsInfo settings;
    FileState rom = FileState::NotConfigured, bios = FileState::NotConfigured;
    bool strict_static = false;
    std::string config_path, log_path;   // already user-path-redacted by the caller
    SessionStatus last_session = SessionStatus::Unknown;
    std::string problem_area;
};

// ---- redaction -------------------------------------------------------------
// Replace a leading home directory with "~" (POSIX) or "%USERPROFILE%" (Windows
// paths, case-insensitive, either slash). Anything else is returned unchanged.
std::string redact_user_path(const std::string& path, const std::string& home);
// Redact the home directory (and the bare user name when it is long enough to be
// unambiguous) wherever it appears inside free text such as log lines.
std::string redact_text(const std::string& text, const std::string& home);

// ---- urls ------------------------------------------------------------------
std::string percent_encode(const std::string& s);
std::string file_url(const std::string& absolute_path);
extern const char* const kIssuesNewUrl;
std::string issue_url();     // new-issue page with the bug-report template and a "[Beta] " title

// ---- status ----------------------------------------------------------------
SessionStatus session_status_from_log(const std::string& log_text);
const char* session_status_name(SessionStatus s);           // for log events
std::string session_status_sentence(SessionStatus s);       // for people
std::string tail_lines(const std::string& text, size_t n);

// ---- settings --------------------------------------------------------------
SettingsInfo describe_display_settings(const std::string& config_ini_text);

// ---- build -----------------------------------------------------------------
BuildInfo current_build_info();                              // compiled-in metadata
std::string short_sha(const std::string& sha);               // 7 chars, "unknown" kept
std::string build_headline(const BuildInfo& b);              // "MZM Recompiled Beta 4"

// ---- formatted text (all plain UTF-8, ready for the clipboard) -------------
const std::vector<std::string>& problem_areas();
std::string format_build_info(const BuildInfo& b);
std::string format_diagnostic(const DiagnosticInput& in);
std::string format_bug_report(const DiagnosticInput& in);
std::string security_guidance();    // antivirus / SmartScreen text
std::string feedback_guide_text();  // minimal built-in instructions

}  // namespace mzm
