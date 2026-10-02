#include "mzm_support.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__linux__)
#include <sys/utsname.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#include "launcher_state.h"

namespace fs = std::filesystem;

namespace mzm {
namespace {
fs::path test_dir() {
    const char* d = std::getenv("MZM_SUPPORT_TEST_DIR");
    return d && *d ? fs::path(d) : fs::path();
}

void record(const char* kind, const std::string& what) {
    const fs::path dir = test_dir();
    std::ofstream out(dir / "opener.txt", std::ios::app);
    out << kind << '\t' << what << '\n';
}

std::string cpu_brand() {
#if (defined(__x86_64__) || defined(__i386__)) && !defined(_MSC_VER)
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000000u, &a, &b, &c, &d) || a < 0x80000004u) return {};
    char brand[49] = {};
    for (unsigned i = 0; i < 3; ++i) {
        __get_cpuid(0x80000002u + i, &a, &b, &c, &d);
        std::memcpy(brand + i * 16, &a, 4); std::memcpy(brand + i * 16 + 4, &b, 4);
        std::memcpy(brand + i * 16 + 8, &c, 4); std::memcpy(brand + i * 16 + 12, &d, 4);
    }
    std::string s(brand);
    const size_t first = s.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : s.substr(first);
#else
    return {};
#endif
}

std::string os_description() {
#ifdef _WIN32
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))))
            if (fn(&v) == 0) {
                const bool w11 = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000;
                return std::string("Windows ") + (w11 ? "11" : std::to_string(v.dwMajorVersion)) + " (build " +
                       std::to_string(v.dwBuildNumber) + ")";
            }
    return "Windows";
#elif defined(__linux__)
    std::ifstream rel("/etc/os-release");
    for (std::string line; std::getline(rel, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string v = line.substr(12);
            if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
            utsname u{};
            if (uname(&u) == 0) v += std::string(" (kernel ") + u.release + ")";
            return v;
        }
    utsname u{};
    return uname(&u) == 0 ? std::string(u.sysname) + " " + u.release : std::string("Linux");
#else
    return {};
#endif
}
}  // namespace

std::string home_directory() {
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE"); p && *p) return p;
#endif
    if (const char* p = std::getenv("HOME"); p && *p) return p;
    return {};
}

SystemInfo collect_system_info(SDL_Window* window) {
    SystemInfo s;
    s.os = os_description();
#if defined(__x86_64__) || defined(_M_X64)
    s.arch = "x86_64";
#endif
    s.cpu = cpu_brand();
    if (const GLubyte* r = glGetString(GL_RENDERER)) s.gpu = reinterpret_cast<const char*>(r);
    if (const GLubyte* v = glGetString(GL_VERSION)) s.gl_version = reinterpret_cast<const char*>(v);
    if (window) {
        SDL_DisplayMode mode{};
        const int index = SDL_GetWindowDisplayIndex(window);
        if (index >= 0 && SDL_GetCurrentDisplayMode(index, &mode) == 0) s.display_refresh_hz = mode.refresh_rate;
    }
    // Controller / audio device names: queried once per request through a short-lived subsystem.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
        s.controller_known = true;
        for (int i = 0, n = SDL_NumJoysticks(); i < n; ++i)
            if (SDL_IsGameController(i)) {
                if (const char* name = SDL_GameControllerNameForIndex(i)) { s.controller = name; break; }
            }
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        if (const char* drv = SDL_GetCurrentAudioDriver()) s.audio_driver = drv;
        if (SDL_GetNumAudioDevices(0) > 0)
            if (const char* name = SDL_GetAudioDeviceName(0, 0)) s.audio_device = name;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    return s;
}

bool clipboard_copy(const std::string& text) {
    const bool ok = SDL_SetClipboardText(text.c_str()) == 0;
    if (!test_dir().empty()) {
        char* back = SDL_GetClipboardText();
        std::ofstream out(test_dir() / "clipboard.txt", std::ios::trunc | std::ios::binary);
        out << (back ? back : "");
        if (back) SDL_free(back);
    }
    return ok;
}

bool open_url(const std::string& url) {
    if (!test_dir().empty()) { record("url", url); return true; }
    return SDL_OpenURL(url.c_str()) == 0;
}

bool open_folder(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!test_dir().empty()) { record("folder", dir.string()); return true; }
#if defined(MZM_PLATFORM_UWP)
    return false; // Desktop shell explorer is unavailable in Xbox / UWP sandbox
#elif defined(_WIN32)
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    return SDL_OpenURL(file_url(dir.string()).c_str()) == 0;
#endif
}

bool open_file(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return false;
    if (!test_dir().empty()) { record("file", file.string()); return true; }
#if defined(MZM_PLATFORM_UWP)
    return false; // Desktop document viewers are unavailable in Xbox / UWP sandbox
#elif defined(_WIN32)
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    return SDL_OpenURL(file_url(file.string()).c_str()) == 0;
#endif
}

std::string read_text_file(const fs::path& file, size_t max_bytes) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamoff size = in.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(max_bytes) ? size - static_cast<std::streamoff>(max_bytes) : 0;
    in.seekg(start);
    std::string data(static_cast<size_t>(size - start), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

fs::path config_ini_path(const fs::path& executable) {
#ifdef _WIN32
    (void)executable;
    return user_config_dir() / "config.ini";
#else
    return executable.parent_path() / "config.ini";
#endif
}

}  // namespace mzm
