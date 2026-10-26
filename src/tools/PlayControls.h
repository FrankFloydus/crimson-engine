#pragma once

#include "editor/PlayShortcuts.h"
#include <imgui.h>

namespace crimson {
struct PlayControlsContext {
    bool playing{},paused{},blocked{},hasPlayer{};
};
inline PlayAction drawPlayMenu(const PlayControlsContext& context) {
    PlayAction action=PlayAction::None;
    if(ImGui::MenuItem("Play","P",false,!context.playing&&!context.blocked))action=PlayAction::Start;
    if(ImGui::MenuItem("Play From Here","Shift+F5",false,!context.playing&&!context.blocked&&context.hasPlayer))action=PlayAction::StartHere;
    ImGui::Separator();
    if(ImGui::MenuItem(context.paused?"Resume":"Pause","F6",false,context.playing&&!context.blocked))action=PlayAction::TogglePause;
    if(ImGui::MenuItem("Step","F7",false,context.playing&&context.paused&&!context.blocked))action=PlayAction::Step;
    if(ImGui::MenuItem("Stop","Shift+Esc",false,context.playing&&!context.blocked))action=PlayAction::Stop;
    return action;
}
inline float playToolbarWidth(const PlayControlsContext& context) {
    const auto button=[](const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;};
    return context.playing?button(context.paused?"Resume":"Pause")+button("Step")+button("Stop")+ImGui::GetStyle().ItemSpacing.x*2:button("Play");
}
inline PlayAction drawPlayToolbar(const PlayControlsContext& context) {
    PlayAction action=PlayAction::None;
    ImGui::BeginDisabled(context.blocked);
    if(context.playing) {
        if(ImGui::Button(context.paused?"Resume":"Pause"))action=PlayAction::TogglePause;
        ImGui::SameLine();ImGui::BeginDisabled(!context.paused);
        if(ImGui::Button("Step"))action=PlayAction::Step;
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Advance one 1/60 second simulation tick.");
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Stop"))action=PlayAction::Stop;
    } else if(ImGui::Button("Play"))action=PlayAction::Start;
    ImGui::EndDisabled();return action;
}
}
