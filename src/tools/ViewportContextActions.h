#pragma once

#include <imgui.h>

namespace crimson {
enum class ViewportContextAction { None,PlayFromHere,Duplicate,Delete,Focus };

inline ViewportContextAction drawViewportContextActions(bool playEnabled,bool objectActionsEnabled) {
    auto action=ViewportContextAction::None;
    ImGui::PushID("SceneContextActions");
    if(ImGui::MenuItem("Play From Here","Shift+F5",false,playEnabled))action=ViewportContextAction::PlayFromHere;
    if(objectActionsEnabled) {
        ImGui::Separator();
        if(ImGui::MenuItem("Duplicate","Ctrl+D"))action=ViewportContextAction::Duplicate;
        if(ImGui::MenuItem("Delete","Del"))action=ViewportContextAction::Delete;
        if(ImGui::MenuItem("Focus","F"))action=ViewportContextAction::Focus;
    }
    ImGui::PopID();
    return action;
}
}
