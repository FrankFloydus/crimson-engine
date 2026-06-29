////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_shortcut_controller.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <optional>

namespace quader {
namespace {

std::optional<QuaderApiVoidResult> executeViewportShortcutCommand(
        QuaderEditorApi& editor,
        const ViewportShortcut& shortcut) {
    const std::optional<std::string> commandTarget = viewportCommandTargetIdForRouterShortcut(shortcut);
    if (!commandTarget) {
        return std::nullopt;
    }
    return editor.commands().execute(*commandTarget);
}

} // namespace

ViewportShortcutControllerResult ViewportShortcutController::handleOperationPreviewShortcut(
        const ViewportInputResult& inputResult,
        const ModelingToolSettingsTarget& toolSettingsTarget,
        QuaderEditorApi& editor) const {
    if (!inputResult.shortcutHandled || !toolSettingsTarget.operationPreviewActive) {
        return {};
    }

    const std::optional<std::string> commandTarget =
            viewportOperationPreviewCommandTargetIdForRouterShortcut(inputResult.shortcut);
    if (!commandTarget) {
        return {};
    }

    const QuaderApiVoidResult command = editor.commands().execute(*commandTarget);
    if (!command.ok) {
        LOG_ERROR("Tool preview shortcut failed: {}", command.message.empty() ? "Command failed." : command.message);
    }
    return {
            .consumed = true,
            .commandExecuted = true,
            .invalidateHover = true,
            .markOverlayDirty = true,
    };
}

ViewportShortcutControllerResult ViewportShortcutController::handleModelingShortcut(
        const ViewportInputResult& inputResult,
        QuaderEditorApi& editor) const {
    if (!inputResult.shortcutHandled) {
        return {};
    }

    const std::optional<QuaderApiVoidResult> command =
            executeViewportShortcutCommand(editor, inputResult.shortcut);
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
