////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/metadata/host_contract.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace qhc = quader_host_contract;

namespace {

/**
 * Represents a Host Chrome Item value used by the automated test suite.
 */
struct HostChromeItem {
	std::string_view surface_id;
	std::string_view group_id;
	std::string_view label;
	std::string_view target_id;
};

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostInteractionDescriptor *find_interaction(
		std::string_view id,
		qhc::HostInteractionKind kind)
{
	const qhc::HostInteractionDescriptor *descriptor = qhc::find_interaction_descriptor(id);
	if (descriptor == nullptr || descriptor->kind != kind) {
		return nullptr;
	}
	return descriptor;
}

const qhc::HostPanelDescriptor *find_panel(std::string_view id)
{
	return qhc::find_panel_descriptor(id);
}

bool placement_target_resolves(const qhc::HostUiPlacementDescriptor& placement)
{
	switch (placement.target_kind) {
        case qhc::HostUiPlacementTargetKind::Action:
          return qhc::find_action_descriptor(placement.target_id) != nullptr;
        case qhc::HostUiPlacementTargetKind::Tool:
          return qhc::find_tool_descriptor(placement.target_id) != nullptr;
        case qhc::HostUiPlacementTargetKind::SelectionMode:
          return qhc::find_selection_mode_descriptor(placement.target_id) !=
                 nullptr;
        case qhc::HostUiPlacementTargetKind::EditorAction:
          return find_interaction(placement.target_id,
                                  qhc::HostInteractionKind::EditorAction) !=
                 nullptr;
        case qhc::HostUiPlacementTargetKind::ViewportAction:
          return find_interaction(placement.target_id,
                                  qhc::HostInteractionKind::ViewportAction) !=
                 nullptr;
        case qhc::HostUiPlacementTargetKind::Panel:
          return qhc::find_operation_panel_descriptor(placement.target_id) !=
                     nullptr ||
                 find_panel(placement.target_id) != nullptr;
        case qhc::HostUiPlacementTargetKind::PreferenceGroup:
          return qhc::find_preference_group_descriptor(placement.target_id) !=
                 nullptr;
	}
	return false;
}

bool contains_target(std::span<const HostChromeItem> items, std::string_view target_id)
{
	for (const HostChromeItem& item : items) {
		if (item.target_id == target_id) {
			return true;
		}
	}
	return false;
}

void append_item(std::vector<HostChromeItem>& items, const qhc::HostUiPlacementDescriptor& placement)
{
	items.push_back({
		placement.surface_id,
		placement.group_id,
		placement.label,
		placement.target_id,
	});
}

} // namespace

TEST(QdrEditorMetadataConsumerSmokeTests, CustomEngineShellCanAssembleChromeFromPublicHostDescriptors) {
	std::vector<HostChromeItem> menu_items;
	std::vector<HostChromeItem> operation_bar_items;
	std::vector<HostChromeItem> viewport_toolbar_items;
	std::vector<HostChromeItem> settings_panel_items;

	for (const qhc::HostUiPlacementDescriptor& placement : qhc::ui_placement_catalog()) {
		const qhc::HostUiSurfaceDescriptor *surface = qhc::find_ui_surface_descriptor(placement.surface_id);
		EXPECT_TRUE(surface != nullptr);
		EXPECT_TRUE(qhc::find_ui_group_descriptor(placement.group_id) != nullptr);
		EXPECT_TRUE(placement_target_resolves(placement));

		if (same_text(surface->id, "main_menu")) {
			append_item(menu_items, placement);
		} else if (same_text(surface->id, "operations_top_bar")) {
			append_item(operation_bar_items, placement);
		} else if (same_text(surface->id, "viewport_tool_toolbar")) {
			append_item(viewport_toolbar_items, placement);
		} else if (same_text(surface->id, "tool_settings_panel")) {
			append_item(settings_panel_items, placement);
		} else {
			FAIL() << "Unhandled host UI surface in consumer smoke test";
		}
	}

	EXPECT_TRUE(menu_items.size() == 14U);
	EXPECT_TRUE(contains_target(menu_items, "open_editor"));
	EXPECT_TRUE(contains_target(menu_items, "save_as_document"));
	EXPECT_TRUE(contains_target(menu_items, "layout_four_view_quad"));
	EXPECT_TRUE(contains_target(menu_items, "viewport_settings"));

	EXPECT_TRUE(operation_bar_items.size() == 4U);
	EXPECT_TRUE(contains_target(operation_bar_items, "object_operations"));
	EXPECT_TRUE(contains_target(operation_bar_items, "vertex_operations"));
	EXPECT_TRUE(contains_target(operation_bar_items, "edge_operations"));
	EXPECT_TRUE(contains_target(operation_bar_items, "face_operations"));

	EXPECT_TRUE(viewport_toolbar_items.size() == 7U);
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "move"));
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "rotate"));
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "scale"));
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "knife"));
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "cut"));
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "mirror"));
	EXPECT_TRUE(contains_target(viewport_toolbar_items, "poly"));

	EXPECT_TRUE(settings_panel_items.size() == 6U);
	EXPECT_TRUE(contains_target(settings_panel_items, "operation.bridge"));
	EXPECT_TRUE(contains_target(settings_panel_items, "operation.edge_bevel"));
	EXPECT_TRUE(contains_target(settings_panel_items, "operation.thicken"));
	EXPECT_TRUE(contains_target(settings_panel_items, "operation.slice_quad"));
	EXPECT_TRUE(contains_target(settings_panel_items, "operation.merge_by_distance"));
	EXPECT_TRUE(contains_target(settings_panel_items, "operation.cut"));
}

TEST(QdrEditorMetadataConsumerSmokeTests, CustomEngineShellCanResolvePreferenceBackedPanelsAndToolGroups) {
	const qhc::HostPanelDescriptor *viewport_settings = qhc::find_panel_descriptor("viewport_settings");
	EXPECT_TRUE(viewport_settings != nullptr);
	EXPECT_TRUE(viewport_settings->preference_group_ids.size() == 7U);
	for (std::string_view group_id : viewport_settings->preference_group_ids) {
		const qhc::HostPreferenceGroupDescriptor *group = qhc::find_preference_group_descriptor(group_id);
		EXPECT_TRUE(group != nullptr);
		EXPECT_FALSE(group->preference_ids.empty());
		for (std::string_view preference_id : group->preference_ids) {
			EXPECT_TRUE(qhc::find_preference_descriptor(preference_id) != nullptr);
		}
	}

	for (const qhc::HostToolGroupDescriptor& group : qhc::tool_group_catalog()) {
		EXPECT_FALSE(group.id.empty());
		EXPECT_FALSE(group.label.empty());
		EXPECT_FALSE(group.tool_ids.empty());
		for (std::string_view tool_id : group.tool_ids) {
			EXPECT_TRUE(qhc::find_tool_descriptor(tool_id) != nullptr);
		}
	}
}

TEST(QdrEditorMetadataConsumerSmokeTests, CustomEngineShellCanResolveShortcutsAndGesturesFromPublicTargets) {
	for (const qhc::HostShortcutDescriptor& shortcut : qhc::shortcut_catalog()) {
		switch (shortcut.target_kind) {
                case qhc::HostShortcutTargetKind::Action:
                  EXPECT_TRUE(qhc::find_action_descriptor(shortcut.target_id) !=
                              nullptr);
                  break;
                case qhc::HostShortcutTargetKind::Tool:
                  EXPECT_TRUE(qhc::find_tool_descriptor(shortcut.target_id) !=
                              nullptr);
                  break;
                case qhc::HostShortcutTargetKind::SelectionMode:
                  EXPECT_TRUE(qhc::find_selection_mode_descriptor(
                                  shortcut.target_id) != nullptr);
                  break;
                case qhc::HostShortcutTargetKind::EditorAction:
                  EXPECT_TRUE(find_interaction(
                                  shortcut.target_id,
                                  qhc::HostInteractionKind::EditorAction) !=
                              nullptr);
                  break;
                case qhc::HostShortcutTargetKind::ViewportAction:
                  EXPECT_TRUE(find_interaction(
                                  shortcut.target_id,
                                  qhc::HostInteractionKind::ViewportAction) !=
                              nullptr);
                  break;
		}
	}

	for (const qhc::HostGestureDescriptor& gesture : qhc::gesture_catalog()) {
		switch (gesture.target_kind) {
                case qhc::HostGestureTargetKind::Tool:
                  EXPECT_TRUE(qhc::find_tool_descriptor(gesture.target_id) !=
                              nullptr);
                  break;
                case qhc::HostGestureTargetKind::ToolGroup:
                  EXPECT_TRUE(qhc::find_tool_group_descriptor(
                                  gesture.target_id) != nullptr);
                  break;
                case qhc::HostGestureTargetKind::ViewportAction:
                  EXPECT_TRUE(find_interaction(
                                  gesture.target_id,
                                  qhc::HostInteractionKind::ViewportAction) !=
                              nullptr);
                  break;
                case qhc::HostGestureTargetKind::PreviewAction:
                  EXPECT_TRUE(find_interaction(
                                  gesture.target_id,
                                  qhc::HostInteractionKind::PreviewAction) !=
                              nullptr);
                  break;
                case qhc::HostGestureTargetKind::InputPolicy:
                  EXPECT_TRUE(
                      find_interaction(gesture.target_id,
                                       qhc::HostInteractionKind::InputPolicy) !=
                      nullptr);
                  break;
		}
	}
}
