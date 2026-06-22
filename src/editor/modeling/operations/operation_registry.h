////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/editor_operation.h>

#include <span>
#include <string_view>

namespace quader_editor_core {

/**
 * Represents an Editor Operation Registry Entry value used by the modeling runtime domain.
 */
struct EditorOperationRegistryEntry final {
	EditorOperationId operation_id;
	std::string_view id;
	std::string_view class_name;
	const EditorOperation *operation;
};

/**
 * Owns Operation Registry lookup and registration for the modeling runtime domain.
 */
class OperationRegistry final {
public:
	[[nodiscard]] std::span<const EditorOperationRegistryEntry> entries() const;
	[[nodiscard]] std::span<const EditorOperation* const> operations() const;
	[[nodiscard]] const EditorOperation *find(EditorOperationId operation_id) const;
	[[nodiscard]] const EditorOperation *find(std::string_view id) const;
};

[[nodiscard]] const OperationRegistry& operation_registry();

[[nodiscard]] const EditorOperation& bridge_operation();
[[nodiscard]] const EditorOperation& edge_bevel_operation();
[[nodiscard]] const EditorOperation& thicken_operation();
[[nodiscard]] const EditorOperation& slice_quad_operation();
[[nodiscard]] const EditorOperation& merge_by_distance_operation();
[[nodiscard]] const EditorOperation& bevel_vertices_operation();
[[nodiscard]] const EditorOperation& duplicate_meshes_operation();
[[nodiscard]] const EditorOperation& connect_vertices_operation();
[[nodiscard]] const EditorOperation& dissolve_vertices_operation();
[[nodiscard]] const EditorOperation& connect_edges_operation();
[[nodiscard]] const EditorOperation& split_edges_operation();
[[nodiscard]] const EditorOperation& dissolve_edges_operation();
[[nodiscard]] const EditorOperation& merge_edges_operation();
[[nodiscard]] const EditorOperation& collapse_edges_operation();
[[nodiscard]] const EditorOperation& fill_hole_operation();
[[nodiscard]] const EditorOperation& combine_faces_operation();
[[nodiscard]] const EditorOperation& collapse_faces_operation();
[[nodiscard]] const EditorOperation& radial_align_operation();
[[nodiscard]] const EditorOperation& flip_face_normals_operation();
[[nodiscard]] const EditorOperation& shade_faces_smooth_operation();
[[nodiscard]] const EditorOperation& shade_faces_flat_operation();
[[nodiscard]] const EditorOperation& harden_edge_normals_operation();
[[nodiscard]] const EditorOperation& soften_edge_normals_operation();
[[nodiscard]] const EditorOperation& combine_meshes_operation();
[[nodiscard]] const EditorOperation& extract_faces_operation();
[[nodiscard]] const EditorOperation& detach_faces_operation();
[[nodiscard]] const EditorOperation& snap_vertices_to_active_operation();
[[nodiscard]] const EditorOperation& snap_edges_to_active_operation();
[[nodiscard]] const EditorOperation& merge_vertices_to_active_operation();
[[nodiscard]] const EditorOperation& merge_vertices_to_center_operation();
[[nodiscard]] const EditorOperation& recalculate_face_normals_operation();
[[nodiscard]] const EditorOperation& assign_material_operation();
[[nodiscard]] const EditorOperation& delete_selection_operation();

[[nodiscard]] std::span<const EditorOperation* const> editor_operation_registry();
[[nodiscard]] const EditorOperation *editor_operation_for_id(EditorOperationId operation_id);
[[nodiscard]] const EditorOperation *editor_operation_for_id(std::string_view id);

} // namespace quader_editor_core
