#include "SceneShortcuts.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>

namespace crimson {
SceneShortcutRequests readSceneShortcuts(const SceneShortcutContext& context) {
    const auto& io=ImGui::GetIO();
    if (context.actionPending || context.preferencesKeyboard || io.WantTextInput || ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) || context.navigationActive ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
        ImGui::GetDragDropPayload()!=nullptr) return {};

    SceneShortcutRequests requests;
    requests.toggleAssetBrowser=io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space,false);
    requests.save=io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S,false);
    requests.saveAs=requests.save && io.KeyShift;
    const auto* navWindow=ImGui::GetCurrentContext()->NavWindow;
    const auto* rootWindow=navWindow ? navWindow->RootWindow : nullptr;
    const bool entityFocus=rootWindow && (std::string_view(rootWindow->Name)=="Viewport" ||
        std::string_view(rootWindow->Name)=="Outliner" || std::string_view(rootWindow->Name)=="Inspector");
    if ((!entityFocus && !context.particleDocumentFocus) || context.transactionActive || context.modelDropPending) return requests;

    const bool controlZ=io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z,false);
    requests.undo=controlZ && !io.KeyShift;
    requests.redo=(controlZ && io.KeyShift) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y,false));
    // A particle document owns its own history; entity operations remain scene-only.
    if (!entityFocus || context.particleDocumentFocus) return requests;
    requests.duplicate=io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D,false);
    requests.eraseSelection=ImGui::IsKeyPressed(ImGuiKey_Delete,false);
    requests.focusSelection=ImGui::IsKeyPressed(ImGuiKey_F,false);
    return requests;
}
} // namespace crimson
