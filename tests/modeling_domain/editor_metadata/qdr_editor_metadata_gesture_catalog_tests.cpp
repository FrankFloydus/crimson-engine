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

const qhc::HostGestureDescriptor& require_gesture(std::string_view id)
{
	const qhc::HostGestureDescriptor *descriptor = qhc::find_gesture_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

bool has_tool(const qhc::HostGestureDescriptor& gesture, std::string_view tool_id)
{
	for (std::string_view candidate : gesture.tool_ids) {
		if (candidate == tool_id) {
			return true;
		}
	}
	return false;
}

bool has_pointer_input(const qhc::HostGestureDescriptor& gesture, qhc::HostPointerInputKind input)
{
	for (qhc::HostPointerInputKind candidate : gesture.pointer_inputs) {
		if (candidate == input) {
			return true;
		}
	}
	return false;
}

bool has_key_input(const qhc::HostGestureDescriptor& gesture, std::string_view key)
{
	for (std::string_view candidate : gesture.key_inputs) {
		if (candidate == key) {
			return true;
		}
	}
	return false;
}

bool has_modifier_meaning(const qhc::HostGestureDescriptor& gesture, std::string_view meaning)
{
	for (std::string_view candidate : gesture.modifier_meanings) {
		if (candidate == meaning) {
			return true;
		}
	}
	return false;
}

template <std::size_t Size>
void check_tool_order(
		const qhc::HostGestureDescriptor& gesture,
		const std::array<std::string_view, Size>& expected)
{
	EXPECT_TRUE(gesture.tool_ids.size() == expected.size());
	for (std::size_t i = 0; i < expected.size(); ++i) {
		EXPECT_TRUE(same_text(gesture.tool_ids[i], expected[i]));
	}
}

bool has_interaction_kind(std::string_view id, qhc::HostInteractionKind kind)
{
	const qhc::HostInteractionDescriptor *descriptor = qhc::find_interaction_descriptor(id);
	return descriptor != nullptr && descriptor->kind == kind;
}

} // namespace

TEST(QdrEditorMetadataGestureCatalogTests, HostContractGestureCatalogStaysDescriptorOnlyAndResolvesToolReferences) {
	const std::span<const qhc::HostGestureDescriptor> gestures = qhc::gesture_catalog();
	EXPECT_TRUE(gestures.size() == 15U);

	for (const qhc::HostGestureDescriptor& gesture : gestures) {
		EXPECT_FALSE(gesture.id.empty());
		EXPECT_FALSE(gesture.label.empty());
		EXPECT_FALSE(gesture.target_id.empty());

		for (std::string_view tool_id : gesture.tool_ids) {
			EXPECT_TRUE(qhc::find_tool_descriptor(tool_id) != nullptr);
		}

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
                  EXPECT_TRUE(has_interaction_kind(
                      gesture.target_id,
                      qhc::HostInteractionKind::ViewportAction));
                  break;
                case qhc::HostGestureTargetKind::PreviewAction:
                  EXPECT_TRUE(has_interaction_kind(
                      gesture.target_id,
                      qhc::HostInteractionKind::PreviewAction));
                  break;
                case qhc::HostGestureTargetKind::InputPolicy:
                  EXPECT_TRUE(has_interaction_kind(
                      gesture.target_id,
                      qhc::HostInteractionKind::InputPolicy));
                  break;
		}
	}
}

TEST(QdrEditorMetadataGestureCatalogTests, HostContractGestureCatalogPreservesViewportRoutingPriority) {
	const qhc::HostGestureDescriptor& active_tools = require_gesture("active_modal_tool_precedence");
	const qhc::HostGestureDescriptor& preview_block = require_gesture("active_preview_pointer_block");
	const qhc::HostGestureDescriptor& transform = require_gesture("transform_gizmo_drag");
	const qhc::HostGestureDescriptor& selection = require_gesture("selection_pick_and_paint");
	const qhc::HostGestureDescriptor& navigation = require_gesture("viewport_scroll_zoom");

	EXPECT_TRUE(active_tools.priority < preview_block.priority);
	EXPECT_TRUE(preview_block.priority < transform.priority);
	EXPECT_TRUE(transform.priority < selection.priority);
	EXPECT_TRUE(selection.priority < navigation.priority);

	EXPECT_TRUE(active_tools.requires_active_tool);
	EXPECT_TRUE(active_tools.consumes_pointer_when_active);
	EXPECT_TRUE(active_tools.suppresses_gizmo);
	check_tool_order(active_tools, std::array<std::string_view, 6> {{
		"create_primitive",
		"insert_edge_loop",
		"knife",
		"cut",
		"mirror",
		"poly",
	}});
}

TEST(QdrEditorMetadataGestureCatalogTests, HostContractGestureCatalogDescribesModalToolPointerPhasesAndLifecycleKeys) {
	const qhc::HostGestureDescriptor& primitive =
		require_gesture("create_primitive_gesture");
	EXPECT_TRUE(primitive.requires_active_tool);
	EXPECT_TRUE(primitive.consumes_pointer_when_active);
	EXPECT_TRUE(primitive.suppresses_gizmo);
	EXPECT_FALSE(primitive.returns_to_select_on_cancel);
	EXPECT_FALSE(primitive.returns_to_select_on_success);
	EXPECT_TRUE(primitive.rebuilds_mesh_on_success);
        EXPECT_TRUE(
            has_pointer_input(primitive, qhc::HostPointerInputKind::MouseMove));
        EXPECT_TRUE(
            has_pointer_input(primitive,
                              qhc::HostPointerInputKind::PrimaryDown));
        EXPECT_TRUE(
            has_pointer_input(primitive,
                              qhc::HostPointerInputKind::PrimaryDrag));
        EXPECT_TRUE(
            has_pointer_input(primitive,
                              qhc::HostPointerInputKind::PrimaryUp));
	EXPECT_TRUE(has_key_input(primitive, "Escape"));
	EXPECT_TRUE(has_tool(primitive, "create_primitive"));

	const qhc::HostGestureDescriptor& insert_edge_loop = require_gesture("insert_edge_loop_gesture");
	EXPECT_TRUE(insert_edge_loop.returns_to_select_on_cancel);
	EXPECT_TRUE(insert_edge_loop.returns_to_select_on_success);
	EXPECT_TRUE(has_modifier_meaning(insert_edge_loop, "primary_modifier:bypass_snap"));

	const qhc::HostGestureDescriptor& knife = require_gesture("knife_tool_gesture");
	EXPECT_TRUE(knife.returns_to_select_on_cancel);
	EXPECT_TRUE(knife.returns_to_select_on_success);
	EXPECT_TRUE(has_key_input(knife, "Return"));
	EXPECT_TRUE(has_key_input(knife, "KeypadEnter"));
	EXPECT_TRUE(has_key_input(knife, "Space"));
	EXPECT_TRUE(has_modifier_meaning(knife, "primary_modifier:bypass_snap"));
	EXPECT_TRUE(has_modifier_meaning(knife, "shift:constrain_axis"));
	EXPECT_TRUE(has_modifier_meaning(knife, "Return:finalize_and_start_new_operation"));
	EXPECT_TRUE(has_modifier_meaning(knife, "Space:finalize"));

	const qhc::HostGestureDescriptor& cut = require_gesture("cut_tool_gesture");
	EXPECT_TRUE(has_key_input(cut, "F"));
	EXPECT_TRUE(has_modifier_meaning(cut, "F:cycle_keep_mode"));
	EXPECT_TRUE(has_modifier_meaning(cut, "Space:finalize"));

	const qhc::HostGestureDescriptor& poly = require_gesture("poly_tool_gesture");
	EXPECT_TRUE(has_key_input(poly, "Space"));
	EXPECT_TRUE(has_modifier_meaning(poly, "primary_modifier:bypass_snap"));
	EXPECT_TRUE(has_modifier_meaning(poly, "shift:constrain_axis"));
	EXPECT_TRUE(has_modifier_meaning(poly, "Space:finalize"));
}

TEST(QdrEditorMetadataGestureCatalogTests, HostContractGestureCatalogDescribesPreviewAndTransformBlockingRules) {
	const qhc::HostGestureDescriptor& preview_block = require_gesture("active_preview_pointer_block");
	EXPECT_TRUE(preview_block.requires_active_preview);
	EXPECT_TRUE(preview_block.suppressed_while_fly_navigation);
	EXPECT_TRUE(preview_block.consumes_pointer_when_active);
	EXPECT_TRUE(preview_block.suppresses_gizmo);
        EXPECT_TRUE(has_pointer_input(preview_block,
                                      qhc::HostPointerInputKind::PrimaryDown));
        EXPECT_TRUE(has_pointer_input(preview_block,
                                      qhc::HostPointerInputKind::PrimaryDrag));
        EXPECT_TRUE(has_pointer_input(preview_block,
                                      qhc::HostPointerInputKind::PrimaryUp));

        const qhc::HostGestureDescriptor& transform = require_gesture("transform_gizmo_drag");
	EXPECT_TRUE(transform.blocked_by_active_preview);
	EXPECT_TRUE(transform.suppressed_while_fly_navigation);
	EXPECT_FALSE(transform.suppresses_gizmo);
	EXPECT_TRUE(transform.rebuilds_mesh_on_success);
	EXPECT_TRUE(has_tool(transform, "move"));
	EXPECT_TRUE(has_tool(transform, "rotate"));
	EXPECT_TRUE(has_tool(transform, "scale"));
	EXPECT_TRUE(has_modifier_meaning(transform, "shift:duplicate_object_selection"));
	EXPECT_TRUE(has_modifier_meaning(transform, "shift:extrude_or_inset_elements"));
	EXPECT_TRUE(has_modifier_meaning(transform, "primary_modifier:transform_snap_modifier"));

	const qhc::HostGestureDescriptor& selection = require_gesture("selection_pick_and_paint");
	EXPECT_TRUE(selection.blocked_by_active_preview);
	EXPECT_TRUE(selection.suppressed_while_fly_navigation);
	EXPECT_FALSE(selection.suppresses_gizmo);
	EXPECT_TRUE(has_modifier_meaning(selection, "primary_modifier:toggle_selection"));
	EXPECT_TRUE(has_modifier_meaning(selection, "shift:extend_selection"));
	EXPECT_TRUE(has_modifier_meaning(selection, "platform_modifier:multi_select"));
	EXPECT_TRUE(has_modifier_meaning(selection, "alt:alternate_pick"));
}

TEST(QdrEditorMetadataGestureCatalogTests, HostContractGestureCatalogDescribesViewportNavigationInputs) {
	const qhc::HostGestureDescriptor& zoom = require_gesture("viewport_scroll_zoom");
        EXPECT_TRUE(zoom.target_kind ==
                    qhc::HostGestureTargetKind::ViewportAction);
        EXPECT_TRUE(same_text(zoom.target_id, "zoom"));
	EXPECT_FALSE(zoom.suppresses_gizmo);
        EXPECT_TRUE(has_pointer_input(zoom, qhc::HostPointerInputKind::Scroll));
        EXPECT_TRUE(has_modifier_meaning(zoom, "primary_modifier:zoom_to_cursor"));

	const qhc::HostGestureDescriptor& fly = require_gesture("viewport_fly_or_orthographic_pan");
        EXPECT_TRUE(
            has_pointer_input(fly, qhc::HostPointerInputKind::SecondaryDown));
        EXPECT_TRUE(
            has_pointer_input(fly, qhc::HostPointerInputKind::SecondaryDrag));
        EXPECT_TRUE(
            has_pointer_input(fly, qhc::HostPointerInputKind::SecondaryUp));
        EXPECT_TRUE(has_key_input(fly, "Escape"));

	const qhc::HostGestureDescriptor& orbit = require_gesture("viewport_orbit_or_pan");
        EXPECT_TRUE(
            has_pointer_input(orbit, qhc::HostPointerInputKind::MiddleDown));
        EXPECT_TRUE(
            has_pointer_input(orbit, qhc::HostPointerInputKind::MiddleDrag));
        EXPECT_TRUE(
            has_pointer_input(orbit, qhc::HostPointerInputKind::MiddleUp));
        EXPECT_TRUE(has_modifier_meaning(orbit, "shift:pan"));
	EXPECT_TRUE(has_modifier_meaning(orbit, "alt:orbit_snap_modifier"));
}

TEST(QdrEditorMetadataGestureCatalogTests, HostContractGestureCatalogDescribesKeyOnlyPreviewAndLayoutBehavior) {
	const qhc::HostGestureDescriptor& preview = require_gesture("active_preview_commit_or_cancel");
	EXPECT_TRUE(preview.requires_active_preview);
	EXPECT_TRUE(preview.suppressed_while_fly_navigation);
	EXPECT_TRUE(preview.suppresses_gizmo);
	EXPECT_TRUE(preview.rebuilds_mesh_on_success);
	EXPECT_TRUE(has_key_input(preview, "Escape"));
	EXPECT_TRUE(has_key_input(preview, "Space"));

	const qhc::HostGestureDescriptor& layout = require_gesture("focused_pane_maximize_toggle");
        EXPECT_TRUE(layout.target_kind ==
                    qhc::HostGestureTargetKind::ViewportAction);
        EXPECT_TRUE(same_text(layout.target_id, "toggle_focused_pane_maximized"));
	EXPECT_TRUE(layout.suppressed_while_fly_navigation);
	EXPECT_TRUE(has_key_input(layout, "Space"));
}
