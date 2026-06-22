#pragma once

#include "core/UndoRedoManager.h"
#include "editor/ModelingHistoryCommand.h"
#include "editor/ModelingCommandExecutionService.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/tools/ViewportToolCatalog.h"

#include <optional>
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

inline ModelingCommandResult executeTrackedCommandTool(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string_view toolId) {
    ModelingCommandExecutionService commands;
    return commands.execute(undoRedo, modeling, toolId);
}

inline ModelingCommandResult executeCommandTool(
        ModelingSessionService& modeling,
        std::string_view toolId) {
    ModelingCommandExecutionService commands;
    return commands.executeUntracked(modeling, toolId);
}

inline ModelingCommandResult undoViewportModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling) {
    return undoModelingCommand(undoRedo, modeling);
}

inline ModelingCommandResult redoViewportModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling) {
    return redoModelingCommand(undoRedo, modeling);
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
        return undoViewportModelingCommand(undoRedo, modeling);
    case ViewportShortcutKind::Redo:
        return redoViewportModelingCommand(undoRedo, modeling);
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
    case ViewportShortcutKind::FlipMeshNormals:
        return executeTrackedCommandTool(undoRedo, modeling, "invert_mesh");
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
    case ViewportShortcutKind::DeletePopup:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace quader
