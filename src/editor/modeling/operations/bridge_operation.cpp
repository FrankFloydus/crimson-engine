////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.hpp"

#include <editor/modeling/operations/operation_registry.hpp>

#include <utility>

namespace quader_editor_core {

namespace {

/**
 * Implements the Bridge Operation modeling operation for the modeling operation adapter layer.
 */
class BridgeOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, BridgeOperationSettings settings) const;
	[[nodiscard]] CommandResult execute_edges(EditorSession& session, BridgeOperationSettings settings) const;
	[[nodiscard]] CommandResult execute_faces(EditorSession& session, BridgeOperationSettings settings) const;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, BridgeOperationSettings settings) const;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, BridgeOperationSettings settings) const;
};

BridgeOperationSettings bridge_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *bridge_settings = std::get_if<BridgeOperationSettings>(&settings)) {
		return normalize_bridge_operation_settings(*bridge_settings);
	}
	return normalize_bridge_operation_settings({});
}

const BridgeOperation& concrete_bridge_operation()
{
  static const BridgeOperation kOperation;
  return kOperation;
}

} // namespace

const EditorOperation& bridge_operation()
{
	return concrete_bridge_operation();
}

EditorOperationId BridgeOperation::operation_id() const
{
  return EditorOperationId::Bridge;
}

std::string_view BridgeOperation::id() const
{
	return bridge_operation_schema().operation_id;
}

std::string_view BridgeOperation::label() const
{
	return bridge_operation_schema().label;
}

bool BridgeOperation::supports_preview() const
{
	return true;
}

CommandResult BridgeOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, bridge_settings_from_variant(settings));
}

CommandResult BridgeOperation::begin_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return begin_preview(session, bridge_settings_from_variant(settings));
}

CommandResult BridgeOperation::update_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return update_preview(session, bridge_settings_from_variant(settings));
}

CommandResult BridgeOperation::execute(EditorSession& session, BridgeOperationSettings settings) const
{
	settings = normalize_bridge_operation_settings(settings);
        return session.selection_mode() == SelectionMode::Face
                   ? execute_faces(session, settings)
                   : execute_edges(session, settings);
}

CommandResult BridgeOperation::execute_edges(EditorSession& session, BridgeOperationSettings settings) const
{
	return session.bridge_selected_edges(normalize_bridge_operation_settings(settings).steps);
}

CommandResult BridgeOperation::execute_faces(EditorSession& session, BridgeOperationSettings settings) const
{
	return session.bridge_selected_faces(normalize_bridge_operation_settings(settings).steps);
}

CommandResult BridgeOperation::begin_preview(EditorSession& session, BridgeOperationSettings settings) const
{
	return session.begin_bridge_preview(normalize_bridge_operation_settings(settings).steps);
}

CommandResult BridgeOperation::update_preview(EditorSession& session, BridgeOperationSettings settings) const
{
	return session.update_bridge_preview(normalize_bridge_operation_settings(settings).steps);
}

CommandResult EditorSession::begin_bridge_preview(int steps)
{
  begin_preview_transaction(PreviewTransactionKind::Bridge);
  return finish_preview_begin(update_bridge_preview_from_base(steps));
}

CommandResult EditorSession::begin_bridge_preview(BridgeOperationSettings settings)
{
	return concrete_bridge_operation().begin_preview(*this, settings);
}

CommandResult EditorSession::update_bridge_preview(int steps)
{
  if (!preview_transaction_active(PreviewTransactionKind::Bridge)) {
    return error_result("No bridge preview is active.");
  }
        return update_bridge_preview_from_base(steps);
}

CommandResult EditorSession::update_bridge_preview(BridgeOperationSettings settings)
{
	return concrete_bridge_operation().update_preview(*this, settings);
}

CommandResult EditorSession::begin_bridge_preview_from_working_state(
    SessionState preview_base, SessionState working_base, int steps) {
  begin_preview_transaction(PreviewTransactionKind::Bridge);
  preview_transaction_.base_state = preview_base;
  preview_transaction_.history_state = preview_base;
  return finish_preview_begin(update_bridge_preview_from_working_state(
      std::move(preview_base), std::move(working_base), steps));
}

CommandResult EditorSession::update_bridge_preview_from_working_state(
    SessionState preview_base, SessionState working_base, int steps) {
  if (!preview_transaction_active(PreviewTransactionKind::Bridge)) {
    return error_result("No bridge preview is active.");
  }
  set_state(std::move(working_base));
  const CommandResult result = selection_mode() == SelectionMode::Face
                                   ? bridge_selected_faces(steps)
                                   : bridge_selected_edges(steps);
  return finish_preview_update_from_base(
      preview_base, result, CommandResult{false, false, result.message});
}

CommandResult EditorSession::bridge_selected_edges(int steps)
{
	const CommandResult result = quader_editor_core::bridge_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id, steps);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::bridge_selected_edges(BridgeOperationSettings settings)
{
	return concrete_bridge_operation().execute_edges(*this, settings);
}

CommandResult EditorSession::bridge_selected_faces(int steps)
{
	const CommandResult result = quader_editor_core::bridge_selected_faces(state_.objects, state_.selection_mode, state_.active_object_id, steps);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::bridge_selected_faces(BridgeOperationSettings settings)
{
	return concrete_bridge_operation().execute_faces(*this, settings);
}

CommandResult EditorSession::update_bridge_preview_from_base(int steps)
{
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
        const CommandResult result = selection_mode() == SelectionMode::Face
                                         ? bridge_selected_faces(steps)
                                         : bridge_selected_edges(steps);
        return finish_preview_update_from_base(
		base,
		result,
		CommandResult { false, false, result.message });
}

} // namespace quader_editor_core
