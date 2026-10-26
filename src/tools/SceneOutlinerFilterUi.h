#pragma once
#include "scene/SceneOutlinerRows.h"
#include <imgui.h>
#include <algorithm>

namespace crimson {
struct SceneOutlinerFilterInteraction {bool changed{},activated{};};
// Continue the existing Add menu bar so the query remains reachable while the
// hierarchy scrolls. These controls edit only transient Outliner projection.
inline SceneOutlinerFilterInteraction drawSceneOutlinerFilterControls(char* search,std::size_t capacity,SceneOutlinerFilter& filter,bool enabled=true) {
    SceneOutlinerFilterInteraction result;
    if(!ImGui::BeginMenuBar())return result;
    ImGui::PushID("Outliner filters");ImGui::BeginDisabled(!enabled);
    const float available=std::max(0.f,ImGui::GetWindowWidth()-ImGui::GetCursorPosX()-ImGui::GetStyle().WindowPadding.x);
    const float gap=ImGui::GetStyle().ItemSpacing.x,typeWidth=std::min(120.f,available*.43f);
    ImGui::SetNextItemWidth(std::max(24.f,available-typeWidth-gap));
    result.changed=ImGui::InputTextWithHint("##Search","Search",search,capacity);
    result.activated=ImGui::IsItemActivated();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Search entity names. Ancestors remain visible.");
    ImGui::SameLine(0,gap);ImGui::SetNextItemWidth(std::max(24.f,typeWidth));
    const bool open=ImGui::BeginCombo("##Type",sceneOutlinerFilterName(filter));
    result.activated|=ImGui::IsItemActivated();
    if(open) {
        for(const auto value:SceneOutlinerFilters) {
            const bool selected=value==filter;
            if(ImGui::Selectable(sceneOutlinerFilterName(value),selected)) {result.changed|=value!=filter;filter=value;}
            if(selected)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();ImGui::PopID();ImGui::EndMenuBar();return result;
}
// The filtered tree has its own ID namespace in the caller. Even clicking its
// arrows cannot hide a matching descendant or change normal expansion state.
inline bool sceneOutlinerTreeNode(const char* id,ImGuiTreeNodeFlags flags,const char* label,bool filtered) {
    if(!filtered)return ImGui::TreeNodeEx(id,flags,"%s",label);
    const bool leaf=(flags&ImGuiTreeNodeFlags_Leaf)!=0;
    ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    ImGui::TreeNodeEx(id,flags|ImGuiTreeNodeFlags_NoTreePushOnOpen,"%s",label);
    if(!leaf)ImGui::TreePush(id);
    return true;
}
}
