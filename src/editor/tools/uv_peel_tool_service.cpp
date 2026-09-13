////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/uv_peel_tool_service.hpp"

#include "editor/history/editor_history_service.hpp"
#include "editor/history/uv_peel_history_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

#include <memory>
#include <utility>

namespace quader {
namespace {

CommandResult command_result(ModelingCommandResult result) {
  return {.ok = result.ok,
          .changed = result.changed,
          .message = std::move(result.message)};
}

} // namespace

UvPeelToolService::UvPeelToolService(EditorHistoryService &history,
                                   EditorSelectionService &selection,
                                   ModelingSessionService &modeling)
    : history_(history), selection_(selection), modeling_(modeling) {}

bool UvPeelToolService::preview_valid() const {
  return active_ && modeling_.uv_peel_preview_valid();
}

std::string UvPeelToolService::preview_error() const {
  return active_ ? modeling_.uv_peel_preview_error() : std::string{};
}

ModelingUvPeelOptions UvPeelToolService::options() const {
  return modeling_.uv_peel_settings().options;
}

std::uint64_t UvPeelToolService::tool_settings_revision() const {
  const std::uint64_t source_revision = modeling_.uv_peel_settings_revision();
  const bool valid = preview_valid();
  if (source_revision_ != source_revision || source_active_ != active_ ||
      source_valid_ != valid) {
    source_revision_ = source_revision;
    source_active_ = active_;
    source_valid_ = valid;
    ++settings_revision_;
  }
  return settings_revision_;
}

CommandResult UvPeelToolService::activate() {
  if (active_) {
    return command_unchanged_result();
  }
  CommandResult selected = command_result(
      modeling_.activate_viewport_tool(ModelingViewportTool::Select));
  if (!selected.ok) {
    return selected;
  }
  CommandResult mode =
      command_result(selection_.set_mode(ModelingSelectionMode::Edge));
  mode.changed = mode.changed || selected.changed;
  if (!mode.ok) {
    return mode;
  }
  active_ = true;
  CommandResult preview = refresh();
  preview.changed = true;
  return preview;
}

CommandResult UvPeelToolService::refresh() {
  if (!active_) {
    return command_unchanged_result();
  }
  if (selection_.mode() != ModelingSelectionMode::Edge ||
      modeling_.tool_status().active_tool != ModelingViewportTool::Select) {
    return cancel();
  }
  // The modeling owner gates solving before target enumeration and records the
  // post-publication revisions, so preview writes cannot trigger another solve.
  return command_result(modeling_.refresh_uv_peel_preview());
}

CommandResult UvPeelToolService::set_options(const ModelingUvPeelOptions &options) {
  const CommandResult configured =
      command_result(modeling_.set_uv_peel_options(options));
  if (!configured.ok || !configured.changed || !active_) {
    return configured;
  }
  CommandResult preview = refresh();
  preview.changed = preview.changed || configured.changed;
  return preview;
}

CommandResult UvPeelToolService::reset_settings() {
  return set_options({});
}

CommandResult UvPeelToolService::apply() {
  if (!active_) {
    return command_invalid_result("No UV Peel tool is active.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  if (!preview_valid()) {
    return command_invalid_result(preview_error());
  }
  CommandResult result = history_.execute(
      std::make_unique<CommitUvPeelPreviewHistoryCommand>(modeling_));
  if (result.ok) {
    active_ = false;
    result.changed = true;
  }
  return result;
}

CommandResult UvPeelToolService::cancel() {
  if (!active_) {
    return command_unchanged_result();
  }
  CommandResult result = command_result(modeling_.cancel_uv_peel_preview());
  if (result.ok) {
    active_ = false;
    result.changed = true;
  }
  return result;
}

} // namespace quader
