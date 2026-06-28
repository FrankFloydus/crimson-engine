////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_shortcut_controller.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"

#include <optional>

namespace quader {
namespace {

QuaderCommandId commandForMirrorAxis(ViewportMirrorAxis axis) {
    switch (axis) {
    case ViewportMirrorAxis::X:
        return QuaderCommand::ModelingMirrorSelectionX;
    case ViewportMirrorAxis::Y:
        return QuaderCommand::ModelingMirrorSelectionY;
    case ViewportMirrorAxis::Z:
        return QuaderCommand::ModelingMirrorSelectionZ;
    }
    return QuaderCommand::ModelingMirrorSelectionX;
}

std::optional<QuaderApiVoidResult> executeViewportModelingShortcut(
        QuaderEditorApi& editor,
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::DeleteSelection:
        return editor.commands().execute(QuaderCommand::DeleteSelection);
    case ViewportShortcutKind::SelectAll:
        return editor.commands().execute(QuaderCommand::EditSelectAll);
    case ViewportShortcutKind::InvertSelection:
        return editor.commands().execute(QuaderCommand::EditInvertSelection);
    case ViewportShortcutKind::Copy:
        return editor.commands().execute(QuaderCommand::CopySelection);
    case ViewportShortcutKind::Paste:
        return editor.commands().execute(QuaderCommand::PasteSelection);
    case ViewportShortcutKind::Duplicate:
        return editor.commands().execute(QuaderCommand::DuplicateSelection);
    case ViewportShortcutKind::Undo:
        return editor.commands().execute(QuaderCommand::EditUndo);
    case ViewportShortcutKind::Redo:
        return editor.commands().execute(QuaderCommand::EditRedo);
    case ViewportShortcutKind::RepeatLastAction:
        return editor.commands().execute(QuaderCommand::EditRepeatLastAction);
    case ViewportShortcutKind::MergeActive:
        return editor.commands().execute(QuaderCommand::ModelingMergeVerticesToActive);
    case ViewportShortcutKind::MergeCenter:
        return editor.commands().execute(QuaderCommand::ModelingMergeVerticesToCenter);
    case ViewportShortcutKind::MirrorAxis:
        return editor.commands().execute(commandForMirrorAxis(shortcut.mirrorAxis));
    case ViewportShortcutKind::FlipMeshNormals:
        return editor.commands().execute(QuaderCommand::ModelingInvertMesh);
    case ViewportShortcutKind::FlipHorizontal:
        return editor.commands().execute(QuaderCommand::ModelingFlipHorizontal);
    case ViewportShortcutKind::FlipVertical:
        return editor.commands().execute(QuaderCommand::ModelingFlipVertical);
    case ViewportShortcutKind::ApplyMaterial:
        return editor.commands().execute(QuaderCommand::ApplyMaterial);
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
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) const {
    (void)modeling;
    if (!inputResult.shortcutHandled || !toolSettingsTarget.operationPreviewActive) {
        return {};
    }

    if (inputResult.shortcut.kind == ViewportShortcutKind::ToggleMaximized) {
        const QuaderApiVoidResult command = editor.modeling().commitPreview("Apply Tool Preview");
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
        const QuaderApiVoidResult command = editor.modeling().cancelPreview();
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
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) const {
    (void)modeling;
    if (!inputResult.shortcutHandled) {
        return {};
    }

    const std::optional<QuaderApiVoidResult> command =
            executeViewportModelingShortcut(editor, inputResult.shortcut);
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
