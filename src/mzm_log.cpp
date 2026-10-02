#include "mzm_log.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iomanip>

#ifndef MZM_BUILD_SHA
#define MZM_BUILD_SHA "unknown"
#endif
#ifndef MZM_GBARECOMP_SHA
#define MZM_GBARECOMP_SHA "unknown"
#endif
#ifndef MZM_RELEASE_LABEL
#define MZM_RELEASE_LABEL "dev"
#endif
#ifdef _WIN32
#define MZM_PLATFORM "windows-x86_64"
#elif defined(__linux__)
#define MZM_PLATFORM "linux-x86_64"
#else
#define MZM_PLATFORM "unknown"
#endif

namespace mzm {

namespace {
std::filesystem::path compute_log_dir() {
    namespace fs = std::filesystem;
    fs::path root;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) root = local;
    else if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) root = appdata;
#endif
    if (root.empty()) {
        if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg) root = xdg;
        else if (const char* home = std::getenv("HOME"); home && *home) root = fs::path(home) / ".local/state";
        else return {};
    }
    return root / "MZMRecompiled/logs";
}

// Raw descriptor of latest.log for the crash marker (async-signal-safe write).
volatile int g_log_fd = -1;

void write_crash_marker(const char* text) {
    if (g_log_fd < 0) return;
#ifdef _WIN32
    _write(g_log_fd, text, static_cast<unsigned>(std::strlen(text)));
#else
    const ssize_t ignored = ::write(g_log_fd, text, std::strlen(text));
    (void)ignored;
#endif
}

void print_banner() {
    std::printf("MZMRecompiled release=\"%s\" version=%s build=%s gbarecomp=%s os=%s\n",
                MZM_RELEASE_LABEL, MZM_VERSION, MZM_BUILD_SHA, MZM_GBARECOMP_SHA,
                MZM_PLATFORM);
    std::fflush(stdout);
}

#ifdef _WIN32
LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
    char text[160];
    std::snprintf(text, sizeof(text),
                  "\nMZMRecompiled CRASH: exception=0x%08lx address=%p (unhandled)\n",
                  static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                  info->ExceptionRecord->ExceptionAddress);
    write_crash_marker(text);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
void crash_handler(int sig) {
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT"
                     : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL"
                     : sig == SIGBUS ? "SIGBUS" : "signal";
    write_crash_marker("\nMZMRecompiled CRASH: fatal signal ");
    write_crash_marker(name);
    write_crash_marker(" (see lines above for the last runtime output)\n");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

int g_tee_read_fd = -1;

// Let the copy thread finish before the process exits so the last lines
// ("closed result=...") reach latest.log.
void drain_tee() {
    std::fflush(stdout);
    std::fflush(stderr);
    for (int i = 0; i < 200; ++i) {
        int pending = 0;
        if (::ioctl(g_tee_read_fd, FIONREAD, &pending) != 0 || pending == 0) break;
        ::usleep(5000);
    }
    ::usleep(5000);
}

struct TeePipe { int read_fd; int terminal_fd; int log_fd; };

void* tee_loop(void* arg) {
    auto* t = static_cast<TeePipe*>(arg);
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(t->read_fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (int fd : {t->terminal_fd, t->log_fd}) {
            const char* p = buf;
            ssize_t left = n;
            while (left > 0) {
                const ssize_t w = ::write(fd, p, static_cast<size_t>(left));
                if (w <= 0) break;
                p += w;
                left -= w;
            }
        }
    }
    return nullptr;
}
#endif

void install_crash_marker() {
#if defined(MZM_PLATFORM_UWP)
    // Under UWP, SetUnhandledExceptionFilter is restricted. Exception handling
    // is managed by the WinRT application lifecycle / SEH.
#elif defined(_WIN32)
    SetUnhandledExceptionFilter(crash_filter);
#else
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) std::signal(sig, crash_handler);
#endif
}
}  // namespace

std::filesystem::path log_directory() { return compute_log_dir(); }

namespace {
SessionStatus g_previous_status = SessionStatus::Unknown;

// Called before latest.log is truncated: classify the old run and keep its log.
void capture_previous_session(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    const fs::path latest = dir / "latest.log";
    std::error_code ec;
    if (!fs::is_regular_file(latest, ec)) return;
    // Classify from the tail only (the end markers are the last lines): bounded work at startup.
    std::ifstream in(latest, std::ios::binary | std::ios::ate);
    const std::streamoff size = in.tellg();
    const std::streamoff start = size > 65536 ? size - 65536 : 0;
    in.seekg(start);
    std::string text(static_cast<size_t>(size - start), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    g_previous_status = session_status_from_log(text);
    fs::copy_file(latest, dir / "previous.log", fs::copy_options::overwrite_existing, ec);
    if (g_previous_status == SessionStatus::Crash || g_previous_status == SessionStatus::Unexpected)
        fs::copy_file(latest, dir / "last-crash.log", fs::copy_options::overwrite_existing, ec);
}
}  // namespace

SessionStatus previous_session_status() { return g_previous_status; }

void redirect_console_to_log() {
    namespace fs = std::filesystem;
    const fs::path dir = compute_log_dir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    capture_previous_session(dir);
    const fs::path file = dir / "latest.log";
#ifdef _WIN32
    if (!std::freopen(file.string().c_str(), "w", stdout)) return;
    if (_dup2(_fileno(stdout), _fileno(stderr)) != 0) return;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    g_log_fd = _fileno(stdout);
#else
    const int log_fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log_fd < 0) return;
    int pipefd[2];
    if (::pipe(pipefd) != 0) { ::close(log_fd); return; }
    auto* tee = new TeePipe{pipefd[0], ::dup(STDOUT_FILENO), log_fd};
    // stderr keeps going to the original stderr stream only through the tee too,
    // so ordering between the two streams is preserved in latest.log.
    if (tee->terminal_fd < 0 || ::dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        ::dup2(pipefd[1], STDERR_FILENO) < 0) {
        delete tee; ::close(pipefd[0]); ::close(pipefd[1]); ::close(log_fd); return;
    }
    ::close(pipefd[1]);
    pthread_t th;
    if (pthread_create(&th, nullptr, tee_loop, tee) != 0) return;
    pthread_detach(th);
    g_tee_read_fd = pipefd[0];
    std::atexit(drain_tee);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    g_log_fd = log_fd;
#endif
    install_crash_marker();
    print_banner();
}

void log_event(const char* event) {
    namespace fs = std::filesystem;
    const fs::path dir = compute_log_dir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    std::ofstream out(dir / "mzm-recompiled.log", std::ios::app);
    if (out)
        out << stamp << " release=\"" << MZM_RELEASE_LABEL << "\" version=" << MZM_VERSION
            << " build=" << MZM_BUILD_SHA << " gbarecomp=" << MZM_GBARECOMP_SHA
            << " os=" << MZM_PLATFORM << " " << event << '\n';
    // Mirror into latest.log (captured stdout) so one file tells the whole run.
    std::printf("[mzm] %s %s\n", stamp, event);
    std::fflush(stdout);
}

}  // namespace mzm
