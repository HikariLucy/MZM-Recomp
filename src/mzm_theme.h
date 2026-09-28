#pragma once

#include "imgui.h"

namespace mzm::theme {

inline constexpr ImVec4 background{7 / 255.f, 11 / 255.f, 18 / 255.f, 1};
inline constexpr ImVec4 surface{13 / 255.f, 20 / 255.f, 31 / 255.f, 1};
inline constexpr ImVec4 elevated{18 / 255.f, 29 / 255.f, 42 / 255.f, 1};
inline constexpr ImVec4 energy{1, 122 / 255.f, 26 / 255.f, 1};
inline constexpr ImVec4 energy_hot{226 / 255.f, 69 / 255.f, 47 / 255.f, 1};
inline constexpr ImVec4 cool{55 / 255.f, 183 / 255.f, 200 / 255.f, 1};
inline constexpr ImVec4 text{231 / 255.f, 237 / 255.f, 243 / 255.f, 1};
inline constexpr ImVec4 muted{151 / 255.f, 166 / 255.f, 182 / 255.f, 1};
inline constexpr ImVec4 success{76 / 255.f, 195 / 255.f, 138 / 255.f, 1};
inline constexpr ImVec4 warning{244 / 255.f, 185 / 255.f, 66 / 255.f, 1};
inline constexpr ImVec4 error{229 / 255.f, 87 / 255.f, 87 / 255.f, 1};
inline constexpr float spacing = 12;
inline constexpr float radius = 9;

inline void apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = radius;
    s.ChildRounding = radius;
    s.FrameRounding = 6;
    s.WindowPadding = ImVec2(20, 17);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, spacing);
    s.Colors[ImGuiCol_WindowBg] = surface;
    s.Colors[ImGuiCol_ChildBg] = elevated;
    s.Colors[ImGuiCol_Text] = text;
    s.Colors[ImGuiCol_TextDisabled] = muted;
    s.Colors[ImGuiCol_Button] = ImVec4(.09f, .18f, .25f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(.14f, .30f, .38f, 1);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(.18f, .38f, .45f, 1);
    s.Colors[ImGuiCol_FrameBg] = background;
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.07f, .14f, .20f, 1);
    s.Colors[ImGuiCol_Header] = elevated;
    s.Colors[ImGuiCol_Border] = ImVec4(.14f, .28f, .34f, 1);
    s.Colors[ImGuiCol_Separator] = ImVec4(.14f, .28f, .34f, 1);
    s.Colors[ImGuiCol_NavHighlight] = energy;
    s.Colors[ImGuiCol_CheckMark] = cool;
}

} // namespace mzm::theme
