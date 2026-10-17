#pragma once

#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>
#include <algorithm>

namespace cydaw::v2::ui {

// Hide controls anchored to the right end of a panel tab bar.
inline bool panelHideTab(const char* label, const char* tip)
{
    auto* bar = ImGui::GetCurrentTabBar();
    if (bar == nullptr)
        return false;
    const auto& p = theme::palette();
    const auto savedCursor = ImGui::GetCursorScreenPos();
    const auto width = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const auto right = bar->BarRect.Max.x;
    ImGui::SetCursorScreenPos(ImVec2(std::max(bar->BarRect.Min.x, right - width), bar->BarRect.Min.y));
    ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::alpha(p.accent, 0.18f));
    const auto pressed = ImGui::Button(label, ImVec2(width, bar->BarRect.GetHeight()));
    tooltip(tip);
    ImGui::PopStyleColor(2);
    ImGui::SetCursorScreenPos(savedCursor);
    ImGui::Dummy(ImVec2(0, 0)); // Submit after restoring the dock host cursor (ImGui layout contract).
    // For ordinary tool tabs this is called before layout: leave room for the button.
    if (bar->WantLayout)
        bar->BarRect.Max.x = std::max(bar->BarRect.Min.x, right - width - dp(4.0f));
    return pressed;
}

// Shared by the Studio dock tabs and the DJ/Foundry tool tabs.
inline bool bottomPanelHideTab()
{
    return panelHideTab(L("Masquer  v###hideBottomPanel", "Hide  v###hideBottomPanel"),
                        L("Masquer le panneau du bas (Ctrl+J)", "Hide the bottom panel (Ctrl+J)"));
}

inline float bottomPanelRestoreHeight()
{
    return ImGui::GetFrameHeight() + dp(8.0f);
}

// A slim, persistent handle, so a hidden panel can be reopened where it was.
inline bool bottomPanelRestoreBar(float width = 0.0f)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(8.0f), dp(4.0f)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::palette().bg1);
    const auto opened = ImGui::BeginChild("##bottomPanelRestore", ImVec2(width, bottomPanelRestoreHeight()), 0,
                                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    bool pressed = false;
    if (opened) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", L("Panneau du bas", "Bottom panel"));
        const auto* label = L("Afficher  ^", "Show  ^");
        const auto buttonWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - dp(8.0f) - buttonWidth));
        pressed = primaryButton(label);
        tooltip(L("Réafficher le panneau du bas (Ctrl+J)", "Show the bottom panel again (Ctrl+J)"));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return pressed;
}

} // namespace cydaw::v2::ui
