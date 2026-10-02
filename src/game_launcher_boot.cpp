#include "game_launcher_boot.h"
#include "launcher_state.h"
#include "mzm_theme.h"
#include "mzm_log.h"
#include "mzm_support.h"
#include "mzm_diagnostics.h"
#include "launcher_files.h"
#include "launcher_gl.h"
#include "windows_executable_path.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <string>
#ifdef _WIN32
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
namespace {
using PathBuffer = std::array<char, 4096>;
constexpr const char* kRomPatterns[] = {"*.gba"};
constexpr const char* kBiosPatterns[] = {"*.bin"};
void copy_path(PathBuffer& dst, const std::string& value) {
    std::snprintf(dst.data(), dst.size(), "%s", value.c_str());
}
void pick(const char* title, const char* const* patterns, PathBuffer& path) {
    char selected[4096] = {};
    if (launcher_try_pick_file(title, patterns, 1, title, selected, sizeof(selected)) == 1)
        copy_path(path, selected);
}
void heading(const char* title, const char* subtitle, ImFont* bold) {
    if (bold) ImGui::PushFont(bold);
    ImGui::TextColored(mzm::theme::cool, "%s", title);
    if (bold) ImGui::PopFont();
    ImGui::TextColored(mzm::theme::muted, "%s", subtitle);
    ImGui::Separator();
}
void status(const char* label, const std::string& error, const char* path) {
    const bool missing = !path || !*path;
    ImGui::TextColored(missing ? mzm::theme::muted :
                       error.empty() ? mzm::theme::success : mzm::theme::error,
                       "%s: %s", label, missing ? "Not configured" :
                       error.empty() ? "Valid" : "Invalid");
    if (!missing && !error.empty()) ImGui::TextWrapped("%s", error.c_str());
}
void file_row(const char* name, const char* hint, const char* action,
              PathBuffer& path, const char* const* patterns,
              const std::string& error) {
    ImGui::PushID(name);
    ImGui::TextColored(mzm::theme::text, "%s", name);
    if (*hint) ImGui::TextColored(mzm::theme::muted, "%s", hint);
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(90.f, width - 178.f));
    ImGui::InputText("##path", path.data(), path.size());
    if (ImGui::IsItemHovered() && path[0]) ImGui::SetTooltip("%s", path.data());
    ImGui::SameLine();
    if (ImGui::Button(action, ImVec2(164, 0))) pick(name, patterns, path);
    status(name, error, path.data());
    ImGui::PopID();
}
void enhancement_card(const char* title, const char* detail, const char* availability,
                      float width, ImFont* bold) {
    ImGui::PushID(title);
    ImGui::BeginChild("##capability", ImVec2(width, 105.f), true);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    if (bold) ImGui::PushFont(bold);
    ImGui::TextColored(mzm::theme::text, "%s", title);
    if (bold) ImGui::PopFont();
    ImGui::TextWrapped("%s", detail);
    ImGui::TextColored(mzm::theme::success, "%s", availability);
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopID();
}
void background(ImVec2 vp) {
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), vp,
        IM_COL32(7, 11, 18, 255), IM_COL32(10, 19, 29, 255),
        IM_COL32(7, 13, 21, 255), IM_COL32(7, 11, 18, 255));
    for (float x = 0; x < vp.x; x += 48)
        dl->AddLine(ImVec2(x, 0), ImVec2(x, vp.y), IM_COL32(55, 183, 200, 12));
    for (float y = 0; y < vp.y; y += 48)
        dl->AddLine(ImVec2(0, y), ImVec2(vp.x, y), IM_COL32(55, 183, 200, 12));
    for (int i = 0; i < 3; ++i)
        dl->AddCircle(ImVec2(vp.x*.78f, vp.y*.31f), 90.f+i*42.f,
                      IM_COL32(55, 183, 200, 28), 72, 1);
}
bool primary_button(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, mzm::theme::energy);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, .58f, .24f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mzm::theme::energy_hot);
    ImGui::PushStyleColor(ImGuiCol_Text, mzm::theme::background);
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return hit;
}
void set_window_icon(SDL_Window* window, const fs::path& executable) {
    const fs::path icon = executable.parent_path() / "assets/icons/mzm-recompiled.bmp";
    SDL_Surface* surface = SDL_LoadBMP(icon.string().c_str());
    if (surface) { SDL_SetWindowIcon(window, surface); SDL_FreeSurface(surface); }
    else std::fprintf(stderr, "[mzm-launcher] icon unavailable: %s\n", icon.string().c_str());
}
enum class Page { Setup, Home, Enhancements, Settings, Data, About, Support };
}

int game_launcher_preboot(std::vector<std::string>& args,
                          const gbarecomp::RunOptions&) {
#ifdef _WIN32
    const fs::path executable = mzm::executable_path();
    if (executable.empty()) return 2;
#else
    const fs::path executable = fs::absolute(args.front());
#endif
    bool force = false, skip = false, direct = false;
    for (size_t i = 1; i < args.size();) {
        if (args[i] == "--launcher") { force = true; args.erase(args.begin() + i); continue; }
        if (args[i] == "--no-launcher") { skip = true; args.erase(args.begin() + i); continue; }
        if (args[i] == "--rom" || args[i] == "--steps" || args[i] == "--frames" ||
            args[i] == "--tcp" || args[i] == "--no-window" || args[i] == "--load-state" ||
            args[i] == "--dump-bmp" || args[i] == "--dump-png") direct = true;
        ++i;
    }
    if (const char* env = std::getenv("GBARECOMP_NO_LAUNCHER"); env && *env && *env != '0') skip = true;
    if (!force && (skip || direct)) return 0;

    const fs::path state_file = mzm::user_config_dir() / "launcher.ini";
    std::error_code ec;
    fs::create_directories(state_file.parent_path(), ec);
    auto state = mzm::LauncherState::load(state_file);
    PathBuffer rom{}, bios{};
    copy_path(rom, state.rom); copy_path(bios, state.bios);
    std::string rom_error = mzm::validate_game_file(rom.data());
    std::string bios_error = mzm::validate_bios(bios.data());
    std::string checked_rom = rom.data(), checked_bios = bios.data();
    Page page = rom_error.empty() && bios_error.empty() ? Page::Home : Page::Setup;
    // Local visual review hook; it never writes launcher state or bypasses validation.
    if (const char* preview = std::getenv("MZM_LAUNCHER_PREVIEW_PAGE")) {
        if (std::strcmp(preview, "home") == 0) page = Page::Home;
        else if (std::strcmp(preview, "enhancements") == 0) page = Page::Enhancements;
        else if (std::strcmp(preview, "data") == 0) page = Page::Data;
        else if (std::strcmp(preview, "settings") == 0) page = Page::Settings;
        else if (std::strcmp(preview, "about") == 0) page = Page::About;
        else if (std::strcmp(preview, "support") == 0) page = Page::Support;
    }
    mzm::log_event(page == Page::Home ? "game_data=validated" : "game_data=setup_required");
    std::string message;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[mzm-launcher] SDL init failed: %s\n", SDL_GetError()); return 2;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    int window_width = 960, window_height = 640;
    if (const char* requested = std::getenv("MZM_LAUNCHER_WINDOW_SIZE")) {
        int w = 0, h = 0;
        if (std::sscanf(requested, "%dx%d", &w, &h) == 2 && w >= 720 && h >= 480) {
            window_width = w; window_height = h;
        }
    }
    SDL_Window* window = SDL_CreateWindow("MZM Recompiled | " MZM_RELEASE_LABEL,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window) SDL_SetWindowMinimumSize(window, 720, 480);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!window || !gl) {
        std::fprintf(stderr, "[mzm-launcher] window failed: %s\n", SDL_GetError());
        if (gl) SDL_GL_DeleteContext(gl);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS); return 2;
    }
    set_window_icon(window, executable);
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    mzm::theme::apply();
    const fs::path fonts = executable.parent_path() / "assets/fonts";
    ImFont* body = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        (fonts / "LatoLatin-Regular.ttf").string().c_str(), 17.f);
    ImFont* bold = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        (fonts / "LatoLatin-Bold.ttf").string().c_str(), 19.f);
    if (!body) ImGui::GetIO().Fonts->AddFontDefault();
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    const fs::path brand_path = executable.parent_path()
        / "assets/icons/mzm-brand-helm-core.png";
    LauncherTexture helm = launcher_texture_load(brand_path.string().c_str());
    if (!helm.id) std::fprintf(stderr, "[mzm-launcher] brand unavailable: %s\n", brand_path.string().c_str());
    // ---- Support module state. Everything is collected on demand (page open / Refresh),
    // never per frame, and nothing leaves the machine unless the user clicks an action.
    mzm::SystemInfo sysinfo; bool sysinfo_loaded = false;
    mzm::SettingsInfo settings_info;
    std::string tail_text; bool tail_loaded = false, show_tail = false;
    int problem_idx = static_cast<int>(mzm::problem_areas().size()) - 1;
    std::vector<const char*> problem_items;
    for (const auto& a : mzm::problem_areas()) problem_items.push_back(a.c_str());
    const std::string home = mzm::home_directory();
    const fs::path log_dir = mzm::user_log_dir();
    const mzm::SessionStatus last_session = mzm::previous_session_status();
    auto refresh_support = [&]() {
        sysinfo = mzm::collect_system_info(window); sysinfo_loaded = true;
        settings_info = mzm::describe_display_settings(
            mzm::read_text_file(mzm::config_ini_path(executable), 1 << 16));
        tail_loaded = false;
    };
    auto make_input = [&]() {
        mzm::DiagnosticInput in;
        in.build = mzm::current_build_info();
        in.system = sysinfo; in.settings = settings_info;
        auto state_of = [](const PathBuffer& path, const std::string& error) {
            return !path[0] ? mzm::FileState::NotConfigured
                 : error.empty() ? mzm::FileState::Valid : mzm::FileState::Invalid;
        };
        in.rom = state_of(rom, rom_error); in.bios = state_of(bios, bios_error);
        const char* strict = std::getenv("GBARECOMP_STRICT_STATIC");
        in.strict_static = strict && *strict == '1';
        in.config_path = mzm::redact_user_path(mzm::config_ini_path(executable).string(), home);
        in.log_path = mzm::redact_user_path((log_dir / "latest.log").string(), home);
        in.last_session = last_session;
        in.problem_area = mzm::problem_areas()[static_cast<size_t>(problem_idx)];
        return in;
    };
    auto copy_text = [&](const std::string& text, const char* what) {
        message = mzm::clipboard_copy(text) ? std::string(what) + " copied to the clipboard."
                                            : "Could not access the clipboard.";
    };
    bool running = true, play = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        const ImVec2 vp = ImGui::GetIO().DisplaySize;
        background(vp);
        const float margin = vp.x < 800 ? 12.f : 26.f;
        ImGui::SetNextWindowPos(ImVec2(margin, margin));
        ImGui::SetNextWindowSize(ImVec2(vp.x - margin*2, vp.y - margin*2));
        ImGui::Begin("MZM", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        const float content = ImGui::GetContentRegionAvail().x;
        if (bold) ImGui::PushFont(bold);
        ImGui::TextUnformatted("MZM RECOMPILED");
        if (bold) ImGui::PopFont();
        ImGui::SameLine(content > 550 ? content - 73 : 0);
        ImGui::TextColored(mzm::theme::muted, "v%s", MZM_VERSION);
        ImGui::TextColored(mzm::theme::cool, "STATIC RECOMPILATION  /  " MZM_RELEASE_LABEL " PUBLIC RUNTIME TEST");
        ImGui::Separator();
        if (checked_rom != rom.data()) {
            checked_rom = rom.data(); rom_error = mzm::validate_game_file(rom.data());
        }
        if (checked_bios != bios.data()) {
            checked_bios = bios.data(); bios_error = mzm::validate_bios(bios.data());
        }
        const bool valid = rom_error.empty() && bios_error.empty();
        if (page == Page::Setup || page == Page::Data) {
            const bool setup = page == Page::Setup;
            heading(setup ? "INITIAL SYSTEM CONFIGURATION" : "GAME DATA",
                    setup ? "Select your legally obtained game image and your Game Boy Advance BIOS."
                          : vp.x < 800 ? "Game: Metroid: Zero Mission  /  Region: USA  /  Revision: 0"
                                       : "Metroid: Zero Mission  /  USA  /  Revision 0", bold);
            ImGui::BeginChild("##data-panel", ImVec2(0, -65.f), true);
            if (!setup || vp.x < 800) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
            if (!setup && vp.x >= 800) {
                ImGui::Text("Game: Metroid: Zero Mission");
                ImGui::Text("Region: USA     Revision: 0");
                ImGui::Separator();
            }
            file_row("ROM", setup && vp.x >= 800 ? "Select your legally obtained game image" : "",
                     setup ? "Select Game File" : "Change Game File", rom, kRomPatterns, rom_error);
            ImGui::Separator();
            file_row("GBA BIOS", setup && vp.x >= 800 ? "Select your Game Boy Advance BIOS" : "",
                     setup ? "Select BIOS" : "Change BIOS", bios, kBiosPatterns, bios_error);
            if (!setup && ImGui::TreeNode("Advanced details")) {
                ImGui::TextWrapped("ROM SHA-1: 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8");
                ImGui::TextWrapped("BIOS SHA-1: 300c20df6731a33952ded8c436f7f186d25d3492");
                ImGui::TreePop();
            }
            if (!setup || vp.x < 800) ImGui::PopStyleVar();
            ImGui::EndChild();
            if (ImGui::Button("Revalidate", ImVec2(135, 42))) {
                rom_error = mzm::validate_game_file(rom.data());
                bios_error = mzm::validate_bios(bios.data());
                message = rom_error.empty() && bios_error.empty()
                    ? "Both files validated." : "Check the selected files above.";
                mzm::log_event(message == "Both files validated."
                    ? "game_data=validated" : "game_data=invalid");
            }
            ImGui::SameLine(); ImGui::BeginDisabled(!valid);
            if (primary_button(setup ? "CONTINUE" : "SAVE GAME DATA", ImVec2(175, 42))) {
                state.rom = rom.data(); state.bios = bios.data();
                if (state.save(state_file)) { page = Page::Home; message.clear(); }
                else { message = "Could not save launcher settings."; mzm::log_event("launcher_error=config_save"); }
            }
            ImGui::EndDisabled();
            if (!setup) {
                ImGui::SameLine();
                if (ImGui::Button("Back to Home", ImVec2(155, 42))) {
                    copy_path(rom, state.rom); copy_path(bios, state.bios);
                    page = Page::Home;
                }
            }
        } else if (page == Page::Home) {
            const float hero_h = std::max(168.f, ImGui::GetContentRegionAvail().y - 72.f);
            ImGui::BeginChild("##hero", ImVec2(0, hero_h), true);
            const ImVec2 p = ImGui::GetWindowPos();
            const ImVec2 s = ImGui::GetWindowSize();
            const float mark_h = std::min(235.f, hero_h*.46f);
            const float mark_w = helm.h ? mark_h * helm.w / helm.h : 0.f;
            const float hero_offset = std::max(0.f, (hero_h-mark_h-146.f)*.5f);
            if (helm.id) {
                const ImVec2 top(p.x+(s.x-mark_w)*.5f, p.y+hero_offset+8.f);
                ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)helm.id,
                    top, ImVec2(top.x+mark_w, top.y+mark_h));
            }
            ImGui::SetCursorPosY(hero_offset+mark_h+14);
            if (bold) ImGui::PushFont(bold);
            const char* title = "MZM RECOMPILED";
            ImGui::SetCursorPosX((s.x-ImGui::CalcTextSize(title).x)*.5f);
            ImGui::TextUnformatted(title);
            if (bold) ImGui::PopFont();
            const char* caption = valid ? "GAME DATA VERIFIED  /  USA" : "GAME DATA REQUIRES ATTENTION";
            ImGui::SetCursorPosX(std::max(12.f, (s.x-ImGui::CalcTextSize(caption).x)*.5f));
            ImGui::TextColored(valid ? mzm::theme::success : mzm::theme::warning, "%s", caption);
            ImGui::SetCursorPosX(std::max(12.f, (s.x-230.f)*.5f));
            ImGui::BeginDisabled(!valid);
            if (primary_button("PLAY", ImVec2(230, 62))) { play = true; running = false; }
            ImGui::EndDisabled();
            if (hero_h > 360.f) {
                const char* features = "SAVE STATES  ·  REWIND  ·  FAST FORWARD";
                ImGui::SetCursorPosX(std::max(12.f, (s.x-ImGui::CalcTextSize(features).x)*.5f));
                ImGui::TextColored(mzm::theme::muted, "%s", features);
            }
            ImGui::EndChild();
            const float nav = std::min(170.f, (content-40.f)/5.f);
            if (ImGui::Button("Game Data", ImVec2(nav, 42))) page = Page::Data;
            ImGui::SameLine(); if (ImGui::Button("Enhancements", ImVec2(nav, 42))) page = Page::Enhancements;
            ImGui::SameLine(); if (ImGui::Button("Settings", ImVec2(nav, 42))) page = Page::Settings;
            ImGui::SameLine(); if (ImGui::Button("Support", ImVec2(nav, 42))) { page = Page::Support; sysinfo_loaded = false; }
            ImGui::SameLine(); if (ImGui::Button("About", ImVec2(nav, 42))) page = Page::About;
        } else if (page == Page::Enhancements) {
            heading("NATIVE ENHANCEMENTS", "Host runtime capabilities for play and presentation.", bold);
            ImGui::BeginChild("##enhancements", ImVec2(0, -48.f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 7));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
            const float card = (ImGui::GetContentRegionAvail().x - 16.f) / 3.f;
            enhancement_card("SAVE STATES", "9 selectable slots", "AVAILABLE IN-GAME", card, bold);
            ImGui::SameLine(); enhancement_card("REWIND", "Recent play, ~15 sec history", "AVAILABLE IN-GAME", card, bold);
            ImGui::SameLine(); enhancement_card("FAST FORWARD", "4x target by default", "AVAILABLE IN-GAME", card, bold);
            enhancement_card("DISPLAY", "Resizable window + fullscreen", "AVAILABLE IN-GAME", card, bold);
            ImGui::SameLine(); enhancement_card("CONTROLS", "Keyboard / controller bindings", "AVAILABLE", card, bold);
            ImGui::SameLine(); enhancement_card("COLOR MODELS", "Raw + 4 screen simulations", "AVAILABLE", card, bold);
            ImGui::PopStyleVar(2);
            ImGui::EndChild();
        } else if (page == Page::Settings) {
            heading("SETTINGS", "Preferences are managed by the host runtime.", bold);
            ImGui::BeginChild("##settings", ImVec2(0, -48), true);
            ImGui::TextColored(mzm::theme::cool, "IN-GAME MENU");
            ImGui::TextWrapped("Display, audio, save states, rewind, and fast-forward are managed during play.");
            ImGui::Separator();
            ImGui::TextColored(mzm::theme::cool, "HOST CONFIGURATION");
#ifdef _WIN32
            ImGui::TextWrapped("Keyboard and controller bindings load from your MZMRecompiled AppData folder.");
#else
            ImGui::TextWrapped("Keyboard and controller bindings load from keybinds.ini and config.ini beside the executable.");
#endif
            ImGui::TextWrapped("Screen color model is selected at launch via [video].screen, --screen, or GBARECOMP_SCREEN.");
            ImGui::EndChild();
        } else if (page == Page::Support) {
            if (!sysinfo_loaded) refresh_support();
            const mzm::BuildInfo build = mzm::current_build_info();
            heading("SUPPORT", "Found a problem? Help us improve MZM Recompiled.", bold);
            ImGui::BeginChild("##support", ImVec2(0, -72.f), true);
            if (last_session == mzm::SessionStatus::Crash || last_session == mzm::SessionStatus::Unexpected) {
                ImGui::TextColored(mzm::theme::warning, "%s",
                    last_session == mzm::SessionStatus::Crash ? "A previous crash was detected."
                                                              : "The last session may have ended unexpectedly.");
                ImGui::TextWrapped("Its log was kept as previous.log / last-crash.log in the log folder. "
                                   "Please open the log folder and copy a bug report.");
                ImGui::Separator();
            }
            ImGui::TextColored(mzm::theme::cool, "%s  /  %s", mzm::build_headline(build).c_str(), build.platform.c_str());
            ImGui::SameLine(); ImGui::Text("  Build %s  GBARecomp %s", mzm::short_sha(build.mzm_sha).c_str(),
                        mzm::short_sha(build.gbarecomp_sha).c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("Problem area:");
            ImGui::SameLine(); ImGui::SetNextItemWidth(260.f);
            ImGui::Combo("##area", &problem_idx, problem_items.data(), static_cast<int>(problem_items.size()));
            const float bw = std::max(150.f, (ImGui::GetContentRegionAvail().x - 24.f) / 3.f);
            if (primary_button("Report Issue on GitHub", ImVec2(bw, 40))) {
                // Copies the report, then opens the new-issue page only: nothing is filled in or sent.
                const bool copied = mzm::clipboard_copy(mzm::format_bug_report(make_input()));
                const bool opened = mzm::open_url(mzm::issue_url());
                message = opened ? (copied ? "Bug report copied. Paste it into the GitHub issue that just opened."
                                           : "Opened GitHub Issues (clipboard unavailable: use Copy Bug Report).")
                                 : std::string("Could not open the browser. Open ") + mzm::kIssuesNewUrl +
                                   " yourself" + (copied ? " and paste the copied report." : ".");
            }
            ImGui::SameLine(); if (ImGui::Button("Copy Bug Report", ImVec2(bw, 40)))
                copy_text(mzm::format_bug_report(make_input()), "Bug report");
            ImGui::SameLine(); if (ImGui::Button("Copy Diagnostic Info", ImVec2(bw, 40)))
                copy_text(mzm::format_diagnostic(make_input()), "Diagnostic info");
            if (ImGui::Button("Open Log Folder", ImVec2(bw, 36))) {
                if (!mzm::open_folder(log_dir)) message = "Could not open the folder. Path: " + mzm::redact_user_path(log_dir.string(), home);
            }
            ImGui::SameLine(); if (ImGui::Button("Open latest.log", ImVec2(bw, 36)))
                if (!mzm::open_file(log_dir / "latest.log")) message = "Could not open latest.log (no log yet, or no default app).";
            ImGui::SameLine(); if (ImGui::Button("Copy Log Path", ImVec2(bw, 36)))
                copy_text(mzm::redact_user_path((log_dir / "latest.log").string(), home), "Log path");
            if (ImGui::Button("Open Feedback Guide", ImVec2(bw, 36))) {
                if (!mzm::open_file(executable.parent_path() / "FEEDBACK.md")) message = "FEEDBACK.md not found: the instructions are shown below.";
            }
            ImGui::SameLine(); if (ImGui::Button("Copy Build Info", ImVec2(bw, 36)))
                copy_text(mzm::format_build_info(build), "Build info");
            ImGui::SameLine(); if (ImGui::Button("Refresh details", ImVec2(bw, 36))) refresh_support();
            ImGui::Spacing();
            ImGui::TextColored(mzm::theme::success, "No diagnostic data is uploaded automatically.");
            ImGui::TextWrapped("Nothing leaves your computer unless you click Report Issue and paste it yourself. "
                               "If useful, attach a screenshot to the GitHub issue. Never upload your ROM, BIOS or save file.");
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Windows security warning?")) {
                ImGui::TextWrapped("%s", mzm::security_guidance().c_str());
            }
            if (ImGui::CollapsingHeader("What to send")) {
                ImGui::TextWrapped("%s", mzm::feedback_guide_text().c_str());
            }
            if (ImGui::CollapsingHeader("Build and system details")) {
                const std::string text = mzm::format_diagnostic(make_input());
                ImGui::TextWrapped("%s", text.c_str());
            }
            show_tail = ImGui::CollapsingHeader("Recent log (latest.log)");
            if (show_tail) {
                if (ImGui::Button("Refresh log")) tail_loaded = false;
                if (!tail_loaded) {
                    tail_text = mzm::redact_text(mzm::tail_lines(mzm::read_text_file(log_dir / "latest.log", 1 << 16), 15), home);
                    tail_loaded = true;
                }
                ImGui::TextWrapped("%s", tail_text.empty() ? "(no log yet)" : tail_text.c_str());
            }
            ImGui::EndChild();
        } else {
            heading("ABOUT", "MZM Recompiled  /  " MZM_RELEASE_LABEL "  /  Public Runtime Test", bold);
            ImGui::BeginChild("##about", ImVec2(0, -92), true);
            ImGui::Text("%s  (version %s)", MZM_RELEASE_LABEL, MZM_VERSION);
            ImGui::Text("MZM build: %.12s", MZM_BUILD_SHA);
            ImGui::Text("GBARecomp:  %.12s", MZM_GBARECOMP_SHA);
            ImGui::TextWrapped("Include these when you report a problem. The log is "
                               "logs/latest.log (use Open Logs Folder below).");
            ImGui::TextWrapped("Experimental static recompilation project.");
            ImGui::TextWrapped("Independent fan and research project. Not affiliated with or endorsed by Nintendo.");
            ImGui::Separator();
            ImGui::TextColored(mzm::theme::cool, "BUILT WITH");
            ImGui::TextUnformatted("GBARecomp  /  recomp-ui");
            ImGui::EndChild();
            if (ImGui::Button("Open Logs Folder", ImVec2(190, 38)))
                if (!mzm::open_folder(mzm::user_log_dir())) message = "Could not open logs folder.";
            ImGui::SameLine();
            if (ImGui::Button("Support / Report Problem", ImVec2(230, 38))) { page = Page::Support; sysinfo_loaded = false; }
        }
        if (page != Page::Home && page != Page::Setup && page != Page::Data) {
            if (ImGui::Button("Back to Home", ImVec2(170, 38))) {
                page = Page::Home;
            }
        }
        if (!message.empty()) ImGui::TextColored(mzm::theme::warning, "%s", message.c_str());
        ImGui::End(); ImGui::Render();
        int width, height; SDL_GL_GetDrawableSize(window, &width, &height);
        glViewport(0, 0, width, height); glClearColor(.02f, .05f, .08f, 1);
        glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (const char* capture = std::getenv("MZM_LAUNCHER_CAPTURE")) {
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                                SDL_PIXELFORMAT_RGBA32);
            if (shot) {
                std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < height; ++y)
                    std::memcpy(static_cast<unsigned char*>(shot->pixels) + y*shot->pitch,
                                pixels.data() + static_cast<size_t>(height-1-y)*width*4,
                                static_cast<size_t>(width)*4);
                if (SDL_SaveBMP(shot, capture) != 0)
                    std::fprintf(stderr, "[mzm-launcher] capture failed: %s\n", SDL_GetError());
                SDL_FreeSurface(shot);
            }
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }
    launcher_texture_free(&helm);
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    if (!play) return 1;
    mzm::log_event("launch_requested rom=USA-BMXE bios=validated");
    const fs::path config = mzm::resolve_game_config(executable);
    if (!fs::is_regular_file(config)) {
        std::fprintf(stderr, "[mzm-launcher] missing game config: %s\n", config.string().c_str()); return 2;
    }
    args.insert(args.end(), {"--bios", bios.data(), "--rom", rom.data(), "--config", config.string()});
#ifdef _WIN32
    _putenv_s("GBARECOMP_STRICT_STATIC", "1");
#else
    setenv("GBARECOMP_STRICT_STATIC", "1", 1);
#endif
    return 0;
}
