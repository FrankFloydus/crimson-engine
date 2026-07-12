////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/metadata/host_contract.hpp>

#include <string_view>

namespace qhc = quader_host_contract;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostActionDescriptor& require_action(std::string_view id)
{
	const qhc::HostActionDescriptor *descriptor = qhc::find_action_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

} // namespace

TEST(QdrEditorMetadataActionCatalogTests, HostContractActionCatalogLinksPanelActionsToDescriptorOnlyOperationsOrTools) {
	const std::span<const qhc::HostActionDescriptor> actions = qhc::action_catalog();
	EXPECT_TRUE(actions.size() == 43U);

	for (const qhc::HostActionDescriptor& action : actions) {
		EXPECT_FALSE(action.id.empty());
		EXPECT_FALSE(action.label.empty());

                if (action.kind == qhc::HostActionKind::Operation) {
                  EXPECT_FALSE(action.operation_id.empty());
                  EXPECT_TRUE(action.tool_id.empty());
                  EXPECT_TRUE(qhc::find_operation_descriptor(
                                  action.operation_id) != nullptr);
                } else {
                  EXPECT_TRUE(action.operation_id.empty());
                  EXPECT_FALSE(action.tool_id.empty());
                  EXPECT_TRUE(qhc::find_tool_descriptor(action.tool_id) !=
                              nullptr);
                }
        }
}

TEST(QdrEditorMetadataActionCatalogTests, HostContractActionCatalogPreservesOperationPanelVisibilityAndPreviewKinds) {
  EXPECT_TRUE(require_action("bevel_edges").preview_kind ==
              qhc::HostPreviewKind::EdgeBevel);
  EXPECT_TRUE(require_action("interpolated_bridge_edges").preview_kind ==
              qhc::HostPreviewKind::Bridge);
  EXPECT_TRUE(require_action("interpolated_bridge_faces").preview_kind ==
              qhc::HostPreviewKind::Bridge);
  EXPECT_TRUE(require_action("merge_by_distance").preview_kind ==
              qhc::HostPreviewKind::MergeByDistance);
  EXPECT_TRUE(require_action("slice_quad").preview_kind ==
              qhc::HostPreviewKind::SliceQuad);
  EXPECT_TRUE(require_action("create_outer_hull").preview_kind ==
              qhc::HostPreviewKind::Thicken);
  EXPECT_TRUE(require_action("thicken_faces").preview_kind ==
              qhc::HostPreviewKind::Thicken);
  EXPECT_TRUE(require_action("half_cut").preview_kind ==
              qhc::HostPreviewKind::HalfCut);
  EXPECT_TRUE(require_action("bridge_edges").preview_kind ==
              qhc::HostPreviewKind::None);

  EXPECT_FALSE(
      require_action("insert_edge_loop_tool").show_in_operations_panel);
  EXPECT_FALSE(require_action("knife_tool").show_in_operations_panel);
  EXPECT_FALSE(require_action("cut_tool").show_in_operations_panel);
  EXPECT_FALSE(require_action("mirror_tool").show_in_operations_panel);
  EXPECT_FALSE(require_action("poly_tool").show_in_operations_panel);
  EXPECT_FALSE(
      require_action("apply_active_material").show_in_operations_panel);
  EXPECT_TRUE(require_action("bevel_edges").show_in_operations_panel);
}

TEST(QdrEditorMetadataActionCatalogTests, HostContractActionCatalogDescribesSelectionRequirements) {
	const qhc::HostActionDescriptor& combine_mesh = require_action("combine_mesh");
        EXPECT_TRUE(combine_mesh.availability.selection_mode ==
                    qhc::HostSelectionMode::Object);
        EXPECT_TRUE(combine_mesh.availability.minimum_selected_objects == 2U);

	const qhc::HostActionDescriptor& create_outer_hull = require_action("create_outer_hull");
        EXPECT_TRUE(create_outer_hull.availability.selection_mode ==
                    qhc::HostSelectionMode::Object);
        EXPECT_TRUE(create_outer_hull.availability.minimum_selected_objects == 1U);

	const qhc::HostActionDescriptor& half_cut = require_action("half_cut");
        EXPECT_TRUE(half_cut.availability.selection_mode ==
                    qhc::HostSelectionMode::Object);
        EXPECT_TRUE(half_cut.availability.minimum_selected_objects == 1U);
        EXPECT_FALSE(half_cut.availability.requires_single_object);

	const qhc::HostActionDescriptor& snap_vertices = require_action("snap_vertices_to_active");
        EXPECT_TRUE(snap_vertices.availability.selection_mode ==
                    qhc::HostSelectionMode::Vertex);
        EXPECT_TRUE(snap_vertices.availability.minimum_selected_vertices == 2U);
        EXPECT_TRUE(snap_vertices.availability.required_active_kind ==
                    qhc::HostActiveSelectionKind::Vertex);

        const qhc::HostActionDescriptor& fill_hole = require_action("fill_hole");
        EXPECT_TRUE(fill_hole.availability.selection_mode ==
                    qhc::HostSelectionMode::Edge);
        EXPECT_TRUE(fill_hole.availability.minimum_selected_edges == 3U);
	EXPECT_TRUE(fill_hole.availability.requires_single_object);

	const qhc::HostActionDescriptor& bridge_faces = require_action("bridge_faces");
        EXPECT_TRUE(bridge_faces.availability.selection_mode ==
                    qhc::HostSelectionMode::Face);
        EXPECT_TRUE(bridge_faces.availability.minimum_selected_faces == 2U);

	const qhc::HostActionDescriptor& slice_quad = require_action("slice_quad");
	EXPECT_TRUE(slice_quad.availability.minimum_selected_faces == 1U);
	EXPECT_FALSE(slice_quad.availability.requires_single_object);

	const qhc::HostActionDescriptor& apply_material = require_action("apply_active_material");
        EXPECT_TRUE(apply_material.availability.selection_mode ==
                    qhc::HostSelectionMode::Face);
        EXPECT_TRUE(apply_material.availability.minimum_selected_faces == 1U);
	EXPECT_FALSE(apply_material.show_in_operations_panel);

	const qhc::HostActionDescriptor& mirror_tool = require_action("mirror_tool");
        EXPECT_TRUE(mirror_tool.kind == qhc::HostActionKind::Tool);
        EXPECT_TRUE(same_text(mirror_tool.tool_id, "mirror"));
        EXPECT_TRUE(mirror_tool.availability.selection_mode ==
                    qhc::HostSelectionMode::Object);
        EXPECT_TRUE(mirror_tool.availability.minimum_selected_objects == 1U);

	const qhc::HostActionDescriptor& invert_mesh = require_action("invert_mesh");
	EXPECT_TRUE(same_text(invert_mesh.label, "Flip Mesh Normals"));
	EXPECT_TRUE(same_text(invert_mesh.operation_id, "flip_face_normals"));
}

TEST(QdrEditorMetadataActionCatalogTests, HostContractActionIdsDisambiguateSharedLabelsAndSharedRuntimeOperations) {
	const qhc::HostActionDescriptor& radial_vertices = require_action("radial_align_vertices");
	const qhc::HostActionDescriptor& radial_edges = require_action("radial_align_edges");
	const qhc::HostActionDescriptor& radial_faces = require_action("radial_align_faces");

	EXPECT_TRUE(same_text(radial_vertices.label, "Radial Align"));
	EXPECT_TRUE(same_text(radial_edges.label, "Radial Align"));
	EXPECT_TRUE(same_text(radial_faces.label, "Radial Align"));
        EXPECT_TRUE(radial_vertices.availability.selection_mode ==
                    qhc::HostSelectionMode::Vertex);
        EXPECT_TRUE(radial_edges.availability.selection_mode ==
                    qhc::HostSelectionMode::Edge);
        EXPECT_TRUE(radial_faces.availability.selection_mode ==
                    qhc::HostSelectionMode::Face);
        EXPECT_TRUE(same_text(radial_vertices.operation_id, "radial_align"));
	EXPECT_TRUE(same_text(radial_edges.operation_id, "radial_align"));
	EXPECT_TRUE(same_text(radial_faces.operation_id, "radial_align"));

	EXPECT_TRUE(qhc::find_action_descriptor("recalculate_outside_mesh") == nullptr);
	EXPECT_TRUE(qhc::find_action_descriptor("recalculate_inside_mesh") == nullptr);
	EXPECT_TRUE(qhc::find_action_descriptor("recalculate_outside_faces") == nullptr);
	EXPECT_TRUE(qhc::find_action_descriptor("recalculate_inside_faces") == nullptr);
	EXPECT_TRUE(same_text(require_action("apply_active_material").operation_id, "assign_material"));
}
