#pragma once
#include <imgui.h>
#include <imgui_internal.h>

namespace crimson {
inline constexpr ImGuiWindowFlags SceneOutlinerWindowFlags=ImGuiWindowFlags_MenuBar;

template<class Contents> void drawSceneOutlinerAddMenu(bool enabled,Contents&& contents) {
    if(!ImGui::BeginMenuBar())return;
    if(ImGui::BeginMenu("Add",enabled)){contents();ImGui::EndMenu();}
    ImGui::EndMenuBar();
}
inline ImGuiTreeNodeFlags sceneOutlinerRowFlags(bool selected,bool leaf,bool renaming) {
    auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_FramePadding;
    // Highlight and interaction both end beside the label. Inline editing still
    // owns the available row width, including its active text field.
    flags|=renaming?ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_AllowOverlap:ImGuiTreeNodeFlags_SpanLabelWidth;
    if(selected)flags|=ImGuiTreeNodeFlags_Selected;
    if(leaf)flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
    return flags;
}
inline bool sceneOutlinerRowHovered() {
    // Physical current-frame bounds also exclude a dragged source row. ImGui's
    // ordinary hover query suppresses that source and includes a previous-frame
    // result in IsAnyItemHovered(), neither of which identifies empty space.
    return ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
}
inline bool sceneOutlinerBackgroundHovered(bool rowHovered) {
    if(rowHovered||!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))return false;
    const auto& content=ImGui::GetCurrentWindow()->InnerRect;
    return ImGui::IsMouseHoveringRect(content.Min,content.Max,false)&&ImGui::GetCurrentContext()->HoveredId==0;
}
inline bool sceneOutlinerBackgroundClicked(bool enabled,bool rowHovered) {
    return enabled&&sceneOutlinerBackgroundHovered(rowHovered)&&!ImGui::GetDragDropPayload()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}
inline bool beginSceneOutlinerBackgroundMenu(bool enabled,bool rowHovered) {
    if(enabled&&sceneOutlinerBackgroundHovered(rowHovered)&&ImGui::IsMouseReleased(ImGuiMouseButton_Right))ImGui::OpenPopup("Outliner background");
    return ImGui::BeginPopup("Outliner background");
}
inline bool beginSceneOutlinerBackgroundDropTarget(bool rowHovered) {
    if(!sceneOutlinerBackgroundHovered(rowHovered))return false;
    return ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect,ImGui::GetID("##outliner_root_drop"));
}
}
