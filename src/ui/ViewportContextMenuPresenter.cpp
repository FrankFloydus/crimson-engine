#include "ui/ViewportContextMenuPresenter.h"

#include "core/LocalizationService.h"
#include "core/Logger.h"
#include "modeling/ModelingActionCatalog.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportModelingCommandRouter.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

ViewportSelectionMode toViewportSelectionMode(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return ViewportSelectionMode::Vertex;
    case ModelingSelectionMode::Edge:
        return ViewportSelectionMode::Edge;
    case ModelingSelectionMode::Face:
        return ViewportSelectionMode::Face;
    case ModelingSelectionMode::Object:
        return ViewportSelectionMode::Object;
    case ModelingSelectionMode::Group:
        return ViewportSelectionMode::Group;
    }
    return ViewportSelectionMode::Object;
}

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
        UndoRedoManager& undoRedo,
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
            const CommandTool* commandTool = findModelingCommandTool(action.actionId);
            const ModelingCommandResult result = commandTool != nullptr ?
                    (commandTool->previewKind() != ModelingActionPreviewKind::None ?
                                    commandTool->execute(modeling) :
                                    executeTrackedCommandTool(undoRedo, modeling, commandTool->id())) :
                    ModelingCommandResult{
                            .ok = false,
                            .changed = false,
                            .message = "No command tool registered for context action.",
                    };
            if (!result.ok) {
                LOG_ERROR("Operation '{}' failed: {}", action.actionId, result.message.empty() ? "Operation failed." : result.message);
            }
            inputRouter.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
            ImGui::CloseCurrentPopup();
        }
        if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !action.disabledReason.empty()) {
            ImGui::SetTooltip("%s", action.disabledReason.c_str());
        }
    }

    ImGui::EndPopup();
}

} // namespace quader
