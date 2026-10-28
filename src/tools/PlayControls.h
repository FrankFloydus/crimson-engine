#pragma once

#include "editor/PlayShortcuts.h"
#include <imgui.h>

namespace crimson {
struct PlayControlsContext {
    bool playing{},paused{},blocked{};
};
inline float playButtonWidth() {
    return ImGui::CalcTextSize("Play").x+ImGui::GetStyle().FramePadding.x*2;
}
inline PlayAction drawPlayButton(const PlayControlsContext& context) {
    ImGui::PushID("MainPlayButton");
    ImGui::BeginDisabled(context.playing||context.blocked);
    const auto action=ImGui::Button("Play")?PlayAction::Start:PlayAction::None;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Play (P)");
    ImGui::EndDisabled();ImGui::PopID();return action;
}
inline float gamePlayToolbarWidth(const PlayControlsContext& context) {
    if(!context.playing)return 0;
    const auto button=[](const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;};
    return button(context.paused?"Resume":"Pause")+button("Stop")+button("Step")+ImGui::GetStyle().ItemSpacing.x*2;
}
inline PlayAction drawGamePlayToolbar(const PlayControlsContext& context) {
    if(!context.playing)return PlayAction::None;
    PlayAction action=PlayAction::None;
    ImGui::PushID("GamePlayControls");
    ImGui::BeginDisabled(context.blocked);
    if(ImGui::Button(context.paused?"Resume":"Pause"))action=PlayAction::TogglePause;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip(context.paused?"Resume (F6)":"Pause (F6)");
    ImGui::SameLine();
    if(ImGui::Button("Stop"))action=PlayAction::Stop;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Stop (Shift+Esc)");
    ImGui::SameLine();ImGui::BeginDisabled(!context.paused);
    if(ImGui::Button("Step"))action=PlayAction::Step;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Advance one 1/60 second simulation tick (F7).");
    ImGui::EndDisabled();ImGui::EndDisabled();ImGui::PopID();return action;
}
}
