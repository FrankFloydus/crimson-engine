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

std::optional<QuaderApiVoidResult>
execute_viewport_shortcut_command(QuaderEditorApi &editor,
                                  const ViewportShortcut &shortcut) {
  const std::optional<std::string> command_target =
      viewport_command_target_id_for_router_shortcut(shortcut);
  if (!command_target) {
    return std::nullopt;
  }
  return editor.commands().execute(*command_target);
}

} // namespace

ViewportShortcutControllerResult
ViewportShortcutController::handle_operation_preview_shortcut(
    const ViewportInputResult &input_result,
    bool operation_preview_active,
    QuaderEditorApi &editor) const {
  if (!input_result.shortcut_handled ||
      !operation_preview_active) {
    return {};
  }

  const std::optional<std::string> command_target =
      viewport_operation_preview_command_target_id_for_router_shortcut(
          input_result.shortcut);
  if (!command_target) {
    return {};
  }

  const QuaderApiVoidResult command =
      editor.commands().execute(*command_target);
  if (!command.ok) {
    LOG_ERROR("Tool preview shortcut failed: {}",
              command.message.empty() ? "Command failed." : command.message);
  }
  return {
      .consumed = true,
      .command_executed = true,
      .invalidate_hover = true,
      .mark_overlay_dirty = true,
  };
}

ViewportShortcutControllerResult
ViewportShortcutController::handle_modeling_shortcut(
    const ViewportInputResult &input_result, QuaderEditorApi &editor) const {
  if (!input_result.shortcut_handled) {
    return {};
  }

  const std::optional<QuaderApiVoidResult> command =
      execute_viewport_shortcut_command(editor, input_result.shortcut);
  if (!command) {
    return {};
  }

  if (!command->ok) {
    LOG_ERROR("Viewport shortcut failed: {}",
              command->message.empty() ? "Command failed." : command->message);
  }
  return {
      .command_executed = true,
      .invalidate_hover = true,
      .mark_overlay_dirty = true,
  };
}

} // namespace quader
