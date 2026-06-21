#include <catch2/catch_test_macros.hpp>

#include <quader_host_contract/host_contract.h>

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
	REQUIRE(descriptor != nullptr);
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
	REQUIRE(gesture.tool_ids.size() == expected.size());
	for (std::size_t i = 0; i < expected.size(); ++i) {
		CHECK(same_text(gesture.tool_ids[i], expected[i]));
	}
}

bool has_interaction_kind(std::string_view id, qhc::HostInteractionKind kind)
{
	const qhc::HostInteractionDescriptor *descriptor = qhc::find_interaction_descriptor(id);
	return descriptor != nullptr && descriptor->kind == kind;
}

} // namespace

TEST_CASE("host contract gesture catalog stays descriptor-only and resolves tool references", "[host-contract]")
{
	const std::span<const qhc::HostGestureDescriptor> gestures = qhc::gesture_catalog();
	REQUIRE(gestures.size() == 15U);

	for (const qhc::HostGestureDescriptor& gesture : gestures) {
		CHECK_FALSE(gesture.id.empty());
		CHECK_FALSE(gesture.label.empty());
		CHECK_FALSE(gesture.target_id.empty());

		for (std::string_view tool_id : gesture.tool_ids) {
			CHECK(qhc::find_tool_descriptor(tool_id) != nullptr);
		}

		switch (gesture.target_kind) {
			case qhc::HostGestureTargetKind::tool:
				CHECK(qhc::find_tool_descriptor(gesture.target_id) != nullptr);
				break;
			case qhc::HostGestureTargetKind::tool_group:
				CHECK(qhc::find_tool_group_descriptor(gesture.target_id) != nullptr);
				break;
			case qhc::HostGestureTargetKind::viewport_action:
				CHECK(has_interaction_kind(gesture.target_id, qhc::HostInteractionKind::viewport_action));
				break;
			case qhc::HostGestureTargetKind::preview_action:
				CHECK(has_interaction_kind(gesture.target_id, qhc::HostInteractionKind::preview_action));
				break;
			case qhc::HostGestureTargetKind::input_policy:
				CHECK(has_interaction_kind(gesture.target_id, qhc::HostInteractionKind::input_policy));
				break;
		}
	}
}

TEST_CASE("host contract gesture catalog preserves viewport routing priority", "[host-contract]")
{
	const qhc::HostGestureDescriptor& active_tools = require_gesture("active_modal_tool_precedence");
	const qhc::HostGestureDescriptor& preview_block = require_gesture("active_preview_pointer_block");
	const qhc::HostGestureDescriptor& transform = require_gesture("transform_gizmo_drag");
	const qhc::HostGestureDescriptor& selection = require_gesture("selection_pick_and_paint");
	const qhc::HostGestureDescriptor& navigation = require_gesture("viewport_scroll_zoom");

	CHECK(active_tools.priority < preview_block.priority);
	CHECK(preview_block.priority < transform.priority);
	CHECK(transform.priority < selection.priority);
	CHECK(selection.priority < navigation.priority);

	CHECK(active_tools.requires_active_tool);
	CHECK(active_tools.consumes_pointer_when_active);
	CHECK(active_tools.suppresses_gizmo_and_measurements);
	check_tool_order(active_tools, std::array<std::string_view, 6> {{
		"box",
		"insert_edge_loop",
		"knife",
		"cut",
		"mirror",
		"poly",
	}});
}

TEST_CASE("host contract gesture catalog describes modal tool pointer phases and lifecycle keys", "[host-contract]")
{
	const qhc::HostGestureDescriptor& box = require_gesture("box_tool_gesture");
	CHECK(box.requires_active_tool);
	CHECK(box.consumes_pointer_when_active);
	CHECK(box.suppresses_gizmo_and_measurements);
	CHECK_FALSE(box.returns_to_select_on_cancel);
	CHECK_FALSE(box.returns_to_select_on_success);
	CHECK(box.rebuilds_mesh_on_success);
	CHECK(has_pointer_input(box, qhc::HostPointerInputKind::mouse_move));
	CHECK(has_pointer_input(box, qhc::HostPointerInputKind::primary_down));
	CHECK(has_pointer_input(box, qhc::HostPointerInputKind::primary_drag));
	CHECK(has_pointer_input(box, qhc::HostPointerInputKind::primary_up));
	CHECK(has_key_input(box, "Escape"));

	const qhc::HostGestureDescriptor& insert_edge_loop = require_gesture("insert_edge_loop_gesture");
	CHECK(insert_edge_loop.returns_to_select_on_cancel);
	CHECK_FALSE(insert_edge_loop.returns_to_select_on_success);
	CHECK(has_modifier_meaning(insert_edge_loop, "primary_modifier:bypass_snap"));

	const qhc::HostGestureDescriptor& knife = require_gesture("knife_tool_gesture");
	CHECK(knife.returns_to_select_on_cancel);
	CHECK(knife.returns_to_select_on_success);
	CHECK(has_key_input(knife, "Return"));
	CHECK(has_key_input(knife, "KeypadEnter"));
	CHECK(has_key_input(knife, "Space"));
	CHECK(has_modifier_meaning(knife, "primary_modifier:bypass_snap"));
	CHECK(has_modifier_meaning(knife, "shift:constrain_axis"));
	CHECK(has_modifier_meaning(knife, "Return:finalize_and_start_new_operation"));
	CHECK(has_modifier_meaning(knife, "Space:finalize"));

	const qhc::HostGestureDescriptor& cut = require_gesture("cut_tool_gesture");
	CHECK(has_key_input(cut, "F"));
	CHECK(has_modifier_meaning(cut, "F:cycle_keep_mode"));
	CHECK(has_modifier_meaning(cut, "Space:finalize"));

	const qhc::HostGestureDescriptor& poly = require_gesture("poly_tool_gesture");
	CHECK(has_key_input(poly, "Space"));
	CHECK(has_modifier_meaning(poly, "primary_modifier:bypass_snap"));
	CHECK(has_modifier_meaning(poly, "shift:constrain_axis"));
	CHECK(has_modifier_meaning(poly, "Space:finalize"));
}

TEST_CASE("host contract gesture catalog describes preview and transform blocking rules", "[host-contract]")
{
	const qhc::HostGestureDescriptor& preview_block = require_gesture("active_preview_pointer_block");
	CHECK(preview_block.requires_active_preview);
	CHECK(preview_block.suppressed_while_fly_navigation);
	CHECK(preview_block.consumes_pointer_when_active);
	CHECK(preview_block.suppresses_gizmo_and_measurements);
	CHECK(has_pointer_input(preview_block, qhc::HostPointerInputKind::primary_down));
	CHECK(has_pointer_input(preview_block, qhc::HostPointerInputKind::primary_drag));
	CHECK(has_pointer_input(preview_block, qhc::HostPointerInputKind::primary_up));

	const qhc::HostGestureDescriptor& transform = require_gesture("transform_gizmo_drag");
	CHECK(transform.blocked_by_active_preview);
	CHECK(transform.suppressed_while_fly_navigation);
	CHECK_FALSE(transform.suppresses_gizmo_and_measurements);
	CHECK(transform.rebuilds_mesh_on_success);
	CHECK(has_tool(transform, "move"));
	CHECK(has_tool(transform, "rotate"));
	CHECK(has_tool(transform, "scale"));
	CHECK(has_modifier_meaning(transform, "shift:duplicate_object_selection"));
	CHECK(has_modifier_meaning(transform, "shift:extrude_or_inset_elements"));
	CHECK(has_modifier_meaning(transform, "primary_modifier:transform_snap_modifier"));

	const qhc::HostGestureDescriptor& selection = require_gesture("selection_pick_and_paint");
	CHECK(selection.blocked_by_active_preview);
	CHECK(selection.suppressed_while_fly_navigation);
	CHECK_FALSE(selection.suppresses_gizmo_and_measurements);
	CHECK(has_modifier_meaning(selection, "primary_modifier:toggle_selection"));
	CHECK(has_modifier_meaning(selection, "shift:extend_selection"));
	CHECK(has_modifier_meaning(selection, "platform_modifier:multi_select"));
	CHECK(has_modifier_meaning(selection, "alt:alternate_pick"));
}

TEST_CASE("host contract gesture catalog describes viewport navigation inputs", "[host-contract]")
{
	const qhc::HostGestureDescriptor& zoom = require_gesture("viewport_scroll_zoom");
	CHECK(zoom.target_kind == qhc::HostGestureTargetKind::viewport_action);
	CHECK(same_text(zoom.target_id, "zoom"));
	CHECK_FALSE(zoom.suppresses_gizmo_and_measurements);
	CHECK(has_pointer_input(zoom, qhc::HostPointerInputKind::scroll));
	CHECK(has_modifier_meaning(zoom, "primary_modifier:zoom_to_cursor"));

	const qhc::HostGestureDescriptor& fly = require_gesture("viewport_fly_or_orthographic_pan");
	CHECK(has_pointer_input(fly, qhc::HostPointerInputKind::secondary_down));
	CHECK(has_pointer_input(fly, qhc::HostPointerInputKind::secondary_drag));
	CHECK(has_pointer_input(fly, qhc::HostPointerInputKind::secondary_up));
	CHECK(has_key_input(fly, "Escape"));

	const qhc::HostGestureDescriptor& orbit = require_gesture("viewport_orbit_or_pan");
	CHECK(has_pointer_input(orbit, qhc::HostPointerInputKind::middle_down));
	CHECK(has_pointer_input(orbit, qhc::HostPointerInputKind::middle_drag));
	CHECK(has_pointer_input(orbit, qhc::HostPointerInputKind::middle_up));
	CHECK(has_modifier_meaning(orbit, "shift:pan"));
	CHECK(has_modifier_meaning(orbit, "alt:orbit_snap_modifier"));
}

TEST_CASE("host contract gesture catalog describes key-only preview and layout behavior", "[host-contract]")
{
	const qhc::HostGestureDescriptor& preview = require_gesture("active_preview_commit_or_cancel");
	CHECK(preview.requires_active_preview);
	CHECK(preview.suppressed_while_fly_navigation);
	CHECK(preview.suppresses_gizmo_and_measurements);
	CHECK(preview.rebuilds_mesh_on_success);
	CHECK(has_key_input(preview, "Escape"));
	CHECK(has_key_input(preview, "Space"));

	const qhc::HostGestureDescriptor& layout = require_gesture("focused_pane_maximize_toggle");
	CHECK(layout.target_kind == qhc::HostGestureTargetKind::viewport_action);
	CHECK(same_text(layout.target_id, "toggle_focused_pane_maximized"));
	CHECK(layout.suppressed_while_fly_navigation);
	CHECK(has_key_input(layout, "Space"));
}
