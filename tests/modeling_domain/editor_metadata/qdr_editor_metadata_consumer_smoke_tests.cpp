////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/metadata/host_contract.h>

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
		case qhc::HostUiPlacementTargetKind::action:
			return qhc::find_action_descriptor(placement.target_id) != nullptr;
		case qhc::HostUiPlacementTargetKind::tool:
			return qhc::find_tool_descriptor(placement.target_id) != nullptr;
		case qhc::HostUiPlacementTargetKind::selection_mode:
			return qhc::find_selection_mode_descriptor(placement.target_id) != nullptr;
		case qhc::HostUiPlacementTargetKind::editor_action:
			return find_interaction(placement.target_id, qhc::HostInteractionKind::editor_action) != nullptr;
		case qhc::HostUiPlacementTargetKind::viewport_action:
			return find_interaction(placement.target_id, qhc::HostInteractionKind::viewport_action) != nullptr;
		case qhc::HostUiPlacementTargetKind::panel:
			return qhc::find_operation_panel_descriptor(placement.target_id) != nullptr ||
				find_panel(placement.target_id) != nullptr;
		case qhc::HostUiPlacementTargetKind::preference_group:
			return qhc::find_preference_group_descriptor(placement.target_id) != nullptr;
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

TEST_CASE("custom engine shell can assemble chrome from public host descriptors", "[host-contract]")
{
	std::vector<HostChromeItem> menu_items;
	std::vector<HostChromeItem> operation_bar_items;
	std::vector<HostChromeItem> viewport_toolbar_items;
	std::vector<HostChromeItem> settings_panel_items;

	for (const qhc::HostUiPlacementDescriptor& placement : qhc::ui_placement_catalog()) {
		const qhc::HostUiSurfaceDescriptor *surface = qhc::find_ui_surface_descriptor(placement.surface_id);
		REQUIRE(surface != nullptr);
		CHECK(qhc::find_ui_group_descriptor(placement.group_id) != nullptr);
		REQUIRE(placement_target_resolves(placement));

		if (same_text(surface->id, "main_menu")) {
			append_item(menu_items, placement);
		} else if (same_text(surface->id, "operations_top_bar")) {
			append_item(operation_bar_items, placement);
		} else if (same_text(surface->id, "viewport_tool_toolbar")) {
			append_item(viewport_toolbar_items, placement);
		} else if (same_text(surface->id, "tool_settings_panel")) {
			append_item(settings_panel_items, placement);
		} else {
			FAIL("Unhandled host UI surface in consumer smoke test");
		}
	}

	REQUIRE(menu_items.size() == 14U);
	CHECK(contains_target(menu_items, "open_editor"));
	CHECK(contains_target(menu_items, "save_as_document"));
	CHECK(contains_target(menu_items, "layout_four_view_quad"));
	CHECK(contains_target(menu_items, "viewport_settings"));

	REQUIRE(operation_bar_items.size() == 4U);
	CHECK(contains_target(operation_bar_items, "object_operations"));
	CHECK(contains_target(operation_bar_items, "vertex_operations"));
	CHECK(contains_target(operation_bar_items, "edge_operations"));
	CHECK(contains_target(operation_bar_items, "face_operations"));

	REQUIRE(viewport_toolbar_items.size() == 7U);
	CHECK(contains_target(viewport_toolbar_items, "move"));
	CHECK(contains_target(viewport_toolbar_items, "rotate"));
	CHECK(contains_target(viewport_toolbar_items, "scale"));
	CHECK(contains_target(viewport_toolbar_items, "knife"));
	CHECK(contains_target(viewport_toolbar_items, "cut"));
	CHECK(contains_target(viewport_toolbar_items, "mirror"));
	CHECK(contains_target(viewport_toolbar_items, "poly"));

	REQUIRE(settings_panel_items.size() == 6U);
	CHECK(contains_target(settings_panel_items, "operation.bridge"));
	CHECK(contains_target(settings_panel_items, "operation.edge_bevel"));
	CHECK(contains_target(settings_panel_items, "operation.thicken"));
	CHECK(contains_target(settings_panel_items, "operation.slice_quad"));
	CHECK(contains_target(settings_panel_items, "operation.merge_by_distance"));
	CHECK(contains_target(settings_panel_items, "operation.cut"));
}

TEST_CASE("custom engine shell can resolve preference-backed panels and tool groups", "[host-contract]")
{
	const qhc::HostPanelDescriptor *viewport_settings = qhc::find_panel_descriptor("viewport_settings");
	REQUIRE(viewport_settings != nullptr);
	REQUIRE(viewport_settings->preference_group_ids.size() == 7U);
	for (std::string_view group_id : viewport_settings->preference_group_ids) {
		const qhc::HostPreferenceGroupDescriptor *group = qhc::find_preference_group_descriptor(group_id);
		REQUIRE(group != nullptr);
		CHECK_FALSE(group->preference_ids.empty());
		for (std::string_view preference_id : group->preference_ids) {
			CHECK(qhc::find_preference_descriptor(preference_id) != nullptr);
		}
	}

	for (const qhc::HostToolGroupDescriptor& group : qhc::tool_group_catalog()) {
		CHECK_FALSE(group.id.empty());
		CHECK_FALSE(group.label.empty());
		CHECK_FALSE(group.tool_ids.empty());
		for (std::string_view tool_id : group.tool_ids) {
			CHECK(qhc::find_tool_descriptor(tool_id) != nullptr);
		}
	}
}

TEST_CASE("custom engine shell can resolve shortcuts and gestures from public targets", "[host-contract]")
{
	for (const qhc::HostShortcutDescriptor& shortcut : qhc::shortcut_catalog()) {
		switch (shortcut.target_kind) {
			case qhc::HostShortcutTargetKind::action:
				CHECK(qhc::find_action_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::tool:
				CHECK(qhc::find_tool_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::selection_mode:
				CHECK(qhc::find_selection_mode_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::editor_action:
				CHECK(find_interaction(shortcut.target_id, qhc::HostInteractionKind::editor_action) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::viewport_action:
				CHECK(find_interaction(shortcut.target_id, qhc::HostInteractionKind::viewport_action) != nullptr);
				break;
		}
	}

	for (const qhc::HostGestureDescriptor& gesture : qhc::gesture_catalog()) {
		switch (gesture.target_kind) {
			case qhc::HostGestureTargetKind::tool:
				CHECK(qhc::find_tool_descriptor(gesture.target_id) != nullptr);
				break;
			case qhc::HostGestureTargetKind::tool_group:
				CHECK(qhc::find_tool_group_descriptor(gesture.target_id) != nullptr);
				break;
			case qhc::HostGestureTargetKind::viewport_action:
				CHECK(find_interaction(gesture.target_id, qhc::HostInteractionKind::viewport_action) != nullptr);
				break;
			case qhc::HostGestureTargetKind::preview_action:
				CHECK(find_interaction(gesture.target_id, qhc::HostInteractionKind::preview_action) != nullptr);
				break;
			case qhc::HostGestureTargetKind::input_policy:
				CHECK(find_interaction(gesture.target_id, qhc::HostInteractionKind::input_policy) != nullptr);
				break;
		}
	}
}
