#include "GameViewInteraction.h"
#include <imgui_internal.h>

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
bool gameMetricsToggleRequested(bool playing) {
    const auto& io=ImGui::GetIO();
    return playing&&!io.AppFocusLost&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&
        !io.WantTextInput&&!ImGui::GetCurrentContext()->PlatformImeData.WantTextInput&&
        !io.KeyCtrl&&!io.KeyShift&&!io.KeyAlt&&!io.KeySuper&&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)&&!ImGui::GetDragDropPayload()&&
        ImGui::IsKeyPressed(ImGuiKey_F1,false);
}
int gameAttributeDebugCycleRequested(bool enabled) {
    const auto& io=ImGui::GetIO();const auto& context=*ImGui::GetCurrentContext();
    const auto image=ImGui::GetID("##GameViewportImage");
    if(!enabled || io.AppFocusLost || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
       io.WantTextInput || context.PlatformImeData.WantTextInput ||
       (ImGui::IsAnyItemActive() && context.ActiveId!=image) ||
       io.KeyCtrl || io.KeyAlt || io.KeySuper ||
       ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload())return 0;
    const bool forward=ImGui::Shortcut(ImGuiKey_Tab,ImGuiInputFlags_RouteFocused);
    const bool backward=ImGui::Shortcut(ImGuiMod_Shift|ImGuiKey_Tab,ImGuiInputFlags_RouteFocused);
    return forward?1:backward?-1:0;
}
}
