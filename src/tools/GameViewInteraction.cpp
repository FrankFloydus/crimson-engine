#include "GameViewInteraction.h"

namespace crimson {
GameViewInteraction drawGameViewImage(ImTextureRef texture,const ImVec2& size) {
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##GameViewportImage",size,ImGuiButtonFlags_MouseButtonLeft);
    const bool imageActive=ImGui::IsItemActive();
    GameViewInteraction interaction;
    interaction.hovered=ImGui::IsItemHovered();
    interaction.clicked=interaction.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    interaction.held=imageActive && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImGui::GetWindowDrawList()->AddImage(texture,origin,ImVec2(origin.x+size.x,origin.y+size.y));
    if(interaction.clicked)ImGui::SetWindowFocus();
    // WantTextInput is computed at NewFrame and can still describe the text
    // field that this click just deactivated. The image now owns that gesture.
    interaction.blocked=(!imageActive && (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())) ||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload()!=nullptr;
    return interaction;
}
}
