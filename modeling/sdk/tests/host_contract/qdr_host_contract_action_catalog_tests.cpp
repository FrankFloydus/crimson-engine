#include <catch2/catch_test_macros.hpp>

#include <quader_host_contract/host_contract.h>

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
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

} // namespace

TEST_CASE("host contract action catalog links panel actions to descriptor-only operations or tools", "[host-contract]")
{
	const std::span<const qhc::HostActionDescriptor> actions = qhc::action_catalog();
	REQUIRE(actions.size() == 45U);

	for (const qhc::HostActionDescriptor& action : actions) {
		CHECK_FALSE(action.id.empty());
		CHECK_FALSE(action.label.empty());

		if (action.kind == qhc::HostActionKind::operation) {
			CHECK_FALSE(action.operation_id.empty());
			CHECK(action.tool_id.empty());
			CHECK(qhc::find_operation_descriptor(action.operation_id) != nullptr);
		} else {
			CHECK(action.operation_id.empty());
			CHECK_FALSE(action.tool_id.empty());
			CHECK(qhc::find_tool_descriptor(action.tool_id) != nullptr);
		}
	}
}

TEST_CASE("host contract action catalog preserves operation panel visibility and preview kinds", "[host-contract]")
{
	CHECK(require_action("bevel_edges").preview_kind == qhc::HostPreviewKind::edge_bevel);
	CHECK(require_action("interpolated_bridge_edges").preview_kind == qhc::HostPreviewKind::bridge);
	CHECK(require_action("interpolated_bridge_faces").preview_kind == qhc::HostPreviewKind::bridge);
	CHECK(require_action("merge_by_distance").preview_kind == qhc::HostPreviewKind::merge_by_distance);
	CHECK(require_action("slice_quad").preview_kind == qhc::HostPreviewKind::slice_quad);
	CHECK(require_action("create_outer_hull").preview_kind == qhc::HostPreviewKind::thicken);
	CHECK(require_action("thicken_faces").preview_kind == qhc::HostPreviewKind::thicken);
	CHECK(require_action("bridge_edges").preview_kind == qhc::HostPreviewKind::none);

	CHECK_FALSE(require_action("insert_edge_loop_tool").show_in_operations_panel);
	CHECK_FALSE(require_action("knife_tool").show_in_operations_panel);
	CHECK_FALSE(require_action("cut_tool").show_in_operations_panel);
	CHECK_FALSE(require_action("poly_tool").show_in_operations_panel);
	CHECK_FALSE(require_action("apply_active_material").show_in_operations_panel);
	CHECK(require_action("bevel_edges").show_in_operations_panel);
}

TEST_CASE("host contract action catalog describes selection requirements", "[host-contract]")
{
	const qhc::HostActionDescriptor& combine_mesh = require_action("combine_mesh");
	CHECK(combine_mesh.availability.selection_mode == qhc::HostSelectionMode::object);
	CHECK(combine_mesh.availability.minimum_selected_objects == 2U);

	const qhc::HostActionDescriptor& create_outer_hull = require_action("create_outer_hull");
	CHECK(create_outer_hull.availability.selection_mode == qhc::HostSelectionMode::object);
	CHECK(create_outer_hull.availability.minimum_selected_objects == 1U);

	const qhc::HostActionDescriptor& snap_vertices = require_action("snap_vertices_to_active");
	CHECK(snap_vertices.availability.selection_mode == qhc::HostSelectionMode::vertex);
	CHECK(snap_vertices.availability.minimum_selected_vertices == 2U);
	CHECK(snap_vertices.availability.required_active_kind == qhc::HostActiveSelectionKind::vertex);

	const qhc::HostActionDescriptor& fill_hole = require_action("fill_hole");
	CHECK(fill_hole.availability.selection_mode == qhc::HostSelectionMode::edge);
	CHECK(fill_hole.availability.minimum_selected_edges == 3U);
	CHECK(fill_hole.availability.requires_single_object);

	const qhc::HostActionDescriptor& bridge_faces = require_action("bridge_faces");
	CHECK(bridge_faces.availability.selection_mode == qhc::HostSelectionMode::face);
	CHECK(bridge_faces.availability.minimum_selected_faces == 2U);

	const qhc::HostActionDescriptor& slice_quad = require_action("slice_quad");
	CHECK(slice_quad.availability.minimum_selected_faces == 1U);
	CHECK_FALSE(slice_quad.availability.requires_single_object);

	const qhc::HostActionDescriptor& apply_material = require_action("apply_active_material");
	CHECK(apply_material.availability.selection_mode == qhc::HostSelectionMode::face);
	CHECK(apply_material.availability.minimum_selected_faces == 1U);
	CHECK_FALSE(apply_material.show_in_operations_panel);
}

TEST_CASE("host contract action ids disambiguate shared labels and shared SDK operations", "[host-contract]")
{
	const qhc::HostActionDescriptor& radial_vertices = require_action("radial_align_vertices");
	const qhc::HostActionDescriptor& radial_edges = require_action("radial_align_edges");
	const qhc::HostActionDescriptor& radial_faces = require_action("radial_align_faces");

	CHECK(same_text(radial_vertices.label, "Radial Align"));
	CHECK(same_text(radial_edges.label, "Radial Align"));
	CHECK(same_text(radial_faces.label, "Radial Align"));
	CHECK(radial_vertices.availability.selection_mode == qhc::HostSelectionMode::vertex);
	CHECK(radial_edges.availability.selection_mode == qhc::HostSelectionMode::edge);
	CHECK(radial_faces.availability.selection_mode == qhc::HostSelectionMode::face);
	CHECK(same_text(radial_vertices.operation_id, "radial_align"));
	CHECK(same_text(radial_edges.operation_id, "radial_align"));
	CHECK(same_text(radial_faces.operation_id, "radial_align"));

	CHECK(same_text(require_action("recalculate_outside_mesh").operation_id, "recalculate_face_normals"));
	CHECK(same_text(require_action("recalculate_inside_mesh").operation_id, "recalculate_face_normals"));
	CHECK(same_text(require_action("recalculate_outside_faces").operation_id, "recalculate_face_normals"));
	CHECK(same_text(require_action("recalculate_inside_faces").operation_id, "recalculate_face_normals"));
	CHECK(same_text(require_action("apply_active_material").operation_id, "assign_material"));
}
