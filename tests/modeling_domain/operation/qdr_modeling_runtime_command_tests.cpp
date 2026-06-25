////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <catch2/catch_approx.hpp>

#include <editor/modeling/runtime/runtime_command.h>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace qec = quader_editor_core;

namespace {

qec::EditorRuntimeCommand copy_command()
{
	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::copy_selected_meshes;
	command.settings = std::monostate {};
	return command;
}

qec::EditorRuntimeCommand no_settings_command(qec::EditorRuntimeCommandId command_id)
{
	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = std::monostate {};
	return command;
}

qec::EditorRuntimeCommand paste_command(std::optional<quader::QVec3> target)
{
	qec::PasteCopiedMeshesRuntimeCommandSettings settings;
	if (target.has_value()) {
		settings.target_center = { *target, true };
	}

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::paste_copied_meshes;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand set_selection_mode_command(int mode)
{
	qec::SetSelectionModeRuntimeCommandSettings settings;
	settings.selection_mode = mode;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::set_selection_mode;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand set_active_tool_command(qec::ViewportInteractionTool tool)
{
	qec::SetActiveToolRuntimeCommandSettings settings;
	settings.tool = tool;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::set_active_tool;
	command.settings = settings;
	return command;
}

quader_poly::Ray front_center_ray()
{
	return { { 0.0F, 0.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

qec::EditorRuntimeCommand pick_select_command(
		quader_poly::Ray ray = front_center_ray(),
		int click_count = 1,
		bool control_pressed = false,
		bool shift_pressed = false)
{
	qec::PickSelectRuntimeCommandSettings settings;
	settings.ray = ray;
	settings.click_count = click_count;
	settings.control_pressed = control_pressed;
	settings.shift_pressed = shift_pressed;
	settings.vertex_radius = 0.12;
	settings.edge_radius = 0.10;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::pick_select;
	command.settings = settings;
	return command;
}

quader_poly::Ray front_right_edge_ray(float y = 0.0F)
{
	return { { 1.0F, y, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

quader_poly::Ray top_center_ray()
{
	return { { 0.0F, 5.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
}

quader_poly::Ray front_right_top_vertex_ray()
{
	return { { 1.0F, 1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

qec::EditorRuntimeCommand insert_edge_loop_pick_command(
		qec::EditorRuntimeCommandId command_id,
		quader_poly::Ray ray = front_right_edge_ray(),
		bool snap_enabled = false)
{
	qec::InsertEdgeLoopPickRuntimeCommandSettings settings;
	settings.ray = ray;
	settings.grid_size = 1.0;
	settings.snap_enabled = snap_enabled;

	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand set_cut_mode_command(int mode)
{
	qec::SetCutModeRuntimeCommandSettings settings;
	settings.mode = mode;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::set_cut_mode;
	command.settings = settings;
	return command;
}

quader_poly::Ray cut_ray(float x, float y = 2.0F)
{
	return { { x, y, 0.0F }, { 0.0F, -1.0F, 0.0F } };
}

qec::ViewportCameraState cut_camera()
{
	qec::ViewportCameraState camera;
	camera.projection = qec::CameraProjection::perspective;
	return camera;
}

qec::EditorRuntimeCommand cut_ray_command(
		qec::EditorRuntimeCommandId command_id,
		quader_poly::Ray ray,
		bool dragging = false)
{
	qec::CutRayRuntimeCommandSettings settings;
	settings.camera = cut_camera();
	settings.ray = ray;
	settings.grid_size = 0.5;
	settings.dragging = dragging;

	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand cut_point_drag_command(int point_index)
{
	qec::CutPointDragRuntimeCommandSettings settings;
	settings.point_index = point_index;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::begin_cut_point_drag;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand mirror_ray_command(
		qec::EditorRuntimeCommandId command_id,
		quader_poly::Ray ray,
		bool dragging = false)
{
	qec::MirrorRayRuntimeCommandSettings settings;
	settings.camera = cut_camera();
	settings.ray = ray;
	settings.grid_size = 0.5;
	settings.dragging = dragging;

	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand mirror_point_drag_command(int point_index)
{
	qec::MirrorPointDragRuntimeCommandSettings settings;
	settings.point_index = point_index;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::begin_mirror_point_drag;
	command.settings = settings;
	return command;
}

quader_poly::Ray box_ground_ray(float x = 4.0F, float z = 4.0F)
{
	return { { x, 4.0F, z }, { 0.0F, -1.0F, 0.0F } };
}

qec::EditorRuntimeCommand box_ray_command(
		qec::EditorRuntimeCommandId command_id,
		quader_poly::Ray ray = box_ground_ray())
{
	qec::BoxRayRuntimeCommandSettings settings;
	settings.ray = ray;
	settings.grid_size = 1.0;

	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand box_height_command(
		qec::EditorRuntimeCommandId command_id,
		quader::QVec2 screen_position = { 400.0F, 300.0F },
		quader_poly::Ray ray = box_ground_ray())
{
	qec::BoxHeightRuntimeCommandSettings settings;
	settings.screen_position = screen_position;
	settings.ray = ray;
	settings.grid_size = 1.0;

	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand create_box_from_bounds_command(
		quader::QVec3 min_bounds = { -2.0F, -1.0F, -3.0F },
		quader::QVec3 max_bounds = { 2.0F, 1.0F, 3.0F })
{
	qec::CreateBoxFromBoundsRuntimeCommandSettings settings;
	settings.min_bounds = min_bounds;
	settings.max_bounds = max_bounds;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::create_box_from_bounds;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand mirror_axis_command(qec::TransformAxis axis)
{
	qec::MirrorAxisRuntimeCommandSettings settings;
	settings.axis = axis;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::mirror_axis;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand flip_view_command(qec::ViewportFlipOrientation orientation)
{
	qec::ViewFlipRuntimeCommandSettings settings;
	settings.orientation = orientation;

	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::flip_view;
	command.settings = settings;
	return command;
}

} // namespace

using qdr_test::selected_edge_count;
using qdr_test::selected_face_count;
using qdr_test::selected_vertex_count;

TEST_CASE("editor runtime command facade copies selected meshes without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, copy_command());

	REQUIRE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(runtime.clipboard_available());
	REQUIRE(result.runtime.clipboard_available);
	REQUIRE(result.runtime.clipboard_object_count == 1U);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade reports failed copy without object selection", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, copy_command());

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Select one or more meshes in object mode before copying.");
	REQUIRE_FALSE(runtime.clipboard_available());
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade handles selection workflow commands without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult clear_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::clear_selection));
	REQUIRE(clear_result.command.ok);
	REQUIRE(clear_result.command.changed);
	REQUIRE_FALSE(session.has_selection());
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult select_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::select_all));
	REQUIRE(select_result.command.ok);
	REQUIRE(select_result.command.changed);
	REQUIRE(session.has_selection());
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult invert_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::invert_selection));
	REQUIRE(invert_result.command.ok);
	REQUIRE(invert_result.command.changed);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade handles delete selection through history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::size_t object_count_before = session.objects().size();

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::delete_selection));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() < object_count_before);
	REQUIRE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	REQUIRE(undo.applied);
	REQUIRE(session.objects().size() == object_count_before);
}

TEST_CASE("editor runtime command facade reports delete with no selection as unchanged success", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::delete_selection));

	REQUIRE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message.empty());
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade pastes copied meshes through history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	REQUIRE(qec::execute_runtime_command(session, runtime, copy_command()).command.ok);

	const std::size_t object_count_before = session.objects().size();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, paste_command(quader::QVec3 { 3.0F, 0.0F, 3.0F }));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() == object_count_before + 1U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	REQUIRE(session.selected_object_ids().front() != 1);
	REQUIRE(session.state().content_revision != content_revision_before);
	REQUIRE(session.state().selection_revision != selection_revision_before);
	REQUIRE(runtime.clipboard_available());
	REQUIRE(result.runtime.clipboard_available);
	REQUIRE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	REQUIRE(undo.applied);
	REQUIRE(session.objects().size() == object_count_before);
}

TEST_CASE("editor runtime command facade pastes without target at current selection center", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	REQUIRE(qec::execute_runtime_command(session, runtime, copy_command()).command.ok);
	REQUIRE(session.translate_selected({ 5.0F, 0.0F, 0.0F }).changed);

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, paste_command(std::nullopt));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() == 2U);
	const qec::Bounds3 selected_bounds = session.selection_bounds();
	REQUIRE_FALSE(selected_bounds.empty);
	REQUIRE((selected_bounds.min.x + selected_bounds.max.x) * 0.5F == Catch::Approx(5.0F));
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade reports empty paste without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, paste_command(std::nullopt));

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Copy one or more meshes before pasting.");
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade marks the current document clean", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	REQUIRE(session.dirty());

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::mark_clean));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(session.dirty());

	const qec::EditorRuntimeCommandResult unchanged_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::mark_clean));
	REQUIRE(unchanged_result.command.ok);
	REQUIRE_FALSE(unchanged_result.command.changed);
	REQUIRE_FALSE(session.dirty());
}

TEST_CASE("editor runtime command facade restores undo and redo history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::size_t object_count_before = session.objects().size();

	const qec::EditorRuntimeCommandResult delete_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::delete_selection));
	REQUIRE(delete_result.command.ok);
	REQUIRE(delete_result.command.changed);
	REQUIRE(session.objects().empty());
	REQUIRE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult undo_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::undo));
	REQUIRE(undo_result.command.ok);
	REQUIRE(undo_result.command.changed);
	REQUIRE(session.objects().size() == object_count_before);
	REQUIRE_FALSE(session.history_status().can_undo);
	REQUIRE(session.history_status().can_redo);

	const qec::EditorRuntimeCommandResult redo_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::redo));
	REQUIRE(redo_result.command.ok);
	REQUIRE(redo_result.command.changed);
	REQUIRE(session.objects().empty());
	REQUIRE(session.history_status().can_undo);
	REQUIRE_FALSE(session.history_status().can_redo);
}

TEST_CASE("editor runtime command facade reports empty undo and redo as unchanged success", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult undo_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::undo));
	REQUIRE(undo_result.command.ok);
	REQUIRE_FALSE(undo_result.command.changed);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);

	const qec::EditorRuntimeCommandResult redo_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::redo));
	REQUIRE(redo_result.command.ok);
	REQUIRE_FALSE(redo_result.command.changed);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade clears hover without document mutation", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::SelectionHoverState hover;
	hover.object_id = 1;
	hover.kind = qec::SelectionHoverKind::object;
	hover.candidate_object_id = 1;
	runtime.set_hover(hover);
	REQUIRE(runtime.status().hover_has_candidate_object);
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::clear_hover));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(runtime.status().hover_active);
	REQUIRE_FALSE(result.runtime.hover_active);
	REQUIRE_FALSE(runtime.status().hover_has_candidate_object);
	REQUIRE_FALSE(result.runtime.hover_has_candidate_object);
	REQUIRE(runtime.hover().kind == qec::SelectionHoverKind::none);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult unchanged_result =
		qec::execute_runtime_command(session, runtime, no_settings_command(qec::EditorRuntimeCommandId::clear_hover));
	REQUIRE(unchanged_result.command.ok);
	REQUIRE_FALSE(unchanged_result.command.changed);
	REQUIRE_FALSE(unchanged_result.runtime.hover_active);
}

TEST_CASE("editor runtime command facade sets selection mode without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult face_result =
		qec::execute_runtime_command(session, runtime, set_selection_mode_command(static_cast<int>(qec::SelectionMode::face)));

	REQUIRE(face_result.command.ok);
	REQUIRE(face_result.command.changed);
	REQUIRE(session.selection_mode() == qec::SelectionMode::face);
	REQUIRE(face_result.command.changed == (session.state().selection_revision != selection_revision_before));
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const std::uint64_t face_selection_revision = session.state().selection_revision;
	const qec::EditorRuntimeCommandResult same_result =
		qec::execute_runtime_command(session, runtime, set_selection_mode_command(static_cast<int>(qec::SelectionMode::face)));
	REQUIRE(same_result.command.ok);
	REQUIRE_FALSE(same_result.command.changed);
	REQUIRE(session.selection_mode() == qec::SelectionMode::face);
	REQUIRE(session.state().selection_revision == face_selection_revision);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult sanitized_result =
		qec::execute_runtime_command(session, runtime, set_selection_mode_command(9999));
	REQUIRE(sanitized_result.command.ok);
	REQUIRE(sanitized_result.command.changed);
	REQUIRE(session.selection_mode() == qec::SelectionMode::object);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult low_sanitized_result =
		qec::execute_runtime_command(session, runtime, set_selection_mode_command(-9999));
	REQUIRE(low_sanitized_result.command.ok);
	REQUIRE(low_sanitized_result.command.changed);
	REQUIRE(session.selection_mode() == qec::SelectionMode::vertex);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects set selection mode with mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::set_selection_mode;
	command.settings = std::monostate {};
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Runtime command settings do not match the command id.");
	REQUIRE(session.selection_mode() == qec::SelectionMode::object);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade sets active tool without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult move_result =
		qec::execute_runtime_command(session, runtime, set_active_tool_command(qec::ViewportInteractionTool::move));

	REQUIRE(move_result.command.ok);
	REQUIRE(move_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::move);
	REQUIRE(move_result.runtime.active_tool == qec::ViewportInteractionTool::move);
	REQUIRE(session.selection_mode() == qec::SelectionMode::object);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult same_result =
		qec::execute_runtime_command(session, runtime, set_active_tool_command(qec::ViewportInteractionTool::move));
	REQUIRE(same_result.command.ok);
	REQUIRE_FALSE(same_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::move);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult edge_loop_result =
		qec::execute_runtime_command(session, runtime, set_active_tool_command(qec::ViewportInteractionTool::insert_edge_loop));
	REQUIRE(edge_loop_result.command.ok);
	REQUIRE(edge_loop_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::insert_edge_loop);
	REQUIRE(edge_loop_result.runtime.active_tool == qec::ViewportInteractionTool::insert_edge_loop);
	REQUIRE(edge_loop_result.runtime.insert_edge_loop_active);
	REQUIRE(session.selection_mode() == qec::SelectionMode::edge);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision != selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult sanitized_result =
		qec::execute_runtime_command(session, runtime, set_active_tool_command(qec::ViewportInteractionTool::unknown));
	REQUIRE(sanitized_result.command.ok);
	REQUIRE(sanitized_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE(sanitized_result.runtime.active_tool == qec::ViewportInteractionTool::select);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects selected-object tool activation without selection", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
	runtime.state().active_tool = qec::ViewportInteractionTool::move;
	runtime.state().transform_drag.active = true;
	runtime.state().transform_drag.axis = qec::TransformAxis::x;
	runtime.state().gizmo_hover_axis = qec::TransformAxis::x;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult cut_result =
		qec::execute_runtime_command(session, runtime, set_active_tool_command(qec::ViewportInteractionTool::cut));
	REQUIRE_FALSE(cut_result.command.ok);
	REQUIRE_FALSE(cut_result.command.changed);
	REQUIRE(cut_result.command.message == "Select one or more mesh objects to cut.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::move);
	REQUIRE(runtime.state().transform_drag.active);
	REQUIRE(runtime.state().gizmo_hover_axis == qec::TransformAxis::x);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);

	const qec::EditorRuntimeCommandResult mirror_result =
		qec::execute_runtime_command(session, runtime, set_active_tool_command(qec::ViewportInteractionTool::mirror));
	REQUIRE_FALSE(mirror_result.command.ok);
	REQUIRE_FALSE(mirror_result.command.changed);
	REQUIRE(mirror_result.command.message == "Select one or more mesh objects to mirror.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::move);
	REQUIRE(runtime.state().transform_drag.active);
	REQUIRE(runtime.state().gizmo_hover_axis == qec::TransformAxis::x);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade rejects set active tool with mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::set_active_tool;
	command.settings = std::monostate {};
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Runtime command settings do not match the command id.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE(session.selection_mode() == qec::SelectionMode::object);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade drives insert edge loop preview and commit", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult hover_result =
		qec::execute_runtime_command(
			session,
			runtime,
			insert_edge_loop_pick_command(qec::EditorRuntimeCommandId::update_insert_edge_loop_hover));
	REQUIRE(hover_result.command.ok);
	REQUIRE(hover_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::insert_edge_loop);
	REQUIRE(session.selection_mode() == qec::SelectionMode::edge);
	REQUIRE(runtime.insert_edge_loop_state().active);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().dragging);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	REQUIRE_FALSE(session.history_status().can_undo);

	const float hover_factor = runtime.insert_edge_loop_state().factor;
	const qec::EditorRuntimeCommandResult begin_result =
		qec::execute_runtime_command(
			session,
			runtime,
			insert_edge_loop_pick_command(qec::EditorRuntimeCommandId::begin_insert_edge_loop_drag));
	REQUIRE(begin_result.command.ok);
	REQUIRE(begin_result.command.changed);
	REQUIRE(runtime.insert_edge_loop_state().dragging);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	REQUIRE(runtime.insert_edge_loop_state().factor == Catch::Approx(hover_factor));
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult release_hover_result =
		qec::execute_runtime_command(
			session,
			runtime,
			insert_edge_loop_pick_command(qec::EditorRuntimeCommandId::update_insert_edge_loop_hover));
	REQUIRE(release_hover_result.command.ok);
	REQUIRE(runtime.insert_edge_loop_state().dragging);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult update_result =
		qec::execute_runtime_command(
			session,
			runtime,
			insert_edge_loop_pick_command(
				qec::EditorRuntimeCommandId::update_insert_edge_loop_drag,
				front_right_edge_ray(0.25F)));
	REQUIRE(update_result.command.ok);
	REQUIRE(runtime.insert_edge_loop_state().dragging);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult commit_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::commit_insert_edge_loop_drag));
	REQUIRE(commit_result.command.ok);
	REQUIRE(commit_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().active);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().dragging);
	REQUIRE(runtime.insert_edge_loop_state().preview_lines.empty());
	REQUIRE(session.selection_mode() == qec::SelectionMode::edge);
	REQUIRE(session.state().content_revision != content_revision_before);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade cancels insert edge loop preview without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult begin_result =
		qec::execute_runtime_command(
			session,
			runtime,
			insert_edge_loop_pick_command(qec::EditorRuntimeCommandId::begin_insert_edge_loop_drag));
	REQUIRE(begin_result.command.ok);
	REQUIRE(runtime.insert_edge_loop_state().active);
	REQUIRE(runtime.insert_edge_loop_state().dragging);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());

	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::cancel_insert_edge_loop));
	REQUIRE(cancel_result.command.ok);
	REQUIRE(cancel_result.command.changed);
	REQUIRE(runtime.insert_edge_loop_state().active);
	REQUIRE_FALSE(runtime.insert_edge_loop_state().dragging);
	REQUIRE(runtime.insert_edge_loop_state().preview_lines.empty());
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade reports insert edge loop commit without a drag", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::commit_insert_edge_loop_drag));
	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Select an edge to insert an edge loop.");
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects insert edge loop mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult pick_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::update_insert_edge_loop_hover));
	REQUIRE_FALSE(pick_result.command.ok);
	REQUIRE_FALSE(pick_result.command.changed);
	REQUIRE(pick_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand cancel_command;
	cancel_command.command_id = qec::EditorRuntimeCommandId::cancel_insert_edge_loop;
	cancel_command.settings = qec::InsertEdgeLoopPickRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(session, runtime, cancel_command);
	REQUIRE_FALSE(cancel_result.command.ok);
	REQUIRE_FALSE(cancel_result.command.changed);
	REQUIRE(cancel_result.command.message == "Runtime command settings do not match the command id.");
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade mirrors selected meshes around a world axis through history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const int source_id = session.objects().front().id;
	const quader::QVec3 first_before = session.objects().front().document.vertices.front().position;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, mirror_axis_command(qec::TransformAxis::x));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() == 2U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	const int copy_id = session.selected_object_ids().front();
	REQUIRE(copy_id != source_id);
	REQUIRE(session.history_status().can_undo);

	const auto copy_it = std::find_if(session.objects().begin(), session.objects().end(), [copy_id](const qec::PolygonObject& object) {
		return object.id == copy_id;
	});
	REQUIRE(copy_it != session.objects().end());
	REQUIRE_FALSE(copy_it->document.vertices.empty());
	CHECK(copy_it->document.vertices.front().position.x == Catch::Approx(-first_before.x));

	const qec::HistoryRestoreResult undo = session.undo();
	REQUIRE(undo.applied);
	REQUIRE(session.objects().size() == 1U);
}

TEST_CASE("editor runtime command facade flips selected meshes in place through history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const int source_id = session.objects().front().id;
	const quader::QVec3 first_before = session.objects().front().document.vertices.front().position;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, flip_view_command(qec::ViewportFlipOrientation::horizontal));

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	CHECK(session.selected_object_ids().front() == source_id);
	REQUIRE_FALSE(session.objects().front().document.vertices.empty());
	CHECK(session.objects().front().document.vertices.front().position.x == Catch::Approx(-first_before.x));
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects mirror and flip quick commands outside object mode", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
	session.set_component_selection(
		session.objects().front().id,
		qdr_test::vertex_selection({ vertex_id }, vertex_id),
		qec::SelectionMode::vertex);
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult mirror_result =
		qec::execute_runtime_command(session, runtime, mirror_axis_command(qec::TransformAxis::x));
	REQUIRE_FALSE(mirror_result.command.ok);
	REQUIRE_FALSE(mirror_result.command.changed);
	REQUIRE(mirror_result.command.message == "Select one or more mesh objects to mirror.");

	const qec::EditorRuntimeCommandResult flip_result =
		qec::execute_runtime_command(session, runtime, flip_view_command(qec::ViewportFlipOrientation::vertical));
	REQUIRE_FALSE(flip_result.command.ok);
	REQUIRE_FALSE(flip_result.command.changed);
	REQUIRE(flip_result.command.message == "Select one or more mesh objects to flip.");

	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects mirror and flip quick command mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;

	qec::EditorRuntimeCommand mirror_wrong_settings;
	mirror_wrong_settings.command_id = qec::EditorRuntimeCommandId::mirror_axis;
	mirror_wrong_settings.settings = std::monostate {};
	const qec::EditorRuntimeCommandResult mirror_result =
		qec::execute_runtime_command(session, runtime, mirror_wrong_settings);
	REQUIRE_FALSE(mirror_result.command.ok);
	REQUIRE_FALSE(mirror_result.command.changed);
	REQUIRE(mirror_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand flip_wrong_settings;
	flip_wrong_settings.command_id = qec::EditorRuntimeCommandId::flip_view;
	flip_wrong_settings.settings = qec::MirrorAxisRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult flip_result =
		qec::execute_runtime_command(session, runtime, flip_wrong_settings);
	REQUIRE_FALSE(flip_result.command.ok);
	REQUIRE_FALSE(flip_result.command.changed);
	REQUIRE(flip_result.command.message == "Runtime command settings do not match the command id.");
}

TEST_CASE("editor runtime command facade drives box footprint height and commit", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::size_t object_count_before = session.objects().size();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;
	REQUIRE(session.has_selection());

	const qec::EditorRuntimeCommandResult hover_result =
		qec::execute_runtime_command(
			session,
			runtime,
			box_ray_command(qec::EditorRuntimeCommandId::update_box_hover));
	REQUIRE(hover_result.command.ok);
	REQUIRE(hover_result.command.changed);
	REQUIRE(runtime.status().box_active);
	REQUIRE(hover_result.runtime.box_active);

	const qec::EditorRuntimeCommandResult begin_result =
		qec::execute_runtime_command(
			session,
			runtime,
			box_ray_command(qec::EditorRuntimeCommandId::begin_box_footprint));
	REQUIRE(begin_result.command.ok);
	REQUIRE(begin_result.command.changed);
	REQUIRE(runtime.box_state().drag_state == qec::BoxDragState::footprint);
	REQUIRE_FALSE(session.has_selection());
	REQUIRE(session.state().selection_revision != selection_revision_before);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult update_result =
		qec::execute_runtime_command(
			session,
			runtime,
			box_ray_command(qec::EditorRuntimeCommandId::update_box_footprint, box_ground_ray(6.0F, 7.0F)));
	REQUIRE(update_result.command.ok);
	REQUIRE(runtime.box_state().drag_state == qec::BoxDragState::footprint);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult begin_height_result =
		qec::execute_runtime_command(
			session,
			runtime,
			box_height_command(qec::EditorRuntimeCommandId::begin_box_height));
	REQUIRE(begin_height_result.command.ok);
	REQUIRE(begin_height_result.command.changed);
	REQUIRE(runtime.box_state().drag_state == qec::BoxDragState::height);

	const qec::EditorRuntimeCommandResult update_height_result =
		qec::execute_runtime_command(
			session,
			runtime,
			box_height_command(qec::EditorRuntimeCommandId::update_box_height, { 400.0F, 360.0F }));
	REQUIRE(update_height_result.command.ok);
	REQUIRE(runtime.box_state().drag_state == qec::BoxDragState::height);
	const qec::BoxPreviewDescriptor preview = runtime.box_preview_descriptor(true);
	REQUIRE(preview.ok);
	REQUIRE_FALSE(preview.bounds.empty);
	REQUIRE(preview.distance > 0.0F);

	const qec::EditorRuntimeCommandResult commit_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::commit_box));
	REQUIRE(commit_result.command.ok);
	REQUIRE(commit_result.command.changed);
	REQUIRE_FALSE(commit_result.runtime.box_active);
	REQUIRE(runtime.box_state().drag_state == qec::BoxDragState::idle);
	REQUIRE(session.objects().size() == object_count_before + 1U);
	REQUIRE(session.state().content_revision != content_revision_before);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade creates a box from bounds through history", "[operation][runtime-command]")
{
	qec::EditorSession session;
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, create_box_from_bounds_command());

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.objects().front().selected);
	REQUIRE(session.selection_mode() == qec::SelectionMode::object);
	REQUIRE(session.state().content_revision != content_revision_before);
	REQUIRE(session.state().selection_revision != selection_revision_before);
	REQUIRE(session.history_status().can_undo);
	REQUIRE_FALSE(result.runtime.hover_active);
	REQUIRE_FALSE(result.runtime.transform_drag_active);
}

TEST_CASE("editor runtime command facade rejects invalid box bounds without history", "[operation][runtime-command]")
{
	qec::EditorSession session;
	qec::EditorRuntime runtime;
	const qec::EditorRuntimeCommandResult failed =
		qec::execute_runtime_command(
			session,
			runtime,
			create_box_from_bounds_command({ 1.0F, 1.0F, 1.0F }, { 1.0F, 2.0F, 3.0F }));

	REQUIRE_FALSE(failed.command.ok);
	REQUIRE_FALSE(failed.command.changed);
	REQUIRE_FALSE(session.history_status().can_undo);
	const qec::EditorRuntimeCommandResult created =
		qec::execute_runtime_command(session, runtime, create_box_from_bounds_command());

	REQUIRE(created.command.ok);
	REQUIRE(created.command.changed);
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.objects().front().id == 1);
}

TEST_CASE("editor runtime command facade cancels box without history", "[operation][runtime-command]")
{
	qec::EditorSession session;
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult begin_result =
		qec::execute_runtime_command(
			session,
			runtime,
			box_ray_command(qec::EditorRuntimeCommandId::begin_box_footprint));
	REQUIRE(begin_result.command.ok);
	REQUIRE(runtime.status().box_active);

	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::cancel_box));
	REQUIRE(cancel_result.command.ok);
	REQUIRE(cancel_result.command.changed);
	REQUIRE_FALSE(cancel_result.runtime.box_active);
	REQUIRE_FALSE(runtime.status().box_active);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects box commit before height", "[operation][runtime-command]")
{
	qec::EditorSession session;
	qec::EditorRuntime runtime;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::commit_box));
	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Box height must be set before commit.");
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects box mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session;
	qec::EditorRuntime runtime;

	const qec::EditorRuntimeCommandResult hover_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::update_box_hover));
	REQUIRE_FALSE(hover_result.command.ok);
	REQUIRE_FALSE(hover_result.command.changed);
	REQUIRE(hover_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand begin_height_wrong_settings;
	begin_height_wrong_settings.command_id = qec::EditorRuntimeCommandId::begin_box_height;
	begin_height_wrong_settings.settings = qec::BoxRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult begin_height_result =
		qec::execute_runtime_command(session, runtime, begin_height_wrong_settings);
	REQUIRE_FALSE(begin_height_result.command.ok);
	REQUIRE_FALSE(begin_height_result.command.changed);
	REQUIRE(begin_height_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand commit_wrong_settings;
	commit_wrong_settings.command_id = qec::EditorRuntimeCommandId::commit_box;
	commit_wrong_settings.settings = qec::BoxRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult commit_result =
		qec::execute_runtime_command(session, runtime, commit_wrong_settings);
	REQUIRE_FALSE(commit_result.command.ok);
	REQUIRE_FALSE(commit_result.command.changed);
	REQUIRE(commit_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand create_box_wrong_settings =
		no_settings_command(qec::EditorRuntimeCommandId::create_box_from_bounds);
	const qec::EditorRuntimeCommandResult create_box_result =
		qec::execute_runtime_command(session, runtime, create_box_wrong_settings);
	REQUIRE_FALSE(create_box_result.command.ok);
	REQUIRE_FALSE(create_box_result.command.changed);
	REQUIRE(create_box_result.command.message == "Runtime command settings do not match the command id.");
}

TEST_CASE("editor runtime command facade drives cut activation mode and cancel without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult activate_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::activate_cut));
	REQUIRE(activate_result.command.ok);
	REQUIRE(activate_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::cut);
	REQUIRE(activate_result.runtime.active_tool == qec::ViewportInteractionTool::cut);
	REQUIRE(activate_result.runtime.cut_active);
	REQUIRE(runtime.cut_state().mode == qec::CutMode::keep_both);
	REQUIRE(runtime.cut_state().target_object_ids.size() == 1U);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult update_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::update_cut_preview));
	REQUIRE(update_result.command.ok);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::cut);
	REQUIRE(update_result.runtime.cut_active);
	REQUIRE(runtime.cut_state().target_object_ids.size() == 1U);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult mode_result =
		qec::execute_runtime_command(
			session,
			runtime,
			set_cut_mode_command(static_cast<int>(qec::CutMode::keep_front)));
	REQUIRE(mode_result.command.ok);
	REQUIRE(mode_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::cut);
	REQUIRE(runtime.cut_state().mode == qec::CutMode::keep_front);
	REQUIRE(runtime.cut_state().target_object_ids.size() == 1U);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::cancel_cut));
	REQUIRE(cancel_result.command.ok);
	REQUIRE(cancel_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE(cancel_result.runtime.active_tool == qec::ViewportInteractionTool::select);
	REQUIRE_FALSE(cancel_result.runtime.cut_active);
	REQUIRE(runtime.cut_state().target_object_ids.empty());
	REQUIRE(runtime.cut_state().mode == qec::CutMode::keep_both);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects cut commands without object selection", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
	runtime.state().active_tool = qec::ViewportInteractionTool::knife;
	runtime.state().knife.active = true;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult activate_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::activate_cut));
	REQUIRE_FALSE(activate_result.command.ok);
	REQUIRE_FALSE(activate_result.command.changed);
	REQUIRE(activate_result.command.message == "Select one or more mesh objects to cut.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::knife);
	REQUIRE(runtime.state().knife.active);

	const qec::EditorRuntimeCommandResult click_result =
		qec::execute_runtime_command(
			session,
			runtime,
			cut_ray_command(qec::EditorRuntimeCommandId::click_cut, cut_ray(0.0F)));
	REQUIRE_FALSE(click_result.command.ok);
	REQUIRE_FALSE(click_result.command.changed);
	REQUIRE(click_result.command.message == "Select one or more mesh objects to cut.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::knife);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade drives cut interactions and finalize", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult first_click =
		qec::execute_runtime_command(
			session,
			runtime,
			cut_ray_command(qec::EditorRuntimeCommandId::click_cut, cut_ray(0.0F)));
	REQUIRE(first_click.command.ok);
	REQUIRE(first_click.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::cut);
	REQUIRE(first_click.runtime.cut_active);
	REQUIRE(runtime.cut_state().points.size() == 1U);
	REQUIRE(runtime.cut_state().dragging_new_point);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult second_click =
		qec::execute_runtime_command(
			session,
			runtime,
			cut_ray_command(qec::EditorRuntimeCommandId::click_cut, cut_ray(1.0F)));
	REQUIRE(second_click.command.ok);
	REQUIRE(second_click.command.changed);
	REQUIRE(runtime.cut_state().points.size() == 2U);
	REQUIRE_FALSE(runtime.cut_state().dragging_new_point);
	REQUIRE(runtime.cut_state().preview_valid);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult begin_point_drag =
		qec::execute_runtime_command(session, runtime, cut_point_drag_command(0));
	REQUIRE(begin_point_drag.command.ok);
	REQUIRE(begin_point_drag.command.changed);
	REQUIRE(runtime.cut_state().dragging_point_index == 0);

	const qec::EditorRuntimeCommandResult point_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			cut_ray_command(qec::EditorRuntimeCommandId::update_cut_hover, cut_ray(-0.5F), true));
	REQUIRE(point_drag.command.ok);
	REQUIRE(point_drag.command.changed);
	REQUIRE(runtime.cut_state().points.size() == 2U);
	REQUIRE(runtime.cut_state().points[0].x == Catch::Approx(-0.5F));
	REQUIRE(runtime.cut_state().preview_valid);

	const qec::EditorRuntimeCommandResult end_point_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::end_cut_drag));
	REQUIRE(end_point_drag.command.ok);
	REQUIRE(end_point_drag.command.changed);
	REQUIRE(runtime.cut_state().dragging_point_index == -1);
	REQUIRE_FALSE(runtime.cut_state().dragging_line);

	const qec::EditorRuntimeCommandResult begin_line_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			cut_ray_command(qec::EditorRuntimeCommandId::begin_cut_line_drag, cut_ray(0.25F)));
	REQUIRE(begin_line_drag.command.ok);
	REQUIRE(begin_line_drag.command.changed);
	REQUIRE(runtime.cut_state().dragging_line);

	const qec::EditorRuntimeCommandResult line_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			cut_ray_command(qec::EditorRuntimeCommandId::update_cut_hover, cut_ray(0.5F), true));
	REQUIRE(line_drag.command.ok);
	REQUIRE(line_drag.command.changed);
	REQUIRE(runtime.cut_state().points.size() == 2U);
	REQUIRE(runtime.cut_state().dragging_line);
	REQUIRE(runtime.cut_state().preview_valid);

	const qec::EditorRuntimeCommandResult end_line_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::end_cut_drag));
	REQUIRE(end_line_drag.command.ok);
	REQUIRE(end_line_drag.command.changed);
	REQUIRE_FALSE(runtime.cut_state().dragging_line);

	const qec::EditorRuntimeCommandResult finalize =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::finalize_cut));
	REQUIRE(finalize.command.ok);
	REQUIRE(finalize.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE_FALSE(finalize.runtime.cut_active);
	REQUIRE(runtime.cut_state().points.empty());
	REQUIRE(session.state().content_revision != content_revision_before);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects cut mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	qec::EditorRuntimeCommand set_mode_missing_settings;
	set_mode_missing_settings.command_id = qec::EditorRuntimeCommandId::set_cut_mode;
	set_mode_missing_settings.settings = std::monostate {};
	const qec::EditorRuntimeCommandResult set_mode_result =
		qec::execute_runtime_command(session, runtime, set_mode_missing_settings);
	REQUIRE_FALSE(set_mode_result.command.ok);
	REQUIRE_FALSE(set_mode_result.command.changed);
	REQUIRE(set_mode_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand cancel_extra_settings;
	cancel_extra_settings.command_id = qec::EditorRuntimeCommandId::cancel_cut;
	cancel_extra_settings.settings = qec::SetCutModeRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(session, runtime, cancel_extra_settings);
	REQUIRE_FALSE(cancel_result.command.ok);
	REQUIRE_FALSE(cancel_result.command.changed);
	REQUIRE(cancel_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand hover_missing_settings;
	hover_missing_settings.command_id = qec::EditorRuntimeCommandId::update_cut_hover;
	hover_missing_settings.settings = std::monostate {};
	const qec::EditorRuntimeCommandResult hover_result =
		qec::execute_runtime_command(session, runtime, hover_missing_settings);
	REQUIRE_FALSE(hover_result.command.ok);
	REQUIRE_FALSE(hover_result.command.changed);
	REQUIRE(hover_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand click_wrong_settings;
	click_wrong_settings.command_id = qec::EditorRuntimeCommandId::click_cut;
	click_wrong_settings.settings = qec::CutPointDragRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult click_result =
		qec::execute_runtime_command(session, runtime, click_wrong_settings);
	REQUIRE_FALSE(click_result.command.ok);
	REQUIRE_FALSE(click_result.command.changed);
	REQUIRE(click_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand point_drag_wrong_settings;
	point_drag_wrong_settings.command_id = qec::EditorRuntimeCommandId::begin_cut_point_drag;
	point_drag_wrong_settings.settings = qec::CutRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult point_drag_result =
		qec::execute_runtime_command(session, runtime, point_drag_wrong_settings);
	REQUIRE_FALSE(point_drag_result.command.ok);
	REQUIRE_FALSE(point_drag_result.command.changed);
	REQUIRE(point_drag_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand line_drag_wrong_settings;
	line_drag_wrong_settings.command_id = qec::EditorRuntimeCommandId::begin_cut_line_drag;
	line_drag_wrong_settings.settings = qec::CutPointDragRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult line_drag_result =
		qec::execute_runtime_command(session, runtime, line_drag_wrong_settings);
	REQUIRE_FALSE(line_drag_result.command.ok);
	REQUIRE_FALSE(line_drag_result.command.changed);
	REQUIRE(line_drag_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand end_drag_extra_settings;
	end_drag_extra_settings.command_id = qec::EditorRuntimeCommandId::end_cut_drag;
	end_drag_extra_settings.settings = qec::CutRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult end_drag_result =
		qec::execute_runtime_command(session, runtime, end_drag_extra_settings);
	REQUIRE_FALSE(end_drag_result.command.ok);
	REQUIRE_FALSE(end_drag_result.command.changed);
	REQUIRE(end_drag_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand finalize_extra_settings;
	finalize_extra_settings.command_id = qec::EditorRuntimeCommandId::finalize_cut;
	finalize_extra_settings.settings = qec::CutRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult finalize_result =
		qec::execute_runtime_command(session, runtime, finalize_extra_settings);
	REQUIRE_FALSE(finalize_result.command.ok);
	REQUIRE_FALSE(finalize_result.command.changed);
	REQUIRE(finalize_result.command.message == "Runtime command settings do not match the command id.");

	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade drives mirror first-drag preview and finalize", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult activate_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::activate_mirror));
	REQUIRE(activate_result.command.ok);
	REQUIRE(activate_result.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::mirror);
	REQUIRE(activate_result.runtime.mirror_active);
	REQUIRE(runtime.mirror_state().target_object_ids.size() == 1U);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult first_click =
		qec::execute_runtime_command(
			session,
			runtime,
			mirror_ray_command(qec::EditorRuntimeCommandId::click_mirror, cut_ray(0.0F)));
	REQUIRE(first_click.command.ok);
	REQUIRE(first_click.command.changed);
	REQUIRE(runtime.mirror_state().plane.points.size() == 1U);
	REQUIRE(runtime.mirror_state().plane.dragging_new_point);
	REQUIRE_FALSE(runtime.mirror_state().preview_valid);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult first_drag_preview =
		qec::execute_runtime_command(
			session,
			runtime,
			mirror_ray_command(qec::EditorRuntimeCommandId::update_mirror_hover, cut_ray(1.0F), true));
	REQUIRE(first_drag_preview.command.ok);
	REQUIRE(first_drag_preview.command.changed);
	REQUIRE(runtime.mirror_state().plane.points.size() == 2U);
	REQUIRE(runtime.mirror_state().plane.dragging_new_point);
	REQUIRE(runtime.mirror_state().preview_valid);
	REQUIRE(first_drag_preview.runtime.mirror_can_preview);
	REQUIRE(first_drag_preview.runtime.mirror_preview_valid);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult end_new_point_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::end_mirror_drag));
	REQUIRE(end_new_point_drag.command.ok);
	REQUIRE(end_new_point_drag.command.changed);
	REQUIRE_FALSE(runtime.mirror_state().plane.dragging_new_point);
	REQUIRE(runtime.mirror_state().preview_valid);

	const qec::EditorRuntimeCommandResult begin_point_drag =
		qec::execute_runtime_command(session, runtime, mirror_point_drag_command(0));
	REQUIRE(begin_point_drag.command.ok);
	REQUIRE(begin_point_drag.command.changed);
	REQUIRE(runtime.mirror_state().plane.dragging_point_index == 0);

	const qec::EditorRuntimeCommandResult point_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			mirror_ray_command(qec::EditorRuntimeCommandId::update_mirror_hover, cut_ray(-0.5F), true));
	REQUIRE(point_drag.command.ok);
	REQUIRE(point_drag.command.changed);
	REQUIRE(runtime.mirror_state().plane.points[0].x == Catch::Approx(-0.5F));
	REQUIRE(runtime.mirror_state().preview_valid);

	const qec::EditorRuntimeCommandResult end_point_drag =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::end_mirror_drag));
	REQUIRE(end_point_drag.command.ok);
	REQUIRE(end_point_drag.command.changed);
	REQUIRE(runtime.mirror_state().plane.dragging_point_index == -1);

	const qec::EditorRuntimeCommandResult finalize =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::finalize_mirror));
	REQUIRE(finalize.command.ok);
	REQUIRE(finalize.command.changed);
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::select);
	REQUIRE_FALSE(finalize.runtime.mirror_active);
	REQUIRE(runtime.mirror_state().plane.points.empty());
	REQUIRE(session.state().content_revision != content_revision_before);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects mirror commands without object selection", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
	runtime.state().active_tool = qec::ViewportInteractionTool::poly;
	runtime.state().poly.active = true;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult activate_result =
		qec::execute_runtime_command(
			session,
			runtime,
			no_settings_command(qec::EditorRuntimeCommandId::activate_mirror));
	REQUIRE_FALSE(activate_result.command.ok);
	REQUIRE_FALSE(activate_result.command.changed);
	REQUIRE(activate_result.command.message == "Select one or more mesh objects to mirror.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::poly);
	REQUIRE(runtime.state().poly.active);

	const qec::EditorRuntimeCommandResult click_result =
		qec::execute_runtime_command(
			session,
			runtime,
			mirror_ray_command(qec::EditorRuntimeCommandId::click_mirror, cut_ray(0.0F)));
	REQUIRE_FALSE(click_result.command.ok);
	REQUIRE_FALSE(click_result.command.changed);
	REQUIRE(click_result.command.message == "Select one or more mesh objects to mirror.");
	REQUIRE(runtime.active_tool() == qec::ViewportInteractionTool::poly);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}

TEST_CASE("editor runtime command facade pick-select applies modifier policy without history", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_selection();
	session.set_selection_mode(qec::SelectionMode::face);
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult replace_result =
		qec::execute_runtime_command(session, runtime, pick_select_command());
	REQUIRE(replace_result.command.ok);
	REQUIRE(replace_result.command.changed);
	REQUIRE(session.selection_mode() == qec::SelectionMode::face);
	REQUIRE(session.active_selection().kind == qec::ActiveSelectionKind::face);
	REQUIRE(session.state().selection_revision != selection_revision_before);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const std::uint64_t selected_revision = session.state().selection_revision;
	const qec::EditorRuntimeCommandResult add_same_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray(), 1, false, true));
	REQUIRE(add_same_result.command.ok);
	REQUIRE_FALSE(add_same_result.command.changed);
	REQUIRE(session.state().selection_revision == selected_revision);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult remove_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray(), 1, true, false));
	REQUIRE(remove_result.command.ok);
	REQUIRE(remove_result.command.changed);
	REQUIRE_FALSE(session.has_selection());
	REQUIRE(session.state().selection_revision > selected_revision);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade pick-select drag strokes paint component selection", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_selection();
	session.set_selection_mode(qec::SelectionMode::face);
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult first_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray()));
	REQUIRE(first_result.command.ok);
	REQUIRE(first_result.command.changed);
	REQUIRE(selected_face_count(session) == 1U);

	const qec::EditorRuntimeCommandResult paint_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(top_center_ray(), 0));
	REQUIRE(paint_result.command.ok);
	REQUIRE(paint_result.command.changed);
	REQUIRE(selected_face_count(session) == 2U);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	const std::uint64_t painted_revision = session.state().selection_revision;
	const qec::EditorRuntimeCommandResult same_paint_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(top_center_ray(), 0));
	REQUIRE(same_paint_result.command.ok);
	REQUIRE_FALSE(same_paint_result.command.changed);
	REQUIRE(session.state().selection_revision == painted_revision);
	REQUIRE(selected_face_count(session) == 2U);

	const qec::EditorRuntimeCommandResult remove_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(top_center_ray(), 0, true, false));
	REQUIRE(remove_result.command.ok);
	REQUIRE(remove_result.command.changed);
	REQUIRE(selected_face_count(session) == 1U);
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade paint-select ignores component-mode face fallback", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_selection();
	session.set_selection_mode(qec::SelectionMode::vertex);
	session.clear_history();
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult face_interior_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray(), 0));
	REQUIRE(face_interior_result.command.ok);
	REQUIRE_FALSE(face_interior_result.command.changed);
	REQUIRE(session.state().selection_revision == selection_revision_before);
	REQUIRE_FALSE(session.has_selection());

	const qec::EditorRuntimeCommandResult vertex_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_right_top_vertex_ray(), 0));
	REQUIRE(vertex_result.command.ok);
	REQUIRE(vertex_result.command.changed);
	REQUIRE(selected_vertex_count(session) == 1U);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade ctrl component click ignores face fallback", "[operation][runtime-command]")
{
	qec::EditorSession edge_session = qdr_test::selected_cube_session();
	qec::EditorRuntime edge_runtime;
	edge_session.clear_selection();
	edge_session.set_selection_mode(qec::SelectionMode::edge);
	REQUIRE(qec::execute_runtime_command(edge_session, edge_runtime, pick_select_command(front_right_edge_ray())).command.changed);
	REQUIRE(selected_edge_count(edge_session) == 1U);
	const std::uint64_t edge_revision = edge_session.state().selection_revision;

	const qec::EditorRuntimeCommandResult edge_face_interior_result =
		qec::execute_runtime_command(edge_session, edge_runtime, pick_select_command(front_center_ray(), 1, true, false));
	REQUIRE(edge_face_interior_result.command.ok);
	REQUIRE_FALSE(edge_face_interior_result.command.changed);
	REQUIRE(edge_session.state().selection_revision == edge_revision);
	REQUIRE(selected_edge_count(edge_session) == 1U);

	qec::EditorSession vertex_session = qdr_test::selected_cube_session();
	qec::EditorRuntime vertex_runtime;
	vertex_session.clear_selection();
	vertex_session.set_selection_mode(qec::SelectionMode::vertex);
	REQUIRE(qec::execute_runtime_command(vertex_session, vertex_runtime, pick_select_command(front_right_top_vertex_ray())).command.changed);
	REQUIRE(selected_vertex_count(vertex_session) == 1U);
	const std::uint64_t vertex_revision = vertex_session.state().selection_revision;

	const qec::EditorRuntimeCommandResult vertex_face_interior_result =
		qec::execute_runtime_command(vertex_session, vertex_runtime, pick_select_command(front_center_ray(), 1, true, false));
	REQUIRE(vertex_face_interior_result.command.ok);
	REQUIRE_FALSE(vertex_face_interior_result.command.changed);
	REQUIRE(vertex_session.state().selection_revision == vertex_revision);
	REQUIRE(selected_vertex_count(vertex_session) == 1U);
}

TEST_CASE("editor runtime command facade pick-select double-click delegates by selection mode", "[operation][runtime-command]")
{
	qec::EditorSession face_session = qdr_test::selected_cube_session();
	qec::EditorRuntime face_runtime;
	face_session.clear_selection();
	face_session.set_selection_mode(qec::SelectionMode::face);
	const qec::EditorRuntimeCommandResult face_result =
		qec::execute_runtime_command(face_session, face_runtime, pick_select_command(front_center_ray(), 2));
	REQUIRE(face_result.command.ok);
	REQUIRE(face_result.command.changed);
	REQUIRE(face_session.selection_mode() == qec::SelectionMode::face);
	REQUIRE(face_session.active_selection().kind == qec::ActiveSelectionKind::face);
	REQUIRE(face_result.command.changed == (face_session.state().selection_revision > 0U));
	REQUIRE_FALSE(face_session.history_status().can_undo);

	qec::EditorSession edge_session = qdr_test::selected_cube_session();
	qec::EditorRuntime edge_runtime;
	edge_session.clear_selection();
	edge_session.set_selection_mode(qec::SelectionMode::edge);
	const qec::EditorRuntimeCommandResult edge_result =
		qec::execute_runtime_command(edge_session, edge_runtime, pick_select_command({ { 0.0F, 1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } }, 2));
	REQUIRE(edge_result.command.ok);
	REQUIRE(edge_session.selection_mode() == qec::SelectionMode::edge);
	REQUIRE_FALSE(edge_session.history_status().can_undo);

	qec::EditorSession vertex_session = qdr_test::selected_cube_session();
	qec::EditorRuntime vertex_runtime;
	vertex_session.clear_selection();
	vertex_session.set_selection_mode(qec::SelectionMode::vertex);
	const qec::EditorRuntimeCommandResult vertex_result =
		qec::execute_runtime_command(vertex_session, vertex_runtime, pick_select_command({ { 1.0F, 1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } }, 2));
	REQUIRE(vertex_result.command.ok);
	REQUIRE(vertex_session.selection_mode() == qec::SelectionMode::vertex);
	REQUIRE_FALSE(vertex_session.history_status().can_undo);
}

TEST_CASE("editor runtime command facade rejects pick-select with mismatched settings", "[operation][runtime-command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::EditorRuntimeCommand command;
	command.command_id = qec::EditorRuntimeCommandId::pick_select;
	command.settings = std::monostate {};
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message == "Runtime command settings do not match the command id.");
	REQUIRE(session.state().content_revision == content_revision_before);
	REQUIRE(session.state().selection_revision == selection_revision_before);
}
