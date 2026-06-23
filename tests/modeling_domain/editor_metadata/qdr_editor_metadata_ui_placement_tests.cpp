////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/metadata/host_contract.h>

#include <array>
#include <string_view>

namespace qhc = quader_host_contract;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

bool has_interaction_kind(std::string_view id, qhc::HostInteractionKind kind)
{
	const qhc::HostInteractionDescriptor *descriptor = qhc::find_interaction_descriptor(id);
	return descriptor != nullptr && descriptor->kind == kind;
}

const qhc::HostUiSurfaceDescriptor& require_surface(std::string_view id)
{
	const qhc::HostUiSurfaceDescriptor *descriptor = qhc::find_ui_surface_descriptor(id);
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

const qhc::HostUiPlacementDescriptor& require_placement(std::string_view id)
{
	const qhc::HostUiPlacementDescriptor *descriptor = qhc::find_ui_placement_descriptor(id);
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

bool is_known_panel(std::string_view id)
{
	return qhc::find_panel_descriptor(id) != nullptr ||
		qhc::find_operation_panel_descriptor(id) != nullptr;
}

template <std::size_t N>
void check_toolbar_order(
		std::string_view surface_id,
		const std::array<std::string_view, N>& expected)
{
	std::size_t index = 0;
	for (const qhc::HostUiPlacementDescriptor& placement : qhc::ui_placement_catalog()) {
		if (placement.surface_id != surface_id) {
			continue;
		}
		REQUIRE(index < expected.size());
		CHECK(same_text(placement.target_id, expected[index]));
		CHECK(placement.target_kind == qhc::HostUiPlacementTargetKind::tool);
		++index;
	}
	CHECK(index == expected.size());
}

} // namespace

TEST_CASE("host contract UI surfaces describe menu and toolbar hosts", "[host-contract]")
{
	const std::span<const qhc::HostUiSurfaceDescriptor> surfaces = qhc::ui_surface_catalog();
	REQUIRE(surfaces.size() == 4U);

	const qhc::HostUiSurfaceDescriptor& menu = require_surface("main_menu");
	CHECK(same_text(menu.label, "Main Menu"));
	CHECK(menu.kind == qhc::HostUiSurfaceKind::menu);
	CHECK_FALSE(menu.floating);
	CHECK_FALSE(menu.selection_contextual);

	const qhc::HostUiSurfaceDescriptor& operations = require_surface("operations_top_bar");
	CHECK(same_text(operations.label, "Operations Top Bar"));
	CHECK(operations.kind == qhc::HostUiSurfaceKind::toolbar);
	CHECK_FALSE(operations.floating);
	CHECK(operations.selection_contextual);

	const qhc::HostUiSurfaceDescriptor& tools = require_surface("viewport_tool_toolbar");
	CHECK(same_text(tools.label, "Viewport Tool Toolbar"));
	CHECK(tools.kind == qhc::HostUiSurfaceKind::toolbar);
	CHECK(tools.floating);
	CHECK_FALSE(tools.selection_contextual);

	const qhc::HostUiSurfaceDescriptor& settings = require_surface("tool_settings_panel");
	CHECK(same_text(settings.label, "Tool Settings Panel"));
	CHECK(settings.kind == qhc::HostUiSurfaceKind::panel);
	CHECK(settings.floating);
	CHECK_FALSE(settings.selection_contextual);
}

TEST_CASE("host contract UI groups and shell targets are discoverable", "[host-contract]")
{
	const std::span<const qhc::HostUiGroupDescriptor> groups = qhc::ui_group_catalog();
	REQUIRE(groups.size() == 8U);
	CHECK(qhc::find_ui_group_descriptor("file") != nullptr);
	CHECK(qhc::find_ui_group_descriptor("viewport_layout") != nullptr);
	CHECK(qhc::find_ui_group_descriptor("operation_settings") != nullptr);

	const std::span<const qhc::HostInteractionDescriptor> interactions = qhc::interaction_catalog();
	REQUIRE(interactions.size() == 38U);
	CHECK(has_interaction_kind("open_editor", qhc::HostInteractionKind::editor_action));
	CHECK(has_interaction_kind("apply_active_material", qhc::HostInteractionKind::editor_action));
	CHECK(has_interaction_kind("save_as_document", qhc::HostInteractionKind::editor_action));
	CHECK(has_interaction_kind("layout_four_view_quad", qhc::HostInteractionKind::viewport_action));
	CHECK(has_interaction_kind("active_preview_commit_or_cancel", qhc::HostInteractionKind::preview_action));
	CHECK(has_interaction_kind("active_modal_tool_precedence", qhc::HostInteractionKind::input_policy));

	const qhc::HostToolGroupDescriptor *transform_group = qhc::find_tool_group_descriptor("transform_tools");
	REQUIRE(transform_group != nullptr);
	REQUIRE(transform_group->tool_ids.size() == 3U);
	CHECK(same_text(transform_group->tool_ids[0], "move"));
	CHECK(same_text(transform_group->tool_ids[1], "rotate"));
	CHECK(same_text(transform_group->tool_ids[2], "scale"));

	const qhc::HostPanelDescriptor *viewport_settings = qhc::find_panel_descriptor("viewport_settings");
	REQUIRE(viewport_settings != nullptr);
	REQUIRE(viewport_settings->preference_group_ids.size() == 7U);
	CHECK(same_text(viewport_settings->preference_group_ids[0], "viewport.scene"));
	CHECK(same_text(viewport_settings->preference_group_ids[1], "viewport.grid"));
	CHECK(same_text(viewport_settings->preference_group_ids[2], "viewport.selection"));
	CHECK(same_text(viewport_settings->preference_group_ids[3], "viewport.overlay"));
	CHECK(same_text(viewport_settings->preference_group_ids[4], "viewport.vertices"));
	CHECK(same_text(viewport_settings->preference_group_ids[5], "viewport.picking"));
	CHECK(same_text(viewport_settings->preference_group_ids[6], "viewport.gizmo"));
}

TEST_CASE("host contract UI placements resolve to known descriptor-only targets", "[host-contract]")
{
	const std::span<const qhc::HostUiPlacementDescriptor> placements = qhc::ui_placement_catalog();
	REQUIRE(placements.size() == 31U);

	for (const qhc::HostUiPlacementDescriptor& placement : placements) {
		CHECK_FALSE(placement.id.empty());
		CHECK_FALSE(placement.surface_id.empty());
		CHECK_FALSE(placement.group_id.empty());
		CHECK_FALSE(placement.label.empty());
		CHECK_FALSE(placement.target_id.empty());
		CHECK(qhc::find_ui_surface_descriptor(placement.surface_id) != nullptr);
		CHECK(qhc::find_ui_group_descriptor(placement.group_id) != nullptr);

		switch (placement.target_kind) {
			case qhc::HostUiPlacementTargetKind::action:
				CHECK(qhc::find_action_descriptor(placement.target_id) != nullptr);
				break;
			case qhc::HostUiPlacementTargetKind::tool:
				CHECK(qhc::find_tool_descriptor(placement.target_id) != nullptr);
				break;
			case qhc::HostUiPlacementTargetKind::selection_mode:
				CHECK(qhc::find_selection_mode_descriptor(placement.target_id) != nullptr);
				break;
			case qhc::HostUiPlacementTargetKind::editor_action:
				CHECK(has_interaction_kind(placement.target_id, qhc::HostInteractionKind::editor_action));
				break;
			case qhc::HostUiPlacementTargetKind::viewport_action:
				CHECK(has_interaction_kind(placement.target_id, qhc::HostInteractionKind::viewport_action));
				break;
			case qhc::HostUiPlacementTargetKind::panel:
				CHECK(is_known_panel(placement.target_id));
				break;
			case qhc::HostUiPlacementTargetKind::preference_group:
				CHECK(qhc::find_preference_group_descriptor(placement.target_id) != nullptr);
				break;
		}
	}
}

TEST_CASE("host contract main menu placement preserves editor shell groups", "[host-contract]")
{
	const qhc::HostUiPlacementDescriptor& open_editor = require_placement("menu_open_editor");
	CHECK(same_text(open_editor.surface_id, "main_menu"));
	CHECK(same_text(open_editor.group_id, "editor"));
	CHECK(open_editor.target_kind == qhc::HostUiPlacementTargetKind::editor_action);
	CHECK(same_text(open_editor.target_id, "open_editor"));
	CHECK(open_editor.order == 0U);
	CHECK_FALSE(open_editor.separator_before);

	const qhc::HostUiPlacementDescriptor& new_document = require_placement("menu_new_document");
	CHECK(same_text(new_document.group_id, "file"));
	CHECK_FALSE(new_document.separator_before);
	CHECK(new_document.order < require_placement("menu_open_document").order);
	CHECK(require_placement("menu_open_document").order < require_placement("menu_save_document").order);
	CHECK(require_placement("menu_save_document").order < require_placement("menu_save_as_document").order);

	const qhc::HostUiPlacementDescriptor& layout = require_placement("menu_layout_single_view");
	CHECK(same_text(layout.group_id, "viewport_layout"));
	CHECK(layout.target_kind == qhc::HostUiPlacementTargetKind::viewport_action);
	CHECK_FALSE(layout.separator_before);
	CHECK(layout.order < require_placement("menu_layout_four_view_quad").order);

	const qhc::HostUiPlacementDescriptor& settings = require_placement("menu_viewport_settings_panel");
	CHECK(same_text(settings.group_id, "panels"));
	CHECK(settings.target_kind == qhc::HostUiPlacementTargetKind::panel);
	CHECK(same_text(settings.target_id, "viewport_settings"));
	CHECK_FALSE(settings.separator_before);
}

TEST_CASE("host contract toolbar placement mirrors current editor chrome order", "[host-contract]")
{
	check_toolbar_order("viewport_tool_toolbar", std::array<std::string_view, 7> {{
		"move",
		"rotate",
		"scale",
		"knife",
		"cut",
		"mirror",
		"poly",
	}});

	CHECK(same_text(require_placement("toolbar_move_tool").group_id, "transform_tools"));
	CHECK_FALSE(require_placement("toolbar_move_tool").separator_before);
	CHECK(same_text(require_placement("toolbar_knife_tool").group_id, "modal_modeling_tools"));
	CHECK_FALSE(require_placement("toolbar_knife_tool").separator_before);
}

TEST_CASE("host contract operation top bar places selection-scoped panels", "[host-contract]")
{
	const qhc::HostUiPlacementDescriptor& object = require_placement("operations_object_panel");
	const qhc::HostUiPlacementDescriptor& vertex = require_placement("operations_vertex_panel");
	const qhc::HostUiPlacementDescriptor& edge = require_placement("operations_edge_panel");
	const qhc::HostUiPlacementDescriptor& face = require_placement("operations_face_panel");

	CHECK(object.target_kind == qhc::HostUiPlacementTargetKind::panel);
	CHECK(vertex.target_kind == qhc::HostUiPlacementTargetKind::panel);
	CHECK(edge.target_kind == qhc::HostUiPlacementTargetKind::panel);
	CHECK(face.target_kind == qhc::HostUiPlacementTargetKind::panel);

	CHECK(qhc::find_operation_panel_descriptor(object.target_id) != nullptr);
	CHECK(qhc::find_operation_panel_descriptor(vertex.target_id) != nullptr);
	CHECK(qhc::find_operation_panel_descriptor(edge.target_id) != nullptr);
	CHECK(qhc::find_operation_panel_descriptor(face.target_id) != nullptr);

	CHECK(object.order < vertex.order);
	CHECK(vertex.order < edge.order);
	CHECK(edge.order < face.order);
}

TEST_CASE("host contract tool settings panel places preview-scoped preference groups", "[host-contract]")
{
	const qhc::HostUiPlacementDescriptor& bridge = require_placement("settings_interpolated_bridge");
	const qhc::HostUiPlacementDescriptor& edge_bevel = require_placement("settings_edge_bevel");
	const qhc::HostUiPlacementDescriptor& thicken = require_placement("settings_thicken");
	const qhc::HostUiPlacementDescriptor& slice_quad = require_placement("settings_slice_quad");
	const qhc::HostUiPlacementDescriptor& merge_by_distance = require_placement("settings_merge_by_distance");
	const qhc::HostUiPlacementDescriptor& cut = require_placement("settings_cut_keep_mode");

	CHECK(same_text(bridge.surface_id, "tool_settings_panel"));
	CHECK(same_text(bridge.target_id, "operation.bridge"));
	CHECK(same_text(bridge.label, "Interpolated Bridge"));
	CHECK(bridge.visibility == qhc::HostUiVisibilityCondition::bridge_preview_active);

	CHECK(same_text(edge_bevel.target_id, "operation.edge_bevel"));
	CHECK(edge_bevel.visibility == qhc::HostUiVisibilityCondition::edge_bevel_preview_active);
	CHECK(same_text(thicken.target_id, "operation.thicken"));
	CHECK(thicken.visibility == qhc::HostUiVisibilityCondition::thicken_preview_active);
	CHECK(same_text(slice_quad.target_id, "operation.slice_quad"));
	CHECK(slice_quad.visibility == qhc::HostUiVisibilityCondition::slice_quad_preview_active);
	CHECK(same_text(merge_by_distance.target_id, "operation.merge_by_distance"));
	CHECK(merge_by_distance.visibility == qhc::HostUiVisibilityCondition::merge_by_distance_preview_active);
	CHECK(same_text(cut.target_id, "operation.cut"));
	CHECK(cut.visibility == qhc::HostUiVisibilityCondition::cut_tool_active);

	CHECK(bridge.order < edge_bevel.order);
	CHECK(edge_bevel.order < thicken.order);
	CHECK(thicken.order < slice_quad.order);
	CHECK(slice_quad.order < merge_by_distance.order);
	CHECK(merge_by_distance.order < cut.order);
}
