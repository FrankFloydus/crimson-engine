////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ViewportShortcutController.h"

#include "api/QuaderApiTypeConversions.h"
#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "editor/modeling/services/SelectionModeAdapter.h"
#include "viewport/HostViewportContractAdapter.h"

#include <optional>
#include <string>

namespace quader {
namespace {

QuaderApiVoidResult applySelectionModeShortcut(
        QuaderEditorApi& editor,
        ViewportSelectionMode mode) {
    return editor.selection().setMode(toApiSelectionMode(viewportSelectionModeToApp(mode)));
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
        QuaderEditorApi& editor) const {
    if (!inputResult.shortcutHandled) {
        return {};
    }

    if (inputResult.shortcut.kind == ViewportShortcutKind::SetSelectionMode) {
        const QuaderApiVoidResult command = applySelectionModeShortcut(editor, inputResult.shortcut.selectionMode);
        if (!command.ok) {
            LOG_ERROR("Viewport selection mode shortcut failed: {}",
                    command.message.empty() ? "Command failed." : command.message);
        }
        return {
                .commandExecuted = true,
                .invalidateHover = true,
                .markOverlayDirty = true,
        };
    }

    const std::optional<std::string> commandId =
            viewportCommandTargetIdForRouterShortcut(inputResult.shortcut);
    if (commandId.has_value()) {
        const QuaderApiVoidResult command = editor.commands().execute(*commandId);
        if (!command.ok) {
            LOG_ERROR("Viewport shortcut command failed ({}): {}",
                    *commandId,
                    command.message.empty() ? "Command failed." : command.message);
        }
        return {
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
    return handleModelingShortcut(inputResult, editor);
}

} // namespace quader
