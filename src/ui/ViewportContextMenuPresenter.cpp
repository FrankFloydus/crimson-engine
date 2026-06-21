#include "ui/ViewportContextMenuPresenter.h"

#include "core/Logger.h"
#include "modeling/ModelingActionCatalog.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportModelingCommandRouter.h"

#include <imgui.h>

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
    }
    return ViewportSelectionMode::Object;
}

} // namespace

void ViewportContextMenuPresenter::drawOperationsPopup(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        ViewportInputRouter& inputRouter,
        bool modalToolOwnsViewport) const {
    if (!ImGui::BeginPopup("ViewportOperationsContext")) {
        return;
    }

    const ModelingActionQuery query{
            .surface = ModelingActionSurface::ViewportContextMenu,
            .modalToolOwnsViewport = modalToolOwnsViewport,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const std::vector<ModelingActionItem> actions = modeling.contextActions(query);
    for (const ModelingActionItem& action : actions) {
        const bool enabled = action.visibility == ModelingActionVisibility::Enabled;
        if (ImGui::MenuItem(action.label.c_str(), nullptr, false, enabled)) {
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
        }
        if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !action.disabledReason.empty()) {
            ImGui::SetTooltip("%s", action.disabledReason.c_str());
        }
    }

    ImGui::EndPopup();
}

} // namespace quader
