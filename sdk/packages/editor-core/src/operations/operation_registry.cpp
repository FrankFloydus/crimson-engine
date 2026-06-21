#include <quader_editor_core/operation_registry.h>

#include <array>

namespace quader_editor_core {

namespace {

EditorOperationRegistryEntry operation_entry(const EditorOperation& operation, std::string_view class_name)
{
	return {
		operation.operation_id(),
		operation.id(),
		class_name,
		&operation,
	};
}

std::span<const EditorOperationRegistryEntry> operation_registry_entries()
{
	static const std::array<EditorOperationRegistryEntry, 33> entries {{
		operation_entry(bridge_operation(), "BridgeOperation"),
		operation_entry(edge_bevel_operation(), "EdgeBevelOperation"),
		operation_entry(thicken_operation(), "ThickenOperation"),
		operation_entry(slice_quad_operation(), "SliceQuadOperation"),
		operation_entry(merge_by_distance_operation(), "MergeByDistanceOperation"),
		operation_entry(bevel_vertices_operation(), "BevelVerticesOperation"),
		operation_entry(duplicate_meshes_operation(), "DuplicateMeshesOperation"),
		operation_entry(connect_vertices_operation(), "ConnectVerticesOperation"),
		operation_entry(dissolve_vertices_operation(), "DissolveVerticesOperation"),
		operation_entry(connect_edges_operation(), "ConnectEdgesOperation"),
		operation_entry(split_edges_operation(), "SplitEdgesOperation"),
		operation_entry(dissolve_edges_operation(), "DissolveEdgesOperation"),
		operation_entry(merge_edges_operation(), "MergeEdgesOperation"),
		operation_entry(collapse_edges_operation(), "CollapseEdgesOperation"),
		operation_entry(fill_hole_operation(), "FillHoleOperation"),
		operation_entry(combine_faces_operation(), "CombineFacesOperation"),
		operation_entry(collapse_faces_operation(), "CollapseFacesOperation"),
		operation_entry(radial_align_operation(), "RadialAlignOperation"),
		operation_entry(flip_face_normals_operation(), "FlipFaceNormalsOperation"),
		operation_entry(shade_faces_smooth_operation(), "ShadeFacesSmoothOperation"),
		operation_entry(shade_faces_flat_operation(), "ShadeFacesFlatOperation"),
		operation_entry(harden_edge_normals_operation(), "HardenEdgeNormalsOperation"),
		operation_entry(soften_edge_normals_operation(), "SoftenEdgeNormalsOperation"),
		operation_entry(combine_meshes_operation(), "CombineMeshesOperation"),
		operation_entry(extract_faces_operation(), "ExtractFacesOperation"),
		operation_entry(detach_faces_operation(), "DetachFacesOperation"),
		operation_entry(snap_vertices_to_active_operation(), "SnapVerticesToActiveOperation"),
		operation_entry(snap_edges_to_active_operation(), "SnapEdgesToActiveOperation"),
		operation_entry(merge_vertices_to_active_operation(), "MergeVerticesToActiveOperation"),
		operation_entry(merge_vertices_to_center_operation(), "MergeVerticesToCenterOperation"),
		operation_entry(recalculate_face_normals_operation(), "RecalculateFaceNormalsOperation"),
		operation_entry(assign_material_operation(), "AssignMaterialOperation"),
		operation_entry(delete_selection_operation(), "DeleteSelectionOperation"),
	}};
	return entries;
}

std::span<const EditorOperation* const> operation_entries()
{
	static const std::array<const EditorOperation*, 33> operations {
		&bridge_operation(),
		&edge_bevel_operation(),
		&thicken_operation(),
		&slice_quad_operation(),
		&merge_by_distance_operation(),
		&bevel_vertices_operation(),
		&duplicate_meshes_operation(),
		&connect_vertices_operation(),
		&dissolve_vertices_operation(),
		&connect_edges_operation(),
		&split_edges_operation(),
		&dissolve_edges_operation(),
		&merge_edges_operation(),
		&collapse_edges_operation(),
		&fill_hole_operation(),
		&combine_faces_operation(),
		&collapse_faces_operation(),
		&radial_align_operation(),
		&flip_face_normals_operation(),
		&shade_faces_smooth_operation(),
		&shade_faces_flat_operation(),
		&harden_edge_normals_operation(),
		&soften_edge_normals_operation(),
		&combine_meshes_operation(),
		&extract_faces_operation(),
		&detach_faces_operation(),
		&snap_vertices_to_active_operation(),
		&snap_edges_to_active_operation(),
		&merge_vertices_to_active_operation(),
		&merge_vertices_to_center_operation(),
		&recalculate_face_normals_operation(),
		&assign_material_operation(),
		&delete_selection_operation(),
	};
	return operations;
}

} // namespace

std::span<const EditorOperationRegistryEntry> OperationRegistry::entries() const
{
	return operation_registry_entries();
}

std::span<const EditorOperation* const> OperationRegistry::operations() const
{
	return operation_entries();
}

const EditorOperation *OperationRegistry::find(EditorOperationId operation_id) const
{
	for (const EditorOperationRegistryEntry& entry : entries()) {
		if (entry.operation_id == operation_id) {
			return entry.operation;
		}
	}
	return nullptr;
}

const EditorOperation *OperationRegistry::find(std::string_view id) const
{
	for (const EditorOperationRegistryEntry& entry : entries()) {
		if (entry.id == id) {
			return entry.operation;
		}
	}
	return nullptr;
}

const OperationRegistry& operation_registry()
{
	static const OperationRegistry registry;
	return registry;
}

std::span<const EditorOperation* const> editor_operation_registry()
{
	return operation_registry().operations();
}

const EditorOperation *editor_operation_for_id(EditorOperationId operation_id)
{
	return operation_registry().find(operation_id);
}

const EditorOperation *editor_operation_for_id(std::string_view id)
{
	return operation_registry().find(id);
}

} // namespace quader_editor_core
