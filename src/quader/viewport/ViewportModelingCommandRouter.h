#pragma once

#include "quader/modeling/ModelingSessionService.h"
#include "quader/viewport/ViewportInputRouter.h"

#include <optional>

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

inline std::optional<ModelingCommandResult> executeViewportModelingShortcut(
        ModelingSessionService& modeling,
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::DeleteSelection:
        return modeling.deleteSelection();
    case ViewportShortcutKind::SelectAll:
        return modeling.selectAll();
    case ViewportShortcutKind::InvertSelection:
        return modeling.invertSelection();
    case ViewportShortcutKind::Copy:
        return modeling.copySelection();
    case ViewportShortcutKind::Paste:
        return modeling.pasteSelection();
    case ViewportShortcutKind::Duplicate:
        return modeling.executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
    case ViewportShortcutKind::Undo:
        return modeling.undo();
    case ViewportShortcutKind::Redo:
        return modeling.redo();
    case ViewportShortcutKind::RepeatLastAction:
        return modeling.repeatLastAction();
    case ViewportShortcutKind::MergeActive:
        return modeling.executeInstantOperation(ModelingInstantOperationId::MergeVerticesToActive);
    case ViewportShortcutKind::MergeCenter:
        return modeling.executeInstantOperation(ModelingInstantOperationId::MergeVerticesToCenter);
    case ViewportShortcutKind::MirrorAxis:
        return modeling.mirrorSelection(viewportMirrorAxisToModeling(shortcut.mirrorAxis));
    case ViewportShortcutKind::FlipHorizontal:
        return modeling.flipSelection(ModelingFlipOrientation::Horizontal);
    case ViewportShortcutKind::FlipVertical:
        return modeling.flipSelection(ModelingFlipOrientation::Vertical);
    case ViewportShortcutKind::ApplyMaterial:
        return modeling.applyActiveMaterial();
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
