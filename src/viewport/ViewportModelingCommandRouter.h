#pragma once

#include "core/UndoRedoManager.h"
#include "editor/ModelingHistoryCommand.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/tools/ViewportToolCatalog.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace quader {

inline ModelingMirrorAxis viewportMirrorAxisToModeling(ViewportMirrorAxis axis) {
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

inline ModelingTransformTool viewportToolToModelingTransformTool(ViewportTool tool) {
    return viewportToolTransformTool(tool);
}

inline ModelingCommandResult toModelingCommandResult(CommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

class ViewportCommandToolHistoryCommand final : public ModelingHistoryCommand {
public:
    ViewportCommandToolHistoryCommand(ModelingSessionService& modeling, const CommandTool& tool)
        : ModelingHistoryCommand(modeling, std::string(tool.label()))
        , tool_(tool) {}

private:
    ModelingCommandResult executeInitial() override {
        return tool_.execute(modeling());
    }

    const CommandTool& tool_;
};

inline ModelingCommandResult executeTrackedCommandTool(
        UndoRedoManager& undoRedo,
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

    if (tool->historyPolicy().mode != ModelingCommandHistoryMode::ModelingUndoRedo) {
        return tool->execute(modeling);
    }

    return toModelingCommandResult(
            undoRedo.execute(std::make_unique<ViewportCommandToolHistoryCommand>(modeling, *tool)));
}

inline ModelingCommandResult executeCommandTool(
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

inline std::optional<ModelingCommandResult> executeViewportModelingShortcut(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::DeleteSelection:
        return executeTrackedCommandTool(undoRedo, modeling, "delete_selection");
    case ViewportShortcutKind::SelectAll:
        return executeCommandTool(modeling, "select_all");
    case ViewportShortcutKind::InvertSelection:
        return executeCommandTool(modeling, "invert_selection");
    case ViewportShortcutKind::Copy:
        return executeCommandTool(modeling, "copy_selection");
    case ViewportShortcutKind::Paste:
        return executeTrackedCommandTool(undoRedo, modeling, "paste_selection");
    case ViewportShortcutKind::Duplicate:
        return executeTrackedCommandTool(undoRedo, modeling, "duplicate_meshes");
    case ViewportShortcutKind::Undo:
        if (undoRedo.canUndo()) {
            return toModelingCommandResult(undoRedo.undo());
        }
        if (modeling.canUndo()) {
            return modeling.undo();
        }
        return ModelingCommandResult{.ok = false, .message = "No command to undo"};
    case ViewportShortcutKind::Redo:
        if (undoRedo.canRedo()) {
            return toModelingCommandResult(undoRedo.redo());
        }
        if (modeling.canRedo()) {
            return modeling.redo();
        }
        return ModelingCommandResult{.ok = false, .message = "No command to redo"};
    case ViewportShortcutKind::RepeatLastAction:
        return executeTrackedCommandTool(undoRedo, modeling, "repeat_last_action");
    case ViewportShortcutKind::MergeActive:
        return executeTrackedCommandTool(undoRedo, modeling, "merge_vertices_to_active");
    case ViewportShortcutKind::MergeCenter:
        return executeTrackedCommandTool(undoRedo, modeling, "merge_vertices_to_center");
    case ViewportShortcutKind::MirrorAxis:
        return executeTrackedCommandTool(
                undoRedo,
                modeling,
                modelingMirrorSelectionToolId(viewportMirrorAxisToModeling(shortcut.mirrorAxis)));
    case ViewportShortcutKind::FlipHorizontal:
        return executeTrackedCommandTool(undoRedo, modeling, "flip_horizontal");
    case ViewportShortcutKind::FlipVertical:
        return executeTrackedCommandTool(undoRedo, modeling, "flip_vertical");
    case ViewportShortcutKind::ApplyMaterial:
        return executeTrackedCommandTool(undoRedo, modeling, "assign_material");
    case ViewportShortcutKind::None:
    case ViewportShortcutKind::ActivateTool:
    case ViewportShortcutKind::SetSelectionMode:
    case ViewportShortcutKind::FocusDocument:
    case ViewportShortcutKind::FocusOrigin:
    case ViewportShortcutKind::ToggleMaximized:
    case ViewportShortcutKind::SetGridPreset:
    case ViewportShortcutKind::CancelActiveTool:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace quader
