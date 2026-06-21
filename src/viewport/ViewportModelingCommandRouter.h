#pragma once

#include "core/UndoRedoManager.h"
#include "modeling/ModelingSessionService.h"
#include "viewport/ViewportInputRouter.h"

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
    switch (tool) {
    case ViewportTool::Move:
        return ModelingTransformTool::Move;
    case ViewportTool::Rotate:
        return ModelingTransformTool::Rotate;
    case ViewportTool::Scale:
        return ModelingTransformTool::Scale;
    case ViewportTool::Select:
    case ViewportTool::Box:
    case ViewportTool::InsertEdgeLoop:
    case ViewportTool::Knife:
    case ViewportTool::Cut:
    case ViewportTool::Poly:
    case ViewportTool::Mirror:
        return ModelingTransformTool::Move;
    }
    return ModelingTransformTool::Move;
}

inline UndoRedoResult toUndoRedoResult(ModelingCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

inline ModelingCommandResult toModelingCommandResult(UndoRedoResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

template <typename Apply>
inline ModelingCommandResult executeTrackedModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string label,
        Apply apply) {
    return toModelingCommandResult(undoRedo.executeExternal(
            std::move(label),
            [&]() {
                return toUndoRedoResult(apply());
            },
            [&]() {
                return toUndoRedoResult(modeling.undo());
            },
            [&]() {
                return toUndoRedoResult(modeling.redo());
            }));
}

inline std::optional<ModelingCommandResult> executeViewportModelingShortcut(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::DeleteSelection:
        return executeTrackedModelingCommand(undoRedo, modeling, "Delete Selection", [&]() {
            return modeling.deleteSelection();
        });
    case ViewportShortcutKind::SelectAll:
        return modeling.selectAll();
    case ViewportShortcutKind::InvertSelection:
        return modeling.invertSelection();
    case ViewportShortcutKind::Copy:
        return modeling.copySelection();
    case ViewportShortcutKind::Paste:
        return executeTrackedModelingCommand(undoRedo, modeling, "Paste Selection", [&]() {
            return modeling.pasteSelection();
        });
    case ViewportShortcutKind::Duplicate:
        return executeTrackedModelingCommand(undoRedo, modeling, "Duplicate Meshes", [&]() {
            return modeling.executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
        });
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
        return executeTrackedModelingCommand(undoRedo, modeling, "Repeat Last Action", [&]() {
            return modeling.repeatLastAction();
        });
    case ViewportShortcutKind::MergeActive:
        return executeTrackedModelingCommand(undoRedo, modeling, "Merge To Active", [&]() {
            return modeling.executeInstantOperation(ModelingInstantOperationId::MergeVerticesToActive);
        });
    case ViewportShortcutKind::MergeCenter:
        return executeTrackedModelingCommand(undoRedo, modeling, "Merge To Center", [&]() {
            return modeling.executeInstantOperation(ModelingInstantOperationId::MergeVerticesToCenter);
        });
    case ViewportShortcutKind::MirrorAxis:
        return executeTrackedModelingCommand(undoRedo, modeling, "Mirror Selection", [&]() {
            return modeling.mirrorSelection(viewportMirrorAxisToModeling(shortcut.mirrorAxis));
        });
    case ViewportShortcutKind::FlipHorizontal:
        return executeTrackedModelingCommand(undoRedo, modeling, "Flip Horizontal", [&]() {
            return modeling.flipSelection(ModelingFlipOrientation::Horizontal);
        });
    case ViewportShortcutKind::FlipVertical:
        return executeTrackedModelingCommand(undoRedo, modeling, "Flip Vertical", [&]() {
            return modeling.flipSelection(ModelingFlipOrientation::Vertical);
        });
    case ViewportShortcutKind::ApplyMaterial:
        return executeTrackedModelingCommand(undoRedo, modeling, "Apply Material", [&]() {
            return modeling.applyActiveMaterial();
        });
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
