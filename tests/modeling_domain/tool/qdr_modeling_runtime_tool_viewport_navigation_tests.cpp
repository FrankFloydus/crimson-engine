////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/interaction_api.h>

TEST_CASE("viewport navigation state tracks orbit pan and fly ownership", "[editor-core][tool][navigation]")
{
	quader_editor_core::ViewportNavigationState state;
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, false);
	CHECK(quader_editor_core::viewport_navigation_is_orbit(state));
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_pan(state));
	CHECK(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, true);
	CHECK(quader_editor_core::viewport_navigation_is_pan(state));
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_orbit(state));

	quader_editor_core::begin_viewport_fly_navigation(state, 77, "main");
	CHECK(quader_editor_core::viewport_navigation_is_fly(state));
	CHECK(quader_editor_core::viewport_navigation_is_fly_camera(state, 77));
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_fly_camera(state, 78));

	quader_editor_core::end_viewport_navigation(state);
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_active(state));
}

TEST_CASE("viewport motion planner routes active tool drags before hover", "[editor-core][tool][navigation]")
{
	quader_editor_core::ViewportInteractionState state;
	state.polygon_session = true;
	state.active_tool = quader_editor_core::ViewportInteractionTool::box;
	state.box_dragging_base = true;

	quader_editor_core::ViewportMotionInput motion;
	motion.left_down = true;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::polygon_update_box_base);

	state.box_dragging_base = false;
	state.box_height_preview = true;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::polygon_update_box_height);

	state.box_height_preview = false;
	state.active_tool = quader_editor_core::ViewportInteractionTool::select;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::update_hover);

	state.polygon_session = false;
	state.selection_box_active = true;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::update_selection_box);
}

TEST_CASE("viewport motion planner owns fly box cancel and pan consume policy", "[editor-core][tool][navigation]")
{
	quader_editor_core::ViewportInteractionState state;
	quader_editor_core::ViewportMotionInput motion;
	state.fly_active = true;
	motion.right_down = true;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::update_fly_look);

	motion.right_down = false;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::end_fly);

	state = {};
	motion = {};
	state.polygon_session = true;
	state.box_dragging_base = true;
	motion.left_down = false;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::polygon_cancel_box_base);

	state = {};
	motion = {};
	state.navigating = true;
	state.navigation_mode = quader_editor_core::ViewportNavigationMode::pan;
	motion.middle_down = true;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::update_pan);

	motion.relative_is_zero = true;
	CHECK(quader_editor_core::plan_viewport_motion(state, motion) ==
		quader_editor_core::ViewportInteractionAction::consume);
}

TEST_CASE("viewport button planner routes polygon tools transform fallbacks and extrude cuts", "[editor-core][tool][navigation]")
{
	quader_editor_core::ViewportInteractionState state;
	quader_editor_core::ViewportButtonInput button;
	button.button = quader_editor_core::ViewportMouseButton::left;
	button.pressed = true;

	state.polygon_session = true;
	state.active_tool = quader_editor_core::ViewportInteractionTool::box;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::polygon_begin_box_base);

	state.box_height_preview = true;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::polygon_commit_box_height);

	state = {};
	state.polygon_session = true;
	state.active_tool = quader_editor_core::ViewportInteractionTool::move;
	state.has_selection = true;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::polygon_begin_move_drag);

	state.active_tool = quader_editor_core::ViewportInteractionTool::select;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::polygon_pick_select);

	state = {};
	state.active_tool = quader_editor_core::ViewportInteractionTool::rotate;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::route_transform_button);

	state.active_tool = quader_editor_core::ViewportInteractionTool::select;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::route_select_button);

	button.button = quader_editor_core::ViewportMouseButton::wheel_up;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::zoom);

	state.move_drag_active = true;
	state.move_drag_tool = quader_editor_core::ModalMoveDragTool::extrude;
	state.extrude_tool_active = true;
	CHECK(quader_editor_core::plan_viewport_button(state, button) ==
		quader_editor_core::ViewportInteractionAction::adjust_extrude_cuts);
}
