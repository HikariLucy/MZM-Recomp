#include "game_launcher_boot.h"
#include "launcher_state.h"
#include "mzm_log.h"
#include "launcher_files.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
void file_row(const char* name, const char* action, PathBuffer& path,
              const char* const* patterns, const std::string& error) {
    ImGui::TextUnformatted(name);
    ImGui::PushID(name);
    ImGui::SetNextItemWidth(-170);
    ImGui::InputText("##path", path.data(), path.size());
    ImGui::SameLine();
    if (ImGui::Button(action, ImVec2(160, 0))) pick(name, patterns, path);
    if (error.empty()) ImGui::TextColored(ImVec4(.32f, .9f, .76f, 1), "VALIDATED");
    else ImGui::TextColored(ImVec4(1, .48f, .38f, 1), "%s", error.c_str());
    ImGui::PopID();
}
void style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 12; s.FrameRounding = 5;
    s.WindowPadding = ImVec2(24, 22); s.FramePadding = ImVec2(12, 9);
    s.ItemSpacing = ImVec2(11, 14);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(.035f, .07f, .11f, 1);
    s.Colors[ImGuiCol_Text] = ImVec4(.88f, .96f, .98f, 1);
    s.Colors[ImGuiCol_Button] = ImVec4(.07f, .35f, .44f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(.12f, .56f, .66f, 1);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(.16f, .68f, .75f, 1);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(.025f, .10f, .14f, 1);
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
    mzm::log_event(page == Page::Home ? "game_data=validated" : "game_data=setup_required");
    std::string message;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[mzm-launcher] SDL init failed: %s\n", SDL_GetError()); return 2;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("MZM Recompiled | Private Beta",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!window || !gl) {
        std::fprintf(stderr, "[mzm-launcher] window failed: %s\n", SDL_GetError());
        if (gl) SDL_GL_DeleteContext(gl);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS); return 2;
    }
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; style();
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    bool running = true, play = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        const ImVec2 vp = ImGui::GetIO().DisplaySize;
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0, 0), vp, IM_COL32(6, 17, 28, 255));
        for (int n = 0; n < 4; ++n)
            bg->AddCircle(ImVec2(vp.x * .78f, vp.y * .32f), 85 + n * 42.0f,
                          IM_COL32(37, 139, 157, 48), 96, 1.5f);
        bg->AddLine(ImVec2(0, vp.y - 90), ImVec2(vp.x, vp.y - 90), IM_COL32(41, 158, 170, 110));
        ImGui::SetNextWindowPos(ImVec2(28, 25));
        ImGui::SetNextWindowSize(ImVec2(vp.x - 56, vp.y - 50));
        ImGui::Begin("MZM", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextColored(ImVec4(.32f, .91f, .84f, 1), "// EXPLORATION SYSTEM");
        ImGui::SetWindowFontScale(1.8f); ImGui::TextUnformatted("MZM RECOMPILED");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("PRIVATE BETA  /  %s  /  USA REVISION 0", MZM_VERSION);
        ImGui::Separator(); ImGui::Spacing();
        if (checked_rom != rom.data()) {
            checked_rom = rom.data(); rom_error = mzm::validate_game_file(rom.data());
        }
        if (checked_bios != bios.data()) {
            checked_bios = bios.data(); bios_error = mzm::validate_bios(bios.data());
        }
        const bool valid = rom_error.empty() && bios_error.empty();
        if (page == Page::Setup || page == Page::Data) {
            ImGui::TextColored(ImVec4(.32f, .91f, .84f, 1),
                               page == Page::Setup ? "GAME DATA SETUP" : "GAME DATA");
            ImGui::TextWrapped("Select your own game file and GBA BIOS. Only file locations are saved in your user configuration.");
            file_row("Game file", "Select Game File", rom, kRomPatterns, rom_error);
            file_row("GBA BIOS", "Select GBA BIOS", bios, kBiosPatterns, bios_error);
            if (ImGui::Button("Revalidate", ImVec2(150, 42))) {
                rom_error = mzm::validate_game_file(rom.data());
                bios_error = mzm::validate_bios(bios.data());
                message = rom_error.empty() && bios_error.empty()
                    ? "Both files validated." : "Check the selected files above.";
                mzm::log_event(message == "Both files validated."
                    ? "game_data=validated" : "game_data=invalid");
            }
            ImGui::SameLine(); ImGui::BeginDisabled(!valid);
            if (ImGui::Button(page == Page::Setup ? "Continue" : "Save Game Data", ImVec2(170, 42))) {
                state.rom = rom.data(); state.bios = bios.data();
                if (state.save(state_file)) { page = Page::Home; message.clear(); }
                else { message = "Could not save launcher settings."; mzm::log_event("launcher_error=config_save"); }
            }
            ImGui::EndDisabled();
        } else if (page == Page::Home) {
            ImGui::TextColored(ImVec4(.32f, .91f, .84f, 1), "SYSTEM READY");
            auto save = fs::path(rom.data()); save.replace_extension(".sav");
            if (fs::is_regular_file(save)) ImGui::Text("Save found: %s", save.filename().string().c_str());
            else ImGui::TextDisabled("No existing save detected");
            ImGui::Spacing(); ImGui::BeginDisabled(!valid);
            if (ImGui::Button("PLAY", ImVec2(230, 70))) { play = true; running = false; }
            ImGui::EndDisabled();
            if (!valid) ImGui::TextColored(ImVec4(1, .48f, .38f, 1), "Game data needs revalidation.");
            if (ImGui::Button("Settings", ImVec2(170, 42))) page = Page::Settings;
            ImGui::SameLine(); if (ImGui::Button("Game Data", ImVec2(170, 42))) page = Page::Data;
            ImGui::SameLine(); if (ImGui::Button("About", ImVec2(170, 42))) page = Page::About;
        } else if (page == Page::Settings) {
            ImGui::TextColored(ImVec4(.32f, .91f, .84f, 1), "SETTINGS");
            ImGui::TextUnformatted("Video"); ImGui::TextDisabled("Runtime controls available during gameplay.");
            ImGui::TextUnformatted("Audio"); ImGui::TextDisabled("Runtime controls available during gameplay.");
            ImGui::TextUnformatted("Controls"); ImGui::TextDisabled("Launcher editor unavailable in this beta.");
            if (ImGui::Button("Game Data", ImVec2(170, 38))) page = Page::Data;
            ImGui::TextUnformatted("Advanced"); ImGui::TextDisabled("No launcher options available in this beta.");
        } else {
            ImGui::TextColored(ImVec4(.32f, .91f, .84f, 1), "ABOUT");
            ImGui::Text("MZM Recompiled  /  Private Beta  /  %s", MZM_VERSION);
            ImGui::TextWrapped("An independent native recompilation project. Not affiliated with Nintendo. Game and BIOS files are supplied by the player.");
            if (ImGui::Button("Open Logs Folder", ImVec2(190, 38))) {
                fs::create_directories(mzm::user_log_dir(), ec);
                const std::string url = "file://" + mzm::user_log_dir().string();
                if (SDL_OpenURL(url.c_str()) != 0) message = "Could not open logs folder.";
            }
        }
        if (page != Page::Home && page != Page::Setup) {
            if (ImGui::Button("Back to Home", ImVec2(170, 38))) {
                if (page == Page::Data) { copy_path(rom, state.rom); copy_path(bios, state.bios); }
                page = Page::Home;
            }
        }
        if (!message.empty()) ImGui::TextColored(ImVec4(1, .73f, .42f, 1), "%s", message.c_str());
        ImGui::End(); ImGui::Render();
        int width, height; SDL_GL_GetDrawableSize(window, &width, &height);
        glViewport(0, 0, width, height); glClearColor(.02f, .05f, .08f, 1);
        glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
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
