#include "ui/ViewportShortcutController.h"

#include "core/Logger.h"
#include "core/UndoRedoManager.h"
#include "editor/ModelingPreviewCommandService.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"

#include <optional>
#include <string>
#include <string_view>

namespace quader {
namespace {

ModelingMirrorAxis viewportMirrorAxisToModeling(ViewportMirrorAxis axis) {
    switch (axis) {
    case ViewportMirrorAxis::X:
        return ModelingMirrorAxis::X;
    case ViewportMirrorAxis::Y:
        return ModelingMirrorAxis::Y;
    case ViewportMirrorAxis::Z:
        return ModelingMirrorAxis::Z;
    }
    return ModelingMirrorAxis::X;
}

ModelingCommandResult executeCommandTool(
        ModelingSessionService& modeling,
        std::string_view toolId) {
    const CommandTool* tool = findModelingCommandTool(toolId);
    if (tool == nullptr) {
        return {
                .ok = false,
                .changed = false,
                .message = "Command tool is not registered.",
        };
    }
    return tool->execute(modeling);
}

std::optional<ModelingCommandResult> executeViewportModelingShortcut(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::DeleteSelection:
        return executeCommandTool(modeling, "delete_selection");
    case ViewportShortcutKind::SelectAll:
        return executeCommandTool(modeling, "select_all");
    case ViewportShortcutKind::InvertSelection:
        return executeCommandTool(modeling, "invert_selection");
    case ViewportShortcutKind::Copy:
        return executeCommandTool(modeling, "copy_selection");
    case ViewportShortcutKind::Paste:
        return executeCommandTool(modeling, "paste_selection");
    case ViewportShortcutKind::Duplicate:
        return executeCommandTool(modeling, "duplicate_meshes");
    case ViewportShortcutKind::Undo:
        if (undoRedo.canUndo()) {
            const UndoRedoResult result = undoRedo.undo();
            return ModelingCommandResult{
                    .ok = result.ok,
                    .changed = result.changed,
                    .message = result.message,
            };
        }
        if (modeling.canUndo()) {
            return modeling.undo();
        }
        return ModelingCommandResult{.ok = false, .message = "No command to undo"};
    case ViewportShortcutKind::Redo:
        if (undoRedo.canRedo()) {
            const UndoRedoResult result = undoRedo.redo();
            return ModelingCommandResult{
                    .ok = result.ok,
                    .changed = result.changed,
                    .message = result.message,
            };
        }
        if (modeling.canRedo()) {
            return modeling.redo();
        }
        return ModelingCommandResult{.ok = false, .message = "No command to redo"};
    case ViewportShortcutKind::RepeatLastAction:
        return executeCommandTool(modeling, "repeat_last_action");
    case ViewportShortcutKind::MergeActive:
        return executeCommandTool(modeling, "merge_vertices_to_active");
    case ViewportShortcutKind::MergeCenter:
        return executeCommandTool(modeling, "merge_vertices_to_center");
    case ViewportShortcutKind::MirrorAxis:
        return executeCommandTool(modeling, modelingMirrorSelectionToolId(viewportMirrorAxisToModeling(shortcut.mirrorAxis)));
    case ViewportShortcutKind::FlipMeshNormals:
        return executeCommandTool(modeling, "invert_mesh");
    case ViewportShortcutKind::FlipHorizontal:
        return executeCommandTool(modeling, "flip_horizontal");
    case ViewportShortcutKind::FlipVertical:
        return executeCommandTool(modeling, "flip_vertical");
    case ViewportShortcutKind::ApplyMaterial:
        return executeCommandTool(modeling, "assign_material");
    case ViewportShortcutKind::None:
    case ViewportShortcutKind::ActivateTool:
    case ViewportShortcutKind::SetSelectionMode:
    case ViewportShortcutKind::FocusDocument:
    case ViewportShortcutKind::FocusOrigin:
    case ViewportShortcutKind::ToggleMaximized:
    case ViewportShortcutKind::SetGridPreset:
    case ViewportShortcutKind::CancelActiveTool:
    case ViewportShortcutKind::DeletePopup:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

ViewportShortcutControllerResult ViewportShortcutController::handleOperationPreviewShortcut(
        const ViewportInputResult& inputResult,
        const ModelingToolSettingsTarget& toolSettingsTarget,
        ViewportInputRouter& inputRouter,
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling) const {
    if (!inputResult.shortcutHandled || !toolSettingsTarget.operationPreviewActive) {
        return {};
    }

    if (inputResult.shortcut.kind == ViewportShortcutKind::ToggleMaximized) {
        inputRouter.setMaximized(!inputRouter.state().maximized);
        const ModelingCommandResult command = ModelingPreviewCommandService::commit(undoRedo, modeling, "Apply Tool Preview");
        if (!command.ok) {
            LOG_ERROR("Tool preview apply failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        return {
                .consumed = true,
                .commandExecuted = true,
                .invalidateHover = true,
                .markOverlayDirty = true,
        };
    }

    if (inputResult.shortcut.kind == ViewportShortcutKind::CancelActiveTool) {
        const ModelingCommandResult command = ModelingPreviewCommandService::cancel(modeling);
        if (!command.ok) {
            LOG_ERROR("Tool preview cancel failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        return {
                .consumed = true,
                .commandExecuted = true,
                .invalidateHover = true,
                .markOverlayDirty = true,
        };
    }

    return {};
}

ViewportShortcutControllerResult ViewportShortcutController::handleModelingShortcut(
        const ViewportInputResult& inputResult,
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling) const {
    if (!inputResult.shortcutHandled) {
        return {};
    }

    const std::optional<ModelingCommandResult> command =
            executeViewportModelingShortcut(undoRedo, modeling, inputResult.shortcut);
    if (!command) {
        return {};
    }

    if (!command->ok) {
        LOG_ERROR("Viewport shortcut failed: {}", command->message.empty() ? "Command failed." : command->message);
    }
    return {
            .commandExecuted = true,
            .invalidateHover = true,
            .markOverlayDirty = true,
    };
}

} // namespace quader
