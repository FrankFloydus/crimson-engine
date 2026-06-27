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
 * Implements the Bevel Vertices Operation modeling operation for the modeling operation adapter layer.
 */
class BevelVerticesOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, BevelVerticesOperationSettings settings) const;
};

BevelVerticesOperationSettings bevel_vertices_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *bevel_settings = std::get_if<BevelVerticesOperationSettings>(&settings)) {
		return *bevel_settings;
	}
	return {};
}

const BevelVerticesOperation& concrete_bevel_vertices_operation()
{
	static const BevelVerticesOperation operation;
	return operation;
}

} // namespace

const EditorOperation& bevel_vertices_operation()
{
	return concrete_bevel_vertices_operation();
}

EditorOperationId BevelVerticesOperation::operation_id() const
{
	return EditorOperationId::bevel_vertices;
}

std::string_view BevelVerticesOperation::id() const
{
	return bevel_vertices_operation_schema().operation_id;
}

std::string_view BevelVerticesOperation::label() const
{
	return bevel_vertices_operation_schema().label;
}

bool BevelVerticesOperation::supports_preview() const
{
	return false;
}

CommandResult BevelVerticesOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, bevel_vertices_settings_from_variant(settings));
}

CommandResult BevelVerticesOperation::execute(EditorSession& session, BevelVerticesOperationSettings settings) const
{
	return session.bevel_selected_vertices(settings);
}

} // namespace quader_editor_core
