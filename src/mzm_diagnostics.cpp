#include "mzm_diagnostics.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "display_settings.h"

#ifndef MZM_BUILD_SHA
#define MZM_BUILD_SHA "unknown"
#endif
#ifndef MZM_GBARECOMP_SHA
#define MZM_GBARECOMP_SHA "unknown"
#endif
#ifndef MZM_RECOMP_UI_SHA
#define MZM_RECOMP_UI_SHA "unknown"
#endif
#ifndef MZM_RELEASE_LABEL
#define MZM_RELEASE_LABEL "dev"
#endif
#ifndef MZM_RELEASE_CHANNEL
#define MZM_RELEASE_CHANNEL "development"
#endif
#ifndef MZM_VERSION
#define MZM_VERSION "unknown"
#endif
#ifndef MZM_COMPILER
#define MZM_COMPILER "unknown"
#endif
#ifndef MZM_BUILD_TYPE
#define MZM_BUILD_TYPE "unknown"
#endif
#ifdef _WIN32
#define MZM_PLATFORM_NAME "Windows x64"
#elif defined(__linux__)
#define MZM_PLATFORM_NAME "Linux x86_64"
#else
#define MZM_PLATFORM_NAME "unknown"
#endif

namespace mzm {

const char* const kIssuesNewUrl = "https://github.com/HikariLucy/MZM-Recomp/issues/new";

namespace {
std::string unknown_if_empty(const std::string& s) { return s.empty() ? "Unknown" : s; }

bool is_sep(char c) { return c == '/' || c == '\\'; }
char lower_c(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool windows_style(const std::string& s) {
    return s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && is_sep(s[2]);
}

// Length of `home` as a prefix of `path` (separator-insensitive, case-insensitive for
// Windows drive paths), or 0.
size_t home_prefix_len(const std::string& path, const std::string& home_in) {
    std::string home = home_in;
    while (home.size() > 1 && is_sep(home.back())) home.pop_back();
    if (home.size() < 2 || path.size() < home.size()) return 0;
    const bool win = windows_style(home);
    for (size_t i = 0; i < home.size(); ++i) {
        char a = path[i], b = home[i];
        if (win) { a = lower_c(a); b = lower_c(b); if (is_sep(a)) a = '/'; if (is_sep(b)) b = '/'; }
        if (a != b) return 0;
    }
    if (path.size() > home.size() && !is_sep(path[home.size()])) return 0;
    return home.size();
}

std::string last_component(const std::string& home) {
    std::string h = home;
    while (h.size() > 1 && is_sep(h.back())) h.pop_back();
    size_t i = h.find_last_of("/\\");
    return i == std::string::npos ? h : h.substr(i + 1);
}

const char* on_off(bool b) { return b ? "yes" : "no"; }

std::string file_state_name(FileState s) {
    return s == FileState::Valid ? "configured, valid"
         : s == FileState::Invalid ? "configured, INVALID" : "not configured";
}

std::string refresh_text(int hz) { return hz > 0 ? std::to_string(hz) + " Hz" : "Unknown"; }
}  // namespace

std::string redact_user_path(const std::string& path, const std::string& home) {
    const size_t n = home_prefix_len(path, home);
    if (!n) return path;
    const std::string rest = path.substr(n);
    return (windows_style(home) ? std::string("%USERPROFILE%") : std::string("~")) + rest;
}

std::string redact_text(const std::string& text, const std::string& home) {
    if (home.size() < 2) return text;
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const size_t n = home_prefix_len(text.substr(i, home.size() + 1), home);
        if (n) {
            out += windows_style(home) ? "%USERPROFILE%" : "~";
            i += n;
        } else {
            out += text[i++];
        }
    }
    const std::string user = last_component(home);
    // Only strip the bare account name when it is distinctive (avoids mangling "a", "me", "pi").
    if (user.size() >= 4) {
        std::string res;
        for (size_t i = 0; i < out.size();) {
            if (out.compare(i, user.size(), user) == 0) { res += "<user>"; i += user.size(); }
            else res += out[i++];
        }
        out = res;
    }
    return out;
}

std::string percent_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string file_url(const std::string& p) {
    std::string path = p;
    for (char& c : path) if (c == '\\') c = '/';
    if (!path.empty() && path[0] != '/') path = "/" + path;   // "C:/x" -> "/C:/x"
    std::string enc = percent_encode(path);
    size_t colon;                                             // keep the drive colon readable
    if ((colon = enc.find("%3A")) == 2) enc.replace(colon, 3, ":");
    return "file://" + enc;
}

std::string issue_url() {
    return std::string(kIssuesNewUrl) + "?template=bug_report.yml&title=" + percent_encode("[Beta] ") + "&labels=bug%2Cbeta";
}

SessionStatus session_status_from_log(const std::string& log) {
    if (log.empty()) return SessionStatus::Unknown;
    if (log.find("MZMRecompiled CRASH:") != std::string::npos) return SessionStatus::Crash;
    if (log.find(" closed result=") != std::string::npos || log.find(" launcher_closed") != std::string::npos)
        return SessionStatus::Clean;
    return SessionStatus::Unexpected;
}

const char* session_status_name(SessionStatus s) {
    switch (s) {
        case SessionStatus::Clean: return "clean";
        case SessionStatus::Unexpected: return "unexpected-end";
        case SessionStatus::Crash: return "crash-marker";
        default: return "none";
    }
}

std::string session_status_sentence(SessionStatus s) {
    switch (s) {
        case SessionStatus::Clean: return "Clean exit";
        case SessionStatus::Unexpected: return "Last session may have ended unexpectedly";
        case SessionStatus::Crash: return "A previous crash was detected (crash marker in the log)";
        default: return "Unknown (no previous log)";
    }
}

std::string tail_lines(const std::string& text, size_t n) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    const size_t start = lines.size() > n ? lines.size() - n : 0;
    std::string out;
    for (size_t i = start; i < lines.size(); ++i) { out += lines[i]; out += '\n'; }
    return out;
}

SettingsInfo describe_display_settings(const std::string& ini) {
    using namespace gbarecomp;
    SettingsInfo s;
    DisplaySettings d;
    if (ini.empty() || !display_settings_parse_ini(&d, ini)) {
        s.presentation = "native (default)"; s.window_mode = "windowed (default)";
        s.filtering = "nearest (default)"; s.scaling = "fit (default)";
        s.crt = "off (default)"; s.color_profile = "off (default)";
        return s;
    }
    s.presentation = presentation_mode_name(d.presentation);
    s.window_mode = window_mode_name(d.window_mode);
    s.filtering = d.filtering == FilterMode::Linear ? "bilinear" : "nearest";
    s.scaling = d.scale_mode == ScaleMode::IntegerFit ? "integer" : "fit";
    s.crt = d.crt_enabled ? std::string(crt_preset_name(d.crt_preset)) + " (" + std::to_string(d.scanline_strength) + "%)" : "off";
    s.color_profile = d.color_profile == ColorProfile::GbaLike ? "gba-like (approximate)" : "off";
    return s;
}

BuildInfo current_build_info() {
    BuildInfo b;
    b.release_label = MZM_RELEASE_LABEL; b.channel = MZM_RELEASE_CHANNEL; b.version = MZM_VERSION;
    b.mzm_sha = MZM_BUILD_SHA; b.gbarecomp_sha = MZM_GBARECOMP_SHA; b.recomp_ui_sha = MZM_RECOMP_UI_SHA;
    b.compiler = MZM_COMPILER; b.build_type = MZM_BUILD_TYPE; b.platform = MZM_PLATFORM_NAME;
    return b;
}

std::string short_sha(const std::string& sha) {
    if (sha.empty()) return "unknown";
    std::string s = sha.substr(0, 7);
    return sha.size() > 7 && sha.find("-dirty") != std::string::npos ? s + "-dirty" : s;
}

std::string build_headline(const BuildInfo& b) { return "MZM Recompiled " + unknown_if_empty(b.release_label); }

const std::vector<std::string>& problem_areas() {
    static const std::vector<std::string> a = {
        "Launcher", "Game startup", "Graphics", "Audio", "Controls", "Save", "NES",
        "Enhancements", "Performance", "Windows security / antivirus", "Other"};
    return a;
}

std::string format_build_info(const BuildInfo& b) {
    std::string o;
    o += build_headline(b) + " (" + unknown_if_empty(b.channel) + " channel, version " + unknown_if_empty(b.version) + ")\n";
    o += "MZM SHA: " + unknown_if_empty(b.mzm_sha) + "\n";
    o += "GBARecomp SHA: " + unknown_if_empty(b.gbarecomp_sha) + "\n";
    o += "recomp-ui SHA: " + unknown_if_empty(b.recomp_ui_sha) + "\n";
    o += "Compiler: " + unknown_if_empty(b.compiler) + "\n";
    o += "Build type: " + unknown_if_empty(b.build_type) + "\n";
    o += "Platform: " + unknown_if_empty(b.platform) + "\n";
    return o;
}

std::string format_diagnostic(const DiagnosticInput& in) {
    const auto& b = in.build; const auto& s = in.system; const auto& c = in.settings;
    std::string o = "MZM Recompiled Diagnostic Report\n\n";
    o += "Version: " + build_headline(b) + " (" + unknown_if_empty(b.version) + ", " + unknown_if_empty(b.channel) + ")\n";
    o += "MZM SHA: " + unknown_if_empty(b.mzm_sha) + "\n";
    o += "GBARecomp SHA: " + unknown_if_empty(b.gbarecomp_sha) + "\n";
    o += "recomp-ui SHA: " + unknown_if_empty(b.recomp_ui_sha) + "\n\n";
    o += "Platform: " + unknown_if_empty(b.platform) + "\n";
    o += "OS: " + unknown_if_empty(s.os) + "\n";
    o += "Architecture: " + unknown_if_empty(s.arch) + "\n\n";
    o += "CPU: " + unknown_if_empty(s.cpu) + "\n";
    o += "GPU: " + unknown_if_empty(s.gpu) + "\n";
    o += "Renderer: " + (s.gl_version.empty() ? std::string("Unknown") : "OpenGL " + s.gl_version) + "\n";
    o += "Display refresh: " + refresh_text(s.display_refresh_hz) + " (monitor; the game itself runs at ~59.73 Hz)\n\n";
    o += "Gamepad: " + (s.controller_known ? (s.controller.empty() ? std::string("not connected") : s.controller) : std::string("Unknown")) + "\n";
    o += "Audio: " + (s.audio_device.empty() ? std::string("Unknown") : s.audio_device) +
         (s.audio_driver.empty() ? "" : " [" + s.audio_driver + "]") + "\n";
    o += "Presentation mode: " + unknown_if_empty(c.presentation) + "\n";
    o += "Window mode: " + unknown_if_empty(c.window_mode) + "\n";
    o += "Filter: " + unknown_if_empty(c.filtering) + "\n";
    o += "Scaling: " + unknown_if_empty(c.scaling) + "\n";
    o += "CRT preset: " + unknown_if_empty(c.crt) + "\n";
    o += "Color profile: " + unknown_if_empty(c.color_profile) + "\n\n";
    o += std::string("Strict static: ") + (in.strict_static ? "requested" : "not requested") + "\n";
    o += "ROM validation: " + file_state_name(in.rom) + "\n";
    o += "BIOS validation: " + file_state_name(in.bios) + "\n\n";
    o += "Config path: " + unknown_if_empty(in.config_path) + "\n";
    o += "Log path: " + unknown_if_empty(in.log_path) + "\n";
    o += "Save path: beside the ROM file (<rom name>.sav)\n\n";
    o += "Last session: " + session_status_sentence(in.last_session) + "\n\n";
    o += "No ROM/BIOS contents, file paths of your games, save data, user name, host name or network\n";
    o += "information are included. Nothing was uploaded.\n";
    return o;
}

std::string format_bug_report(const DiagnosticInput& in) {
    const auto& b = in.build; const auto& s = in.system; const auto& c = in.settings;
    std::string o = "## MZM Recompiled bug report\n\n### Build\n";
    o += "MZM: " + unknown_if_empty(b.mzm_sha) + " (" + build_headline(b) + ")\n";
    o += "GBARecomp: " + unknown_if_empty(b.gbarecomp_sha) + "\n";
    o += "Platform: " + unknown_if_empty(b.platform) + "\n\n### Hardware\n";
    o += "OS: " + unknown_if_empty(s.os) + "\n";
    o += "CPU: " + unknown_if_empty(s.cpu) + "\n";
    o += "GPU: " + unknown_if_empty(s.gpu) + "\n";
    o += "Display refresh: " + refresh_text(s.display_refresh_hz) + "\n";
    o += "Gamepad: " + (s.controller_known ? (s.controller.empty() ? std::string("not connected") : s.controller) : std::string("Unknown")) + "\n\n";
    o += "### Problem area\n" + (in.problem_area.empty() ? std::string("Other") : in.problem_area) + "\n\n";
    o += "### What happened\n<describe the problem>\n\n";
    o += "### Steps to reproduce\n1. \n2. \n3. \n\n";
    o += "### Expected\n...\n\n### Actual\n...\n\n";
    o += "### Components (PASS / FAIL / not tried)\nLauncher: \nMZM gameplay: \nNES: \nAudio: \nEnhancements: \nSave: \n\n";
    o += "### Diagnostics\n";
    o += std::string("Strict: ") + (in.strict_static ? "requested" : "not requested") + "\n";
    o += "Renderer: " + (s.gl_version.empty() ? std::string("Unknown") : "OpenGL " + s.gl_version) + "\n";
    o += "Presentation: " + unknown_if_empty(c.presentation) + "\n";
    o += "Window mode: " + unknown_if_empty(c.window_mode) + "\n";
    o += "CRT: " + unknown_if_empty(c.crt) + "\n";
    o += "ROM validation: " + file_state_name(in.rom) + ", BIOS validation: " + file_state_name(in.bios) + "\n";
    o += "Last session: " + session_status_sentence(in.last_session) + "\n\n";
    o += "### Log\nPlease attach `latest.log` if relevant (use \"Open Log Folder\" in the launcher).\n";
    o += "If useful, attach a screenshot. **Do not upload ROM or BIOS files.**\n";
    return o;
}

std::string security_guidance() {
    return
        "The Windows beta executable may be unsigned and may trigger SmartScreen, antivirus reputation\n"
        "or policy warnings. That is not automatically a false positive.\n\n"
        "Do NOT disable your antivirus and do not add global exclusions.\n\n"
        "If it is blocked, please report: your Windows version; the antivirus / security product; the\n"
        "exact detection name or message; a screenshot; the SHA-256 of the ZIP and of MZMRecomp.exe.\n\n"
        "On a managed work or school computer, test on an authorized personal PC instead of working\n"
        "around its policy.";
}

std::string feedback_guide_text() {
    return
        "What to send when something goes wrong:\n"
        " 1. Press \"Copy Bug Report\", then \"Report Issue on GitHub\" and paste it.\n"
        " 2. Describe what happened and how to reproduce it.\n"
        " 3. Attach latest.log (\"Open Log Folder\"). Look at it first and remove anything private.\n"
        " 4. If useful, attach a screenshot.\n"
        "Do NOT upload your ROM, BIOS or save file. Nothing is sent automatically.";
}

}  // namespace mzm
