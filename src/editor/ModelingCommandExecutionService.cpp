#include "editor/ModelingCommandExecutionService.h"

#include "editor/ModelingHistoryCommand.h"
#include "editor/ModelingPreviewCommandService.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

class DispatchedModelingToolHistoryCommand final : public ModelingHistoryCommand {
public:
    DispatchedModelingToolHistoryCommand(
            ModelingSessionService& modeling,
            const ModelingCommandDispatcher& dispatcher,
            std::string toolId,
            std::string label)
        : ModelingHistoryCommand(modeling, std::move(label))
        , dispatcher_(dispatcher)
        , toolId_(std::move(toolId)) {}

private:
    ModelingCommandResult executeInitial() override {
        return dispatcher_.execute(toolId_, modeling());
    }

    const ModelingCommandDispatcher& dispatcher_;
    std::string toolId_;
};

class ModelingActionHistoryCommand final : public ModelingHistoryCommand {
public:
    ModelingActionHistoryCommand(
            ModelingSessionService& modeling,
            std::string actionId,
            std::string label)
        : ModelingHistoryCommand(modeling, std::move(label))
        , actionId_(std::move(actionId)) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().executeContextAction(actionId_);
    }

    std::string actionId_;
};

ModelingCommandResult fromCommandResult(CommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

bool shouldUseSceneAwareDelete(ModelingSessionService& modeling) {
    const ModelingSelectionSummary summary = modeling.selectionSummary();
    return summary.mode == ModelingSelectionMode::Object ||
            summary.mode == ModelingSelectionMode::Group ||
            modeling.hasSelectedSceneGroupRoot();
}

std::string commandLabel(const CommandTool* tool, std::string_view toolId) {
    if (tool == nullptr || tool->label().empty()) {
        return std::string(toolId);
    }
    return std::string(tool->label());
}

} // namespace

ModelingCommandResult ModelingCommandExecutionService::execute(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string_view toolId) {
    const CommandTool* tool = dispatcher_.find(toolId);
    if (tool == nullptr) {
        return {.ok = false, .message = "Command tool is not registered."};
    }

    if (tool->id() == "delete_selection" && shouldUseSceneAwareDelete(modeling)) {
        return fromCommandResult(undoRedo.execute(modeling.createDeleteSelectionCommand()));
    }

    const ModelingCommandAvailability available = dispatcher_.availability(toolId, modeling);
    if (!available.enabled) {
        return {
                .ok = false,
                .message = available.disabledReason.empty() ? "Command is disabled." : available.disabledReason,
        };
    }

    if (tool->historyPolicy().mode != ModelingCommandHistoryMode::ModelingUndoRedo) {
        return dispatcher_.execute(*tool, modeling);
    }

    return fromCommandResult(undoRedo.execute(std::make_unique<DispatchedModelingToolHistoryCommand>(
            modeling,
            dispatcher_,
            std::string(toolId),
            commandLabel(tool, toolId))));
}

ModelingCommandResult ModelingCommandExecutionService::executeUntracked(
        ModelingSessionService& modeling,
        std::string_view toolId) {
    return dispatcher_.execute(toolId, modeling);
}

ModelingCommandResult ModelingCommandExecutionService::executeAction(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string_view actionId) {
    if (actionId == "delete_selection" && shouldUseSceneAwareDelete(modeling)) {
        return fromCommandResult(undoRedo.execute(modeling.createDeleteSelectionCommand()));
    }

    const std::vector<ModelingActionItem> actions = modeling.contextActions(ModelingActionQuery{
            .surface = ModelingActionSurface::ViewportContextMenu,
            .includeDisabled = true,
            .includeHidden = false,
    });
    const auto found = std::find_if(actions.begin(), actions.end(), [&](const ModelingActionItem& action) {
        return action.actionId == actionId;
    });
    if (found == actions.end()) {
        return {.ok = false, .message = "Action is not available in the current mode."};
    }
    if (found->previewKind != ModelingActionPreviewKind::None) {
        return modeling.executeContextAction(actionId);
    }
    return fromCommandResult(undoRedo.execute(std::make_unique<ModelingActionHistoryCommand>(
            modeling,
            std::string(actionId),
            found->label.empty() ? std::string(actionId) : found->label)));
}

ModelingCommandResult ModelingCommandExecutionService::executeActionUntracked(
        ModelingSessionService& modeling,
        std::string_view actionId) {
    return modeling.executeContextAction(actionId);
}

ModelingCommandResult ModelingCommandExecutionService::commitPreview(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string label) {
    if (label.empty()) {
        label = "Commit Operation Preview";
    }
    return ModelingPreviewCommandService::commit(undoRedo, modeling, std::move(label));
}

ModelingCommandResult ModelingCommandExecutionService::cancelPreview(ModelingSessionService& modeling) {
    return ModelingPreviewCommandService::cancel(modeling);
}

ModelingCommandAvailability ModelingCommandExecutionService::availability(
        ModelingSessionService& modeling,
        std::string_view toolId) const {
    return dispatcher_.availability(toolId, modeling);
}

ModelingCommandHistoryPolicy ModelingCommandExecutionService::historyPolicy(std::string_view toolId) const {
    return dispatcher_.historyPolicy(toolId);
}

ModelingCommandResult executeModelingCommandTool(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string_view toolId) {
    ModelingCommandExecutionService service;
    return service.execute(undoRedo, modeling, toolId);
}

ModelingCommandResult executeModelingCommandToolWithoutHistory(
        ModelingSessionService& modeling,
        std::string_view toolId) {
    ModelingCommandExecutionService service;
    return service.executeUntracked(modeling, toolId);
}

ModelingCommandResult undoModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling) {
    if (undoRedo.canUndo()) {
        return fromCommandResult(undoRedo.undo());
    }
    if (modeling.canUndo()) {
        return modeling.undo();
    }
    return {.ok = false, .changed = false, .message = "No command to undo"};
}

ModelingCommandResult redoModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling) {
    if (undoRedo.canRedo()) {
        return fromCommandResult(undoRedo.redo());
    }
    if (!undoRedo.canUndo() && modeling.canRedo()) {
        return modeling.redo();
    }
    return {.ok = false, .changed = false, .message = "No command to redo"};
}

} // namespace quader
