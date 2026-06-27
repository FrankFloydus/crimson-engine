////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/metadata/host_contract.hpp>

#include <array>
#include <string_view>

namespace qhc = quader_host_contract;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostOperationPanelDescriptor& require_panel(std::string_view id)
{
	const qhc::HostOperationPanelDescriptor *descriptor = qhc::find_operation_panel_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

bool panel_contains(const qhc::HostOperationPanelDescriptor& panel, std::string_view action_id)
{
	for (std::string_view candidate : panel.action_ids) {
		if (candidate == action_id) {
			return true;
		}
	}
	return false;
}

template <std::size_t Size>
void check_action_order(
		const qhc::HostOperationPanelDescriptor& panel,
		const std::array<std::string_view, Size>& expected)
{
	EXPECT_TRUE(panel.action_ids.size() == expected.size());
	for (std::size_t i = 0; i < expected.size(); ++i) {
		EXPECT_TRUE(same_text(panel.action_ids[i], expected[i]));
	}
}

} // namespace

TEST(QdrEditorMetadataPanelCatalogTests, HostContractOperationPanelCatalogExposesOnePanelPerSelectionMode) {
	const std::span<const qhc::HostOperationPanelDescriptor> panels = qhc::operation_panel_catalog();
	EXPECT_TRUE(panels.size() == 4U);

	const qhc::HostOperationPanelDescriptor& object = require_panel("object_operations");
	EXPECT_TRUE(same_text(object.label, "Object Operations"));
	EXPECT_TRUE(object.selection_mode == qhc::HostSelectionMode::object);
	EXPECT_TRUE(object.fallback_for_unknown_selection);

	const qhc::HostOperationPanelDescriptor& vertex = require_panel("vertex_operations");
	EXPECT_TRUE(same_text(vertex.label, "Vertex Operations"));
	EXPECT_TRUE(vertex.selection_mode == qhc::HostSelectionMode::vertex);
	EXPECT_FALSE(vertex.fallback_for_unknown_selection);

	const qhc::HostOperationPanelDescriptor& edge = require_panel("edge_operations");
	EXPECT_TRUE(same_text(edge.label, "Edge Operations"));
	EXPECT_TRUE(edge.selection_mode == qhc::HostSelectionMode::edge);
	EXPECT_FALSE(edge.fallback_for_unknown_selection);

	const qhc::HostOperationPanelDescriptor& face = require_panel("face_operations");
	EXPECT_TRUE(same_text(face.label, "Face Operations"));
	EXPECT_TRUE(face.selection_mode == qhc::HostSelectionMode::face);
	EXPECT_FALSE(face.fallback_for_unknown_selection);
}

TEST(QdrEditorMetadataPanelCatalogTests, HostContractOperationPanelItemsResolveToVisibleActionsForTheSameSelectionMode) {
	std::size_t item_count = 0;
	for (const qhc::HostOperationPanelDescriptor& panel : qhc::operation_panel_catalog()) {
		for (std::string_view action_id : panel.action_ids) {
			const qhc::HostActionDescriptor *action = qhc::find_action_descriptor(action_id);
			EXPECT_TRUE(action != nullptr);
			EXPECT_TRUE(action->show_in_operations_panel);
			EXPECT_TRUE(action->availability.selection_mode == panel.selection_mode);
			++item_count;
		}
	}

	EXPECT_TRUE(item_count == 36U);
}

TEST(QdrEditorMetadataPanelCatalogTests, HostContractOperationPanelPreservesNativePanelOrdering) {
	check_action_order(require_panel("object_operations"), std::array<std::string_view, 5> {{
		"invert_mesh",
		"shade_smooth_mesh",
		"shade_flat_mesh",
		"combine_mesh",
		"create_outer_hull",
	}});

	check_action_order(require_panel("vertex_operations"), std::array<std::string_view, 8> {{
		"snap_vertices_to_active",
		"merge_vertices",
		"merge_vertices_at_center",
		"merge_by_distance",
		"bevel_vertices",
		"connect_vertices",
		"dissolve_vertices",
		"radial_align_vertices",
	}});

	check_action_order(require_panel("edge_operations"), std::array<std::string_view, 13> {{
		"snap_edges_to_active",
		"connect_edges",
		"split_edges",
		"harden_edge_normals",
		"soften_edge_normals",
		"bevel_edges",
		"bridge_edges",
		"interpolated_bridge_edges",
		"dissolve_edges",
		"merge_edges",
		"collapse_edges",
		"fill_hole",
		"radial_align_edges",
	}});

	check_action_order(require_panel("face_operations"), std::array<std::string_view, 10> {{
		"bridge_faces",
		"interpolated_bridge_faces",
		"invert_faces",
		"combine_faces",
		"collapse_faces",
		"radial_align_faces",
		"slice_quad",
		"thicken_faces",
		"extract_faces",
		"detach_faces",
	}});
}

TEST(QdrEditorMetadataPanelCatalogTests, HostContractOperationPanelExcludesViewportOnlyToolActions) {
	for (const qhc::HostOperationPanelDescriptor& panel : qhc::operation_panel_catalog()) {
		EXPECT_FALSE(panel_contains(panel, "insert_edge_loop_tool"));
		EXPECT_FALSE(panel_contains(panel, "knife_tool"));
		EXPECT_FALSE(panel_contains(panel, "cut_tool"));
		EXPECT_FALSE(panel_contains(panel, "poly_tool"));
	}

	EXPECT_FALSE(qhc::find_action_descriptor("insert_edge_loop_tool")->show_in_operations_panel);
	EXPECT_FALSE(qhc::find_action_descriptor("knife_tool")->show_in_operations_panel);
	EXPECT_FALSE(qhc::find_action_descriptor("cut_tool")->show_in_operations_panel);
	EXPECT_FALSE(qhc::find_action_descriptor("poly_tool")->show_in_operations_panel);
}
