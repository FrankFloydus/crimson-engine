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
 * Implements the Merge By Distance Operation modeling operation for the modeling operation adapter layer.
 */
class MergeByDistanceOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, MergeByDistanceOperationSettings settings) const;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, MergeByDistanceOperationSettings settings) const;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, MergeByDistanceOperationSettings settings) const;
};

MergeByDistanceOperationSettings merge_by_distance_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *merge_settings = std::get_if<MergeByDistanceOperationSettings>(&settings)) {
		return normalize_merge_by_distance_operation_settings(*merge_settings);
	}
	return normalize_merge_by_distance_operation_settings({});
}

const MergeByDistanceOperation& concrete_merge_by_distance_operation()
{
  static const MergeByDistanceOperation kOperation;
  return kOperation;
}

} // namespace

const EditorOperation& merge_by_distance_operation()
{
	return concrete_merge_by_distance_operation();
}

EditorOperationId MergeByDistanceOperation::operation_id() const
{
  return EditorOperationId::MergeByDistance;
}

std::string_view MergeByDistanceOperation::id() const
{
	return merge_by_distance_operation_schema().operation_id;
}

std::string_view MergeByDistanceOperation::label() const
{
	return merge_by_distance_operation_schema().label;
}

bool MergeByDistanceOperation::supports_preview() const
{
	return true;
}

CommandResult MergeByDistanceOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, merge_by_distance_settings_from_variant(settings));
}

CommandResult MergeByDistanceOperation::begin_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return begin_preview(session, merge_by_distance_settings_from_variant(settings));
}

CommandResult MergeByDistanceOperation::update_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return update_preview(session, merge_by_distance_settings_from_variant(settings));
}

CommandResult MergeByDistanceOperation::execute(EditorSession& session, MergeByDistanceOperationSettings settings) const
{
	return session.merge_selected_vertices_by_distance(settings);
}

CommandResult MergeByDistanceOperation::begin_preview(EditorSession& session, MergeByDistanceOperationSettings settings) const
{
	return session.begin_merge_by_distance_preview(settings);
}

CommandResult MergeByDistanceOperation::update_preview(EditorSession& session, MergeByDistanceOperationSettings settings) const
{
	return session.update_merge_by_distance_preview(settings);
}

} // namespace quader_editor_core
