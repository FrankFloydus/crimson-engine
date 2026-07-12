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

bool has_interaction_kind(std::string_view id, qhc::HostInteractionKind kind)
{
	const qhc::HostInteractionDescriptor *descriptor = qhc::find_interaction_descriptor(id);
	return descriptor != nullptr && descriptor->kind == kind;
}

const qhc::HostUiSurfaceDescriptor& require_surface(std::string_view id)
{
	const qhc::HostUiSurfaceDescriptor *descriptor = qhc::find_ui_surface_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

const qhc::HostUiPlacementDescriptor& require_placement(std::string_view id)
{
	const qhc::HostUiPlacementDescriptor *descriptor = qhc::find_ui_placement_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

bool is_known_panel(std::string_view id)
{
	return qhc::find_panel_descriptor(id) != nullptr ||
		qhc::find_operation_panel_descriptor(id) != nullptr;
}

template <std::size_t ExpectedSize>
void check_toolbar_order(
		std::string_view surface_id,
		const std::array<std::string_view, ExpectedSize>& expected)
{
	std::size_t index = 0;
	for (const qhc::HostUiPlacementDescriptor& placement : qhc::ui_placement_catalog()) {
		if (placement.surface_id != surface_id) {
			continue;
		}
		EXPECT_TRUE(index < expected.size());
		EXPECT_TRUE(same_text(placement.target_id, expected[index]));
                EXPECT_TRUE(placement.target_kind ==
                            qhc::HostUiPlacementTargetKind::Tool);
                ++index;
	}
	EXPECT_TRUE(index == expected.size());
}

} // namespace

TEST(QdrEditorMetadataUiPlacementTests, HostContractUISurfacesDescribeMenuAndToolbarHosts) {
	const std::span<const qhc::HostUiSurfaceDescriptor> surfaces = qhc::ui_surface_catalog();
	EXPECT_TRUE(surfaces.size() == 4U);

	const qhc::HostUiSurfaceDescriptor& menu = require_surface("main_menu");
	EXPECT_TRUE(same_text(menu.label, "Main Menu"));
        EXPECT_TRUE(menu.kind == qhc::HostUiSurfaceKind::Menu);
        EXPECT_FALSE(menu.floating);
	EXPECT_FALSE(menu.selection_contextual);

	const qhc::HostUiSurfaceDescriptor& operations = require_surface("operations_top_bar");
	EXPECT_TRUE(same_text(operations.label, "Operations Top Bar"));
        EXPECT_TRUE(operations.kind == qhc::HostUiSurfaceKind::Toolbar);
        EXPECT_FALSE(operations.floating);
	EXPECT_TRUE(operations.selection_contextual);

	const qhc::HostUiSurfaceDescriptor& tools = require_surface("viewport_tool_toolbar");
	EXPECT_TRUE(same_text(tools.label, "Viewport Tool Toolbar"));
        EXPECT_TRUE(tools.kind == qhc::HostUiSurfaceKind::Toolbar);
        EXPECT_TRUE(tools.floating);
	EXPECT_FALSE(tools.selection_contextual);

	const qhc::HostUiSurfaceDescriptor& settings = require_surface("tool_settings_panel");
	EXPECT_TRUE(same_text(settings.label, "Tool Settings Panel"));
        EXPECT_TRUE(settings.kind == qhc::HostUiSurfaceKind::Panel);
        EXPECT_TRUE(settings.floating);
	EXPECT_FALSE(settings.selection_contextual);
}

TEST(QdrEditorMetadataUiPlacementTests, HostContractUIGroupsAndShellTargetsAreDiscoverable) {
	const std::span<const qhc::HostUiGroupDescriptor> groups = qhc::ui_group_catalog();
	EXPECT_TRUE(groups.size() == 8U);
	EXPECT_TRUE(qhc::find_ui_group_descriptor("file") != nullptr);
	EXPECT_TRUE(qhc::find_ui_group_descriptor("viewport_layout") != nullptr);
	EXPECT_TRUE(qhc::find_ui_group_descriptor("operation_settings") != nullptr);

	const std::span<const qhc::HostInteractionDescriptor> interactions = qhc::interaction_catalog();
	EXPECT_TRUE(interactions.size() == 39U);
        EXPECT_TRUE(has_interaction_kind(
            "open_editor", qhc::HostInteractionKind::EditorAction));
        EXPECT_TRUE(has_interaction_kind(
            "apply_active_material", qhc::HostInteractionKind::EditorAction));
        EXPECT_TRUE(has_interaction_kind(
            "save_as_document", qhc::HostInteractionKind::EditorAction));
        EXPECT_TRUE(has_interaction_kind(
            "layout_four_view_quad", qhc::HostInteractionKind::ViewportAction));
        EXPECT_TRUE(has_interaction_kind(
            "focus_selection", qhc::HostInteractionKind::ViewportAction));
        EXPECT_TRUE(
            has_interaction_kind("active_preview_commit_or_cancel",
                                 qhc::HostInteractionKind::PreviewAction));
        EXPECT_TRUE(
            has_interaction_kind("active_modal_tool_precedence",
                                 qhc::HostInteractionKind::InputPolicy));

        const qhc::HostToolGroupDescriptor *transform_group = qhc::find_tool_group_descriptor("transform_tools");
	EXPECT_TRUE(transform_group != nullptr);
	EXPECT_TRUE(transform_group->tool_ids.size() == 3U);
	EXPECT_TRUE(same_text(transform_group->tool_ids[0], "move"));
	EXPECT_TRUE(same_text(transform_group->tool_ids[1], "rotate"));
	EXPECT_TRUE(same_text(transform_group->tool_ids[2], "scale"));

	const qhc::HostPanelDescriptor *viewport_settings = qhc::find_panel_descriptor("viewport_settings");
	EXPECT_TRUE(viewport_settings != nullptr);
	EXPECT_TRUE(viewport_settings->preference_group_ids.size() == 7U);
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[0], "viewport.scene"));
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[1], "viewport.grid"));
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[2], "viewport.selection"));
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[3], "viewport.overlay"));
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[4], "viewport.vertices"));
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[5], "viewport.picking"));
	EXPECT_TRUE(same_text(viewport_settings->preference_group_ids[6], "viewport.gizmo"));
}

TEST(QdrEditorMetadataUiPlacementTests, HostContractUIPlacementsResolveToKnownDescriptorOnlyTargets) {
	const std::span<const qhc::HostUiPlacementDescriptor> placements = qhc::ui_placement_catalog();
	EXPECT_TRUE(placements.size() == 32U);

	for (const qhc::HostUiPlacementDescriptor& placement : placements) {
		EXPECT_FALSE(placement.id.empty());
		EXPECT_FALSE(placement.surface_id.empty());
		EXPECT_FALSE(placement.group_id.empty());
		EXPECT_FALSE(placement.label.empty());
		EXPECT_FALSE(placement.target_id.empty());
		EXPECT_TRUE(qhc::find_ui_surface_descriptor(placement.surface_id) != nullptr);
		EXPECT_TRUE(qhc::find_ui_group_descriptor(placement.group_id) != nullptr);

		switch (placement.target_kind) {
                case qhc::HostUiPlacementTargetKind::Action:
                  EXPECT_TRUE(qhc::find_action_descriptor(
                                  placement.target_id) != nullptr);
                  break;
                case qhc::HostUiPlacementTargetKind::Tool:
                  EXPECT_TRUE(qhc::find_tool_descriptor(placement.target_id) !=
                              nullptr);
                  break;
                case qhc::HostUiPlacementTargetKind::SelectionMode:
                  EXPECT_TRUE(qhc::find_selection_mode_descriptor(
                                  placement.target_id) != nullptr);
                  break;
                case qhc::HostUiPlacementTargetKind::EditorAction:
                  EXPECT_TRUE(has_interaction_kind(
                      placement.target_id,
                      qhc::HostInteractionKind::EditorAction));
                  break;
                case qhc::HostUiPlacementTargetKind::ViewportAction:
                  EXPECT_TRUE(has_interaction_kind(
                      placement.target_id,
                      qhc::HostInteractionKind::ViewportAction));
                  break;
                case qhc::HostUiPlacementTargetKind::Panel:
                  EXPECT_TRUE(is_known_panel(placement.target_id));
                  break;
                case qhc::HostUiPlacementTargetKind::PreferenceGroup:
                  EXPECT_TRUE(qhc::find_preference_group_descriptor(
                                  placement.target_id) != nullptr);
                  break;
		}
	}
}

TEST(QdrEditorMetadataUiPlacementTests, HostContractMainMenuPlacementPreservesEditorShellGroups) {
	const qhc::HostUiPlacementDescriptor& open_editor = require_placement("menu_open_editor");
	EXPECT_TRUE(same_text(open_editor.surface_id, "main_menu"));
	EXPECT_TRUE(same_text(open_editor.group_id, "editor"));
        EXPECT_TRUE(open_editor.target_kind ==
                    qhc::HostUiPlacementTargetKind::EditorAction);
        EXPECT_TRUE(same_text(open_editor.target_id, "open_editor"));
	EXPECT_TRUE(open_editor.order == 0U);
	EXPECT_FALSE(open_editor.separator_before);

	const qhc::HostUiPlacementDescriptor& new_document = require_placement("menu_new_document");
	EXPECT_TRUE(same_text(new_document.group_id, "file"));
	EXPECT_FALSE(new_document.separator_before);
	EXPECT_TRUE(new_document.order < require_placement("menu_open_document").order);
	EXPECT_TRUE(require_placement("menu_open_document").order < require_placement("menu_save_document").order);
	EXPECT_TRUE(require_placement("menu_save_document").order < require_placement("menu_save_as_document").order);

	const qhc::HostUiPlacementDescriptor& layout = require_placement("menu_layout_single_view");
	EXPECT_TRUE(same_text(layout.group_id, "viewport_layout"));
        EXPECT_TRUE(layout.target_kind ==
                    qhc::HostUiPlacementTargetKind::ViewportAction);
        EXPECT_FALSE(layout.separator_before);
	EXPECT_TRUE(layout.order < require_placement("menu_layout_four_view_quad").order);

	const qhc::HostUiPlacementDescriptor& settings = require_placement("menu_viewport_settings_panel");
	EXPECT_TRUE(same_text(settings.group_id, "panels"));
        EXPECT_TRUE(settings.target_kind ==
                    qhc::HostUiPlacementTargetKind::Panel);
        EXPECT_TRUE(same_text(settings.target_id, "viewport_settings"));
	EXPECT_FALSE(settings.separator_before);
}

TEST(QdrEditorMetadataUiPlacementTests, HostContractToolbarPlacementMirrorsCurrentEditorChromeOrder) {
	check_toolbar_order("viewport_tool_toolbar", std::array<std::string_view, 7> {{
		"move",
		"rotate",
		"scale",
		"knife",
		"cut",
		"mirror",
		"poly",
	}});

	EXPECT_TRUE(same_text(require_placement("toolbar_move_tool").group_id, "transform_tools"));
	EXPECT_FALSE(require_placement("toolbar_move_tool").separator_before);
	EXPECT_TRUE(same_text(require_placement("toolbar_knife_tool").group_id, "modal_modeling_tools"));
	EXPECT_FALSE(require_placement("toolbar_knife_tool").separator_before);
}

TEST(QdrEditorMetadataUiPlacementTests, HostContractOperationTopBarPlacesSelectionScopedPanels) {
	const qhc::HostUiPlacementDescriptor& object = require_placement("operations_object_panel");
	const qhc::HostUiPlacementDescriptor& vertex = require_placement("operations_vertex_panel");
	const qhc::HostUiPlacementDescriptor& edge = require_placement("operations_edge_panel");
	const qhc::HostUiPlacementDescriptor& face = require_placement("operations_face_panel");

        EXPECT_TRUE(object.target_kind ==
                    qhc::HostUiPlacementTargetKind::Panel);
        EXPECT_TRUE(vertex.target_kind ==
                    qhc::HostUiPlacementTargetKind::Panel);
        EXPECT_TRUE(edge.target_kind == qhc::HostUiPlacementTargetKind::Panel);
        EXPECT_TRUE(face.target_kind == qhc::HostUiPlacementTargetKind::Panel);

        EXPECT_TRUE(qhc::find_operation_panel_descriptor(object.target_id) != nullptr);
	EXPECT_TRUE(qhc::find_operation_panel_descriptor(vertex.target_id) != nullptr);
	EXPECT_TRUE(qhc::find_operation_panel_descriptor(edge.target_id) != nullptr);
	EXPECT_TRUE(qhc::find_operation_panel_descriptor(face.target_id) != nullptr);

	EXPECT_TRUE(object.order < vertex.order);
	EXPECT_TRUE(vertex.order < edge.order);
	EXPECT_TRUE(edge.order < face.order);
}

TEST(QdrEditorMetadataUiPlacementTests, HostContractToolSettingsPanelPlacesPreviewScopedPreferenceGroups) {
	const qhc::HostUiPlacementDescriptor& bridge = require_placement("settings_interpolated_bridge");
	const qhc::HostUiPlacementDescriptor& edge_bevel = require_placement("settings_edge_bevel");
	const qhc::HostUiPlacementDescriptor& thicken = require_placement("settings_thicken");
	const qhc::HostUiPlacementDescriptor& slice_quad = require_placement("settings_slice_quad");
	const qhc::HostUiPlacementDescriptor& merge_by_distance = require_placement("settings_merge_by_distance");
	const qhc::HostUiPlacementDescriptor& half_cut = require_placement("settings_half_cut");
	const qhc::HostUiPlacementDescriptor& cut = require_placement("settings_cut_keep_mode");

	EXPECT_TRUE(same_text(bridge.surface_id, "tool_settings_panel"));
	EXPECT_TRUE(same_text(bridge.target_id, "operation.bridge"));
	EXPECT_TRUE(same_text(bridge.label, "Interpolated Bridge"));
        EXPECT_TRUE(bridge.visibility ==
                    qhc::HostUiVisibilityCondition::BridgePreviewActive);

        EXPECT_TRUE(same_text(edge_bevel.target_id, "operation.edge_bevel"));
        EXPECT_TRUE(edge_bevel.visibility ==
                    qhc::HostUiVisibilityCondition::EdgeBevelPreviewActive);
        EXPECT_TRUE(same_text(thicken.target_id, "operation.thicken"));
        EXPECT_TRUE(thicken.visibility ==
                    qhc::HostUiVisibilityCondition::ThickenPreviewActive);
        EXPECT_TRUE(same_text(slice_quad.target_id, "operation.slice_quad"));
        EXPECT_TRUE(slice_quad.visibility ==
                    qhc::HostUiVisibilityCondition::SliceQuadPreviewActive);
        EXPECT_TRUE(same_text(merge_by_distance.target_id, "operation.merge_by_distance"));
        EXPECT_TRUE(
            merge_by_distance.visibility ==
            qhc::HostUiVisibilityCondition::MergeByDistancePreviewActive);
        EXPECT_TRUE(same_text(half_cut.target_id, "operation.half_cut"));
        EXPECT_TRUE(half_cut.visibility ==
                    qhc::HostUiVisibilityCondition::HalfCutPreviewActive);
        EXPECT_TRUE(same_text(cut.target_id, "operation.cut"));
        EXPECT_TRUE(cut.visibility ==
                    qhc::HostUiVisibilityCondition::CutToolActive);

        EXPECT_TRUE(bridge.order < edge_bevel.order);
	EXPECT_TRUE(edge_bevel.order < thicken.order);
	EXPECT_TRUE(thicken.order < slice_quad.order);
	EXPECT_TRUE(slice_quad.order < merge_by_distance.order);
	EXPECT_TRUE(merge_by_distance.order < half_cut.order);
	EXPECT_TRUE(half_cut.order < cut.order);
}
