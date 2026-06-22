#include "ui/ViewportContextMenuPresenter.h"

#include "api/QuaderEditorApi.h"
#include "core/LocalizationService.h"
#include "core/Logger.h"
#include "modeling/ModelingActionCatalog.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/SelectionModeAdapter.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportModelingCommandRouter.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

bool isGenericContextAction(std::string_view actionId) {
    return actionId == "delete_selection" || actionId == "duplicate_meshes";
}

bool shouldCloseOperationsPopupForMouseClick() {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return true;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }
    return !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

} // namespace

void ViewportContextMenuPresenter::drawOperationsPopup(
        QuaderEditorApi& editor,
        ModelingSessionService& modeling,
        ViewportInputRouter& inputRouter,
        LocalizationService& localization,
        bool modalToolOwnsViewport) const {
    if (!ImGui::BeginPopup("ViewportOperationsContext")) {
        return;
    }

    if (shouldCloseOperationsPopupForMouseClick()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const ModelingActionQuery query{
            .surface = ModelingActionSurface::ViewportContextMenu,
            .modalToolOwnsViewport = modalToolOwnsViewport,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const std::vector<ModelingActionItem> actions = modeling.contextActions(query);
    bool separatedGenericActions = false;
    for (const ModelingActionItem& action : actions) {
        if (!separatedGenericActions && isGenericContextAction(action.actionId)) {
            ImGui::Separator();
            separatedGenericActions = true;
        }
        const bool enabled = action.visibility == ModelingActionVisibility::Enabled;
        std::string labelKey = "modeling.action.";
        labelKey.append(action.actionId);
        labelKey.append(".label");
        std::string stableId = "modeling.action.";
        stableId.append(action.actionId);
        const std::string label = localization.imguiLabel(labelKey, stableId, action.label);
        if (ImGui::MenuItem(label.c_str(), nullptr, false, enabled)) {
            const QuaderApiVoidResult result = editor.modeling().executeAction(action.actionId);
            if (!result.ok) {
                LOG_ERROR("Operation '{}' failed: {}", action.actionId, result.message.empty() ? "Operation failed." : result.message);
            }
            inputRouter.setSelectionMode(appSelectionModeToViewport(modeling.selectionSummary().mode));
            ImGui::CloseCurrentPopup();
        }
        if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !action.disabledReason.empty()) {
            ImGui::SetTooltip("%s", action.disabledReason.c_str());
        }
    }

    ImGui::EndPopup();
}

} // namespace quader
