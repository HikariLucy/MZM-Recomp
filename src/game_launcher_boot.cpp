#include "game_launcher_boot.h"
#include "launcher_state.h"
#include "mzm_theme.h"
#include "mzm_log.h"
#include "launcher_files.h"
#include "launcher_gl.h"
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
enum class Page { Setup, Home, Settings, Data, About };
}

int game_launcher_preboot(std::vector<std::string>& args,
                          const gbarecomp::RunOptions&) {
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
        else if (std::strcmp(preview, "data") == 0) page = Page::Data;
        else if (std::strcmp(preview, "settings") == 0) page = Page::Settings;
        else if (std::strcmp(preview, "about") == 0) page = Page::About;
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
    SDL_Window* window = SDL_CreateWindow("MZM Recompiled | Private Beta",
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
    set_window_icon(window, fs::absolute(args.front()));
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    mzm::theme::apply();
    const fs::path fonts = fs::absolute(args.front()).parent_path() / "assets/fonts";
    ImFont* body = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        (fonts / "LatoLatin-Regular.ttf").string().c_str(), 17.f);
    ImFont* bold = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        (fonts / "LatoLatin-Bold.ttf").string().c_str(), 19.f);
    if (!body) ImGui::GetIO().Fonts->AddFontDefault();
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    const fs::path brand_path = fs::absolute(args.front()).parent_path()
        / "assets/icons/mzm-brand-helm-core.png";
    LauncherTexture helm = launcher_texture_load(brand_path.string().c_str());
    if (!helm.id) std::fprintf(stderr, "[mzm-launcher] brand unavailable: %s\n", brand_path.string().c_str());
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
        ImGui::TextColored(mzm::theme::cool, "STATIC RECOMPILATION  /  PRIVATE BETA");
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
            ImGui::EndChild();
            const float nav = std::min(170.f, (content-20.f)/3.f);
            if (ImGui::Button("Game Data", ImVec2(nav, 42))) page = Page::Data;
            ImGui::SameLine(); if (ImGui::Button("Settings", ImVec2(nav, 42))) page = Page::Settings;
            ImGui::SameLine(); if (ImGui::Button("About", ImVec2(nav, 42))) page = Page::About;
        } else if (page == Page::Settings) {
            heading("SETTINGS", "Runtime options are available during gameplay.", bold);
            ImGui::BeginChild("##settings", ImVec2(0, -48), true);
            for (const auto& section : {"Display", "Audio", "Controls", "Assist", "Advanced"}) {
                ImGui::TextColored(mzm::theme::cool, "%s", section);
                ImGui::SameLine(150);
                ImGui::TextColored(mzm::theme::muted, "%s",
                    std::string(section) == "Advanced" ? "Unavailable in this build" :
                    "Use the in-game runtime menu");
                ImGui::Separator();
            }
            ImGui::EndChild();
        } else {
            heading("ABOUT", "MZM Recompiled  /  Private Beta", bold);
            ImGui::BeginChild("##about", ImVec2(0, -92), true);
            ImGui::Text("Version %s", MZM_VERSION);
            ImGui::TextWrapped("Experimental static recompilation project.");
            ImGui::TextWrapped("Independent fan and research project. Not affiliated with or endorsed by Nintendo.");
            ImGui::Separator();
            ImGui::TextColored(mzm::theme::cool, "BUILT WITH");
            ImGui::TextUnformatted("GBARecomp  /  recomp-ui");
            ImGui::EndChild();
            if (ImGui::Button("Open Logs Folder", ImVec2(190, 38))) {
                fs::create_directories(mzm::user_log_dir(), ec);
                const std::string url = "file://" + mzm::user_log_dir().string();
                if (SDL_OpenURL(url.c_str()) != 0) message = "Could not open logs folder.";
            }
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
    const fs::path config = mzm::resolve_game_config(fs::absolute(args.front()));
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
