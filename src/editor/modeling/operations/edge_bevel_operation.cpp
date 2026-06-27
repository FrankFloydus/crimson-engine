////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.hpp"

#include <editor/modeling/operations/operation_registry.hpp>

namespace quader_editor_core {

namespace {

/**
 * Implements the Edge Bevel Operation modeling operation for the modeling operation adapter layer.
 */
class EdgeBevelOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, EdgeBevelOperationSettings settings) const;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, EdgeBevelOperationSettings settings) const;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, EdgeBevelOperationSettings settings) const;
};

EdgeBevelOperationSettings edge_bevel_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *edge_bevel_settings = std::get_if<EdgeBevelOperationSettings>(&settings)) {
		return normalize_edge_bevel_operation_settings(*edge_bevel_settings);
	}
	return normalize_edge_bevel_operation_settings({});
}

const EdgeBevelOperation& concrete_edge_bevel_operation()
{
	static const EdgeBevelOperation operation;
	return operation;
}

} // namespace

const EditorOperation& edge_bevel_operation()
{
	return concrete_edge_bevel_operation();
}

EditorOperationId EdgeBevelOperation::operation_id() const
{
	return EditorOperationId::edge_bevel;
}

std::string_view EdgeBevelOperation::id() const
{
	return edge_bevel_operation_schema().operation_id;
}

std::string_view EdgeBevelOperation::label() const
{
	return edge_bevel_operation_schema().label;
}

bool EdgeBevelOperation::supports_preview() const
{
	return true;
}

CommandResult EdgeBevelOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, edge_bevel_settings_from_variant(settings));
}

CommandResult EdgeBevelOperation::begin_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return begin_preview(session, edge_bevel_settings_from_variant(settings));
}

CommandResult EdgeBevelOperation::update_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return update_preview(session, edge_bevel_settings_from_variant(settings));
}

CommandResult EdgeBevelOperation::execute(EditorSession& session, EdgeBevelOperationSettings settings) const
{
	return session.bevel_selected_edges(settings);
}

CommandResult EdgeBevelOperation::begin_preview(EditorSession& session, EdgeBevelOperationSettings settings) const
{
	return session.begin_edge_bevel_preview(settings);
}

CommandResult EdgeBevelOperation::update_preview(EditorSession& session, EdgeBevelOperationSettings settings) const
{
	return session.update_edge_bevel_preview(settings);
}

} // namespace quader_editor_core
