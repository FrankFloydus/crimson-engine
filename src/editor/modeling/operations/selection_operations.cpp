////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.h"

#include <editor/modeling/operations/operation_registry.h>

namespace quader_editor_core {
namespace {

using SelectionExecuteFn = CommandResult (EditorSession::*)();

/**
 * Implements the Selection Operation modeling operation for the modeling operation adapter layer.
 */
class SelectionOperation final : public EditorOperation {
public:
	constexpr SelectionOperation(
			EditorOperationId operation_id,
			std::string_view id,
			std::string_view label,
			SelectionExecuteFn execute) :
			operation_id_(operation_id),
			id_(id),
			label_(label),
			execute_(execute)
	{}

	[[nodiscard]] EditorOperationId operation_id() const override
	{
		return operation_id_;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return id_;
	}

	[[nodiscard]] std::string_view label() const override
	{
		return label_;
	}

	[[nodiscard]] bool supports_preview() const override
	{
		return false;
	}

	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override
	{
		if (!std::holds_alternative<SelectionOperationSettings>(settings)) {
			return { false, false, "Operation command settings do not match the operation id." };
		}
		return (session.*execute_)();
	}

private:
	EditorOperationId operation_id_;
	std::string_view id_;
	std::string_view label_;
	SelectionExecuteFn execute_;
};

/**
 * Implements the Recalculate Face Normals Operation modeling operation for the modeling operation adapter layer.
 */
class RecalculateFaceNormalsOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override
	{
		return EditorOperationId::recalculate_face_normals;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return "recalculate_face_normals";
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Recalculate Face Normals";
	}

	[[nodiscard]] bool supports_preview() const override
	{
		return false;
	}

	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override
	{
		const auto *recalculate_settings = std::get_if<RecalculateFaceNormalsOperationSettings>(&settings);
		if (recalculate_settings == nullptr) {
			return { false, false, "Operation command settings do not match the operation id." };
		}
		return session.recalculate_selected_face_normals(recalculate_settings->outside);
	}
};

/**
 * Implements the Delete Selection Operation modeling operation for the modeling operation adapter layer.
 */
class DeleteSelectionOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override
	{
		return EditorOperationId::delete_selection;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return "delete_selection";
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Delete Selection";
	}

	[[nodiscard]] bool supports_preview() const override
	{
		return false;
	}

	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override
	{
		if (!std::holds_alternative<SelectionOperationSettings>(settings)) {
			return { false, false, "Operation command settings do not match the operation id." };
		}
		if (!session.has_selection()) {
			return { true, false, "" };
		}
		return session.delete_selected();
	}
};

} // namespace

const EditorOperation& connect_vertices_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::connect_vertices,
			"connect_vertices",
			"Connect Vertices",
			&EditorSession::connect_selected_vertices);
	return operation;
}

const EditorOperation& dissolve_vertices_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::dissolve_vertices,
			"dissolve_vertices",
			"Dissolve Vertices",
			&EditorSession::dissolve_selected_vertices);
	return operation;
}

const EditorOperation& connect_edges_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::connect_edges,
			"connect_edges",
			"Connect Edges",
			&EditorSession::connect_selected_edges);
	return operation;
}

const EditorOperation& split_edges_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::split_edges,
			"split_edges",
			"Split Edges",
			&EditorSession::split_selected_edges);
	return operation;
}

const EditorOperation& dissolve_edges_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::dissolve_edges,
			"dissolve_edges",
			"Dissolve Edges",
			&EditorSession::dissolve_selected_edges);
	return operation;
}

const EditorOperation& merge_edges_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::merge_edges,
			"merge_edges",
			"Merge Edges",
			&EditorSession::merge_selected_edges);
	return operation;
}

const EditorOperation& collapse_edges_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::collapse_edges,
			"collapse_edges",
			"Collapse Edges",
			&EditorSession::collapse_selected_edges);
	return operation;
}

const EditorOperation& fill_hole_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::fill_hole,
			"fill_hole",
			"Fill Hole",
			&EditorSession::fill_hole_from_selected_edges);
	return operation;
}

const EditorOperation& combine_faces_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::combine_faces,
			"combine_faces",
			"Combine Faces",
			&EditorSession::combine_selected_faces);
	return operation;
}

const EditorOperation& collapse_faces_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::collapse_faces,
			"collapse_faces",
			"Collapse Faces",
			&EditorSession::collapse_selected_faces);
	return operation;
}

const EditorOperation& radial_align_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::radial_align,
			"radial_align",
			"Radial Align",
			&EditorSession::radial_align_selected);
	return operation;
}

const EditorOperation& flip_face_normals_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::flip_face_normals,
			"flip_face_normals",
			"Flip Face Normals",
			&EditorSession::flip_selected_face_normals);
	return operation;
}

const EditorOperation& shade_faces_smooth_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::shade_faces_smooth,
			"shade_faces_smooth",
			"Shade Faces Smooth",
			&EditorSession::shade_selected_faces_smooth);
	return operation;
}

const EditorOperation& shade_faces_flat_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::shade_faces_flat,
			"shade_faces_flat",
			"Shade Faces Flat",
			&EditorSession::shade_selected_faces_flat);
	return operation;
}

const EditorOperation& harden_edge_normals_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::harden_edge_normals,
			"harden_edge_normals",
			"Harden Edge Normals",
			&EditorSession::harden_selected_edge_normals);
	return operation;
}

const EditorOperation& soften_edge_normals_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::soften_edge_normals,
			"soften_edge_normals",
			"Soften Edge Normals",
			&EditorSession::soften_selected_edge_normals);
	return operation;
}

const EditorOperation& combine_meshes_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::combine_meshes,
			"combine_meshes",
			"Combine Meshes",
			&EditorSession::combine_selected_meshes);
	return operation;
}

const EditorOperation& extract_faces_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::extract_faces,
			"extract_faces",
			"Extract Faces",
			&EditorSession::extract_selected_faces);
	return operation;
}

const EditorOperation& detach_faces_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::detach_faces,
			"detach_faces",
			"Detach Faces",
			&EditorSession::detach_selected_faces);
	return operation;
}

const EditorOperation& snap_vertices_to_active_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::snap_vertices_to_active,
			"snap_vertices_to_active",
			"Snap Vertices To Active",
			&EditorSession::snap_selected_vertices_to_active);
	return operation;
}

const EditorOperation& snap_edges_to_active_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::snap_edges_to_active,
			"snap_edges_to_active",
			"Snap Edges To Active",
			&EditorSession::snap_selected_edges_to_active);
	return operation;
}

const EditorOperation& merge_vertices_to_active_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::merge_vertices_to_active,
			"merge_vertices_to_active",
			"Merge Vertices To Active",
			&EditorSession::merge_selected_vertices_to_active);
	return operation;
}

const EditorOperation& merge_vertices_to_center_operation()
{
	static const SelectionOperation operation(
			EditorOperationId::merge_vertices_to_center,
			"merge_vertices_to_center",
			"Merge Vertices To Center",
			&EditorSession::merge_selected_vertices_to_center);
	return operation;
}

const EditorOperation& recalculate_face_normals_operation()
{
	static const RecalculateFaceNormalsOperation operation;
	return operation;
}

const EditorOperation& delete_selection_operation()
{
	static const DeleteSelectionOperation operation;
	return operation;
}

} // namespace quader_editor_core
