#pragma once
#include <imgui.h>

namespace crimson {
struct RagdollPreviewInteraction {
    bool hovered{},active{},blocked{};
};
// A passive Image leaves left-dragging to ImGui's window mover. Keep one
// mouse-owning item for both bone dragging and middle-button navigation.
inline RagdollPreviewInteraction drawRagdollPreviewImage(ImTextureRef texture,const ImVec2& size,bool imageReady=true) {
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##RagdollPreviewImage",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    RagdollPreviewInteraction result;
    result.hovered=ImGui::IsItemHovered();result.active=ImGui::IsItemActive();
    if(imageReady)ImGui::GetWindowDrawList()->AddImage(texture,origin,{origin.x+size.x,origin.y+size.y});
    const auto& io=ImGui::GetIO();
    // The preview's own active item is allowed, including a click that has just
    // deactivated a text field whose WantTextInput still describes this frame.
    result.blocked=(!result.active&&(io.WantTextInput||ImGui::IsAnyItemActive()))||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)||ImGui::GetDragDropPayload()!=nullptr||
        io.AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    return result;
}
}
