////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include "test_support.hpp"

#include <editor/modeling/runtime/runtime_command.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace qec = quader_editor_core;

namespace {

qec::EditorRuntimeCommand copy_command()
{
	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::CopySelectedMeshes;
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
        command.command_id = qec::EditorRuntimeCommandId::PasteCopiedMeshes;
        command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand set_selection_mode_command(int mode)
{
	qec::SetSelectionModeRuntimeCommandSettings settings;
	settings.selection_mode = mode;

	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::SetSelectionMode;
        command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand set_active_tool_command(qec::ViewportInteractionTool tool)
{
	qec::SetActiveToolRuntimeCommandSettings settings;
	settings.tool = tool;

	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::SetActiveTool;
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
        command.command_id = qec::EditorRuntimeCommandId::PickSelect;
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
        command.command_id = qec::EditorRuntimeCommandId::SetCutMode;
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
        camera.projection = qec::CameraProjection::Perspective;
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
        command.command_id = qec::EditorRuntimeCommandId::BeginCutPointDrag;
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
        command.command_id = qec::EditorRuntimeCommandId::BeginMirrorPointDrag;
        command.settings = settings;
	return command;
}

quader_poly::Ray box_ground_ray(float x = 4.0F, float z = 4.0F)
{
	return { { x, 4.0F, z }, { 0.0F, -1.0F, 0.0F } };
}

qec::EditorRuntimeCommand box_ray_command(
		qec::EditorRuntimeCommandId command_id,
		quader_poly::Ray ray = box_ground_ray(),
		bool snap_enabled = true)
{
	qec::BoxRayRuntimeCommandSettings settings;
	settings.ray = ray;
	settings.grid_size = 1.0;
	settings.snap_enabled = snap_enabled;

	qec::EditorRuntimeCommand command;
	command.command_id = command_id;
	command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand box_height_command(
		qec::EditorRuntimeCommandId command_id,
		quader::QVec2 screen_position = { 400.0F, 300.0F },
		quader_poly::Ray ray = box_ground_ray(),
		bool snap_enabled = true)
{
	qec::BoxHeightRuntimeCommandSettings settings;
	settings.screen_position = screen_position;
	settings.ray = ray;
	settings.grid_size = 1.0;
	settings.snap_enabled = snap_enabled;

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
        command.command_id = qec::EditorRuntimeCommandId::CreateBoxFromBounds;
        command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand mirror_axis_command(qec::TransformAxis axis)
{
	qec::MirrorAxisRuntimeCommandSettings settings;
	settings.axis = axis;

	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::MirrorAxis;
        command.settings = settings;
	return command;
}

qec::EditorRuntimeCommand flip_view_command(qec::ViewportFlipOrientation orientation)
{
	qec::ViewFlipRuntimeCommandSettings settings;
	settings.orientation = orientation;

	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::FlipView;
        command.settings = settings;
	return command;
}

} // namespace

using qdr_test::selected_edge_count;
using qdr_test::selected_face_count;
using qdr_test::selected_vertex_count;

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeCopiesSelectedMeshesWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, copy_command());

	EXPECT_TRUE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(runtime.clipboard_available());
	EXPECT_TRUE(result.runtime.clipboard_available);
	EXPECT_TRUE(result.runtime.clipboard_object_count == 1U);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeReportsFailedCopyWithoutObjectSelection) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, copy_command());

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Select one or more meshes in object mode before copying.");
	EXPECT_FALSE(runtime.clipboard_available());
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeHandlesSelectionWorkflowCommandsWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;

        const qec::EditorRuntimeCommandResult clear_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::ClearSelection));
        EXPECT_TRUE(clear_result.command.ok);
	EXPECT_TRUE(clear_result.command.changed);
	EXPECT_FALSE(session.has_selection());
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult select_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::SelectAll));
        EXPECT_TRUE(select_result.command.ok);
	EXPECT_TRUE(select_result.command.changed);
	EXPECT_TRUE(session.has_selection());
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult invert_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::InvertSelection));
        EXPECT_TRUE(invert_result.command.ok);
	EXPECT_TRUE(invert_result.command.changed);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeHandlesDeleteSelectionThroughHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::size_t object_count_before = session.objects().size();

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::DeleteSelection));

        EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() < object_count_before);
	EXPECT_TRUE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	EXPECT_TRUE(undo.applied);
	EXPECT_TRUE(session.objects().size() == object_count_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeReportsDeleteWithNoSelectionAsUnchangedSuccess) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::DeleteSelection));

        EXPECT_TRUE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message.empty());
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadePastesCopiedMeshesThroughHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	EXPECT_TRUE(qec::execute_runtime_command(session, runtime, copy_command()).command.ok);

	const std::size_t object_count_before = session.objects().size();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, paste_command(quader::QVec3 { 3.0F, 0.0F, 3.0F }));

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	EXPECT_TRUE(session.selected_object_ids().front() != 1);
	EXPECT_TRUE(session.state().content_revision != content_revision_before);
	EXPECT_TRUE(session.state().selection_revision != selection_revision_before);
	EXPECT_TRUE(runtime.clipboard_available());
	EXPECT_TRUE(result.runtime.clipboard_available);
	EXPECT_TRUE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	EXPECT_TRUE(undo.applied);
	EXPECT_TRUE(session.objects().size() == object_count_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadePastesWithoutTargetAtCurrentSelectionCenter) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	EXPECT_TRUE(qec::execute_runtime_command(session, runtime, copy_command()).command.ok);
	EXPECT_TRUE(session.translate_selected({ 5.0F, 0.0F, 0.0F }).changed);

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, paste_command(std::nullopt));

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	const qec::Bounds3 selected_bounds = session.selection_bounds();
	EXPECT_FALSE(selected_bounds.empty);
	EXPECT_NEAR((selected_bounds.min.x + selected_bounds.max.x) * 0.5F, 5.0F, 0.0001);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeReportsEmptyPasteWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, paste_command(std::nullopt));

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Copy one or more meshes before pasting.");
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeMarksTheCurrentDocumentClean) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	EXPECT_TRUE(session.dirty());

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::MarkClean));

        EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(session.dirty());

        const qec::EditorRuntimeCommandResult unchanged_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::MarkClean));
        EXPECT_TRUE(unchanged_result.command.ok);
	EXPECT_FALSE(unchanged_result.command.changed);
	EXPECT_FALSE(session.dirty());
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRestoresUndoAndRedoHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::size_t object_count_before = session.objects().size();

        const qec::EditorRuntimeCommandResult delete_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::DeleteSelection));
        EXPECT_TRUE(delete_result.command.ok);
	EXPECT_TRUE(delete_result.command.changed);
	EXPECT_TRUE(session.objects().empty());
	EXPECT_TRUE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult undo_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::Undo));
        EXPECT_TRUE(undo_result.command.ok);
	EXPECT_TRUE(undo_result.command.changed);
	EXPECT_TRUE(session.objects().size() == object_count_before);
	EXPECT_FALSE(session.history_status().can_undo);
	EXPECT_TRUE(session.history_status().can_redo);

        const qec::EditorRuntimeCommandResult redo_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::Redo));
        EXPECT_TRUE(redo_result.command.ok);
	EXPECT_TRUE(redo_result.command.changed);
	EXPECT_TRUE(session.objects().empty());
	EXPECT_TRUE(session.history_status().can_undo);
	EXPECT_FALSE(session.history_status().can_redo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeReportsEmptyUndoAndRedoAsUnchangedSuccess) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult undo_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::Undo));
        EXPECT_TRUE(undo_result.command.ok);
	EXPECT_FALSE(undo_result.command.changed);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);

        const qec::EditorRuntimeCommandResult redo_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::Redo));
        EXPECT_TRUE(redo_result.command.ok);
	EXPECT_FALSE(redo_result.command.changed);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeClearsHoverWithoutDocumentMutation) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::SelectionHoverState hover;
	hover.object_id = 1;
        hover.kind = qec::SelectionHoverKind::Object;
        hover.candidate_object_id = 1;
	runtime.set_hover(hover);
	EXPECT_TRUE(runtime.status().hover_has_candidate_object);
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::ClearHover));

        EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(runtime.status().hover_active);
	EXPECT_FALSE(result.runtime.hover_active);
	EXPECT_FALSE(runtime.status().hover_has_candidate_object);
	EXPECT_FALSE(result.runtime.hover_has_candidate_object);
        EXPECT_TRUE(runtime.hover().kind == qec::SelectionHoverKind::None);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult unchanged_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::ClearHover));
        EXPECT_TRUE(unchanged_result.command.ok);
	EXPECT_FALSE(unchanged_result.command.changed);
	EXPECT_FALSE(unchanged_result.runtime.hover_active);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeSetsSelectionModeWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult face_result =
            qec::execute_runtime_command(
                session, runtime,
                set_selection_mode_command(
                    static_cast<int>(qec::SelectionMode::Face)));

        EXPECT_TRUE(face_result.command.ok);
	EXPECT_TRUE(face_result.command.changed);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Face);
        EXPECT_TRUE(face_result.command.changed == (session.state().selection_revision != selection_revision_before));
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

	const std::uint64_t face_selection_revision = session.state().selection_revision;
        const qec::EditorRuntimeCommandResult same_result =
            qec::execute_runtime_command(
                session, runtime,
                set_selection_mode_command(
                    static_cast<int>(qec::SelectionMode::Face)));
        EXPECT_TRUE(same_result.command.ok);
	EXPECT_FALSE(same_result.command.changed);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Face);
        EXPECT_TRUE(session.state().selection_revision == face_selection_revision);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult sanitized_result =
		qec::execute_runtime_command(session, runtime, set_selection_mode_command(9999));
	EXPECT_TRUE(sanitized_result.command.ok);
	EXPECT_TRUE(sanitized_result.command.changed);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Object);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult low_sanitized_result =
		qec::execute_runtime_command(session, runtime, set_selection_mode_command(-9999));
	EXPECT_TRUE(low_sanitized_result.command.ok);
	EXPECT_TRUE(low_sanitized_result.command.changed);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Vertex);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsSetSelectionModeWithMismatchedSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::SetSelectionMode;
        command.settings = std::monostate {};
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Runtime command settings do not match the command id.");
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Object);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeSetsActiveToolWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult move_result =
            qec::execute_runtime_command(
                session, runtime,
                set_active_tool_command(qec::ViewportInteractionTool::Move));

        EXPECT_TRUE(move_result.command.ok);
	EXPECT_TRUE(move_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Move);
        EXPECT_TRUE(move_result.runtime.active_tool ==
                    qec::ViewportInteractionTool::Move);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Object);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult same_result =
            qec::execute_runtime_command(
                session, runtime,
                set_active_tool_command(qec::ViewportInteractionTool::Move));
        EXPECT_TRUE(same_result.command.ok);
	EXPECT_FALSE(same_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Move);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult edge_loop_result =
            qec::execute_runtime_command(
                session, runtime,
                set_active_tool_command(
                    qec::ViewportInteractionTool::InsertEdgeLoop));
        EXPECT_TRUE(edge_loop_result.command.ok);
	EXPECT_TRUE(edge_loop_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::InsertEdgeLoop);
        EXPECT_TRUE(edge_loop_result.runtime.active_tool ==
                    qec::ViewportInteractionTool::InsertEdgeLoop);
        EXPECT_TRUE(edge_loop_result.runtime.insert_edge_loop_active);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Edge);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision != selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult sanitized_result =
            qec::execute_runtime_command(
                session, runtime,
                set_active_tool_command(qec::ViewportInteractionTool::Unknown));
        EXPECT_TRUE(sanitized_result.command.ok);
	EXPECT_TRUE(sanitized_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_TRUE(sanitized_result.runtime.active_tool ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsSelectedObjectToolActivationWithoutSelection) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
        runtime.state().active_tool = qec::ViewportInteractionTool::Move;
        runtime.state().transform_drag.active = true;
        runtime.state().transform_drag.axis = qec::TransformAxis::X;
        runtime.state().gizmo_hover_axis = qec::TransformAxis::X;
        const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult cut_result =
            qec::execute_runtime_command(
                session, runtime,
                set_active_tool_command(qec::ViewportInteractionTool::Cut));
        EXPECT_FALSE(cut_result.command.ok);
	EXPECT_FALSE(cut_result.command.changed);
	EXPECT_TRUE(cut_result.command.message == "Select one or more mesh objects to cut.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Move);
        EXPECT_TRUE(runtime.state().transform_drag.active);
        EXPECT_TRUE(runtime.state().gizmo_hover_axis == qec::TransformAxis::X);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);

        const qec::EditorRuntimeCommandResult mirror_result =
            qec::execute_runtime_command(
                session, runtime,
                set_active_tool_command(qec::ViewportInteractionTool::Mirror));
        EXPECT_FALSE(mirror_result.command.ok);
	EXPECT_FALSE(mirror_result.command.changed);
	EXPECT_TRUE(mirror_result.command.message == "Select one or more mesh objects to mirror.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Move);
        EXPECT_TRUE(runtime.state().transform_drag.active);
        EXPECT_TRUE(runtime.state().gizmo_hover_axis == qec::TransformAxis::X);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsSetActiveToolWithMismatchedSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::SetActiveTool;
        command.settings = std::monostate {};
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Runtime command settings do not match the command id.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Object);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeDrivesInsertEdgeLoopPreviewAndCommit) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

        const qec::EditorRuntimeCommandResult hover_result =
            qec::execute_runtime_command(
                session, runtime,
                insert_edge_loop_pick_command(
                    qec::EditorRuntimeCommandId::UpdateInsertEdgeLoopHover));
        EXPECT_TRUE(hover_result.command.ok);
	EXPECT_TRUE(hover_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::InsertEdgeLoop);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Edge);
        EXPECT_TRUE(runtime.insert_edge_loop_state().active);
	EXPECT_FALSE(runtime.insert_edge_loop_state().dragging);
	EXPECT_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	EXPECT_FALSE(session.history_status().can_undo);

	const float hover_factor = runtime.insert_edge_loop_state().factor;
        const qec::EditorRuntimeCommandResult begin_result =
            qec::execute_runtime_command(
                session, runtime,
                insert_edge_loop_pick_command(
                    qec::EditorRuntimeCommandId::BeginInsertEdgeLoopDrag));
        EXPECT_TRUE(begin_result.command.ok);
	EXPECT_TRUE(begin_result.command.changed);
	EXPECT_TRUE(runtime.insert_edge_loop_state().dragging);
	EXPECT_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	EXPECT_NEAR(runtime.insert_edge_loop_state().factor, hover_factor, 0.0001);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult release_hover_result =
            qec::execute_runtime_command(
                session, runtime,
                insert_edge_loop_pick_command(
                    qec::EditorRuntimeCommandId::UpdateInsertEdgeLoopHover));
        EXPECT_TRUE(release_hover_result.command.ok);
	EXPECT_TRUE(runtime.insert_edge_loop_state().dragging);
	EXPECT_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult update_result =
            qec::execute_runtime_command(
                session, runtime,
                insert_edge_loop_pick_command(
                    qec::EditorRuntimeCommandId::UpdateInsertEdgeLoopDrag,
                    front_right_edge_ray(0.25F)));
        EXPECT_TRUE(update_result.command.ok);
	EXPECT_TRUE(runtime.insert_edge_loop_state().dragging);
	EXPECT_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult commit_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::CommitInsertEdgeLoopDrag));
        EXPECT_TRUE(commit_result.command.ok);
	EXPECT_TRUE(commit_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_FALSE(runtime.insert_edge_loop_state().active);
	EXPECT_FALSE(runtime.insert_edge_loop_state().dragging);
	EXPECT_TRUE(runtime.insert_edge_loop_state().preview_lines.empty());
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Edge);
        EXPECT_TRUE(session.state().content_revision != content_revision_before);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeCancelsInsertEdgeLoopPreviewWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

        const qec::EditorRuntimeCommandResult begin_result =
            qec::execute_runtime_command(
                session, runtime,
                insert_edge_loop_pick_command(
                    qec::EditorRuntimeCommandId::BeginInsertEdgeLoopDrag));
        EXPECT_TRUE(begin_result.command.ok);
	EXPECT_TRUE(runtime.insert_edge_loop_state().active);
	EXPECT_TRUE(runtime.insert_edge_loop_state().dragging);
	EXPECT_FALSE(runtime.insert_edge_loop_state().preview_lines.empty());

        const qec::EditorRuntimeCommandResult cancel_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::CancelInsertEdgeLoop));
        EXPECT_TRUE(cancel_result.command.ok);
	EXPECT_TRUE(cancel_result.command.changed);
	EXPECT_TRUE(runtime.insert_edge_loop_state().active);
	EXPECT_FALSE(runtime.insert_edge_loop_state().dragging);
	EXPECT_TRUE(runtime.insert_edge_loop_state().preview_lines.empty());
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeReportsInsertEdgeLoopCommitWithoutADrag) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::CommitInsertEdgeLoopDrag));
        EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Select an edge to insert an edge loop.");
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsInsertEdgeLoopMismatchedSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult pick_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::UpdateInsertEdgeLoopHover));
        EXPECT_FALSE(pick_result.command.ok);
	EXPECT_FALSE(pick_result.command.changed);
	EXPECT_TRUE(pick_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand cancel_command;
        cancel_command.command_id =
            qec::EditorRuntimeCommandId::CancelInsertEdgeLoop;
        cancel_command.settings = qec::InsertEdgeLoopPickRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(session, runtime, cancel_command);
	EXPECT_FALSE(cancel_result.command.ok);
	EXPECT_FALSE(cancel_result.command.changed);
	EXPECT_TRUE(cancel_result.command.message == "Runtime command settings do not match the command id.");
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeMirrorsSelectedMeshesAroundAWorldAxisThroughHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const int source_id = session.objects().front().id;
	const quader::QVec3 first_before = session.objects().front().document.vertices.front().position;

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime, mirror_axis_command(qec::TransformAxis::X));

        EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	const int copy_id = session.selected_object_ids().front();
	EXPECT_TRUE(copy_id != source_id);
	EXPECT_TRUE(session.history_status().can_undo);

	const auto copy_it = std::find_if(session.objects().begin(), session.objects().end(), [copy_id](const qec::PolygonObject& object) {
		return object.id == copy_id;
	});
	EXPECT_TRUE(copy_it != session.objects().end());
	EXPECT_FALSE(copy_it->document.vertices.empty());
	EXPECT_NEAR(copy_it->document.vertices.front().position.x, -first_before.x, 0.0001);

	const qec::HistoryRestoreResult undo = session.undo();
	EXPECT_TRUE(undo.applied);
	EXPECT_TRUE(session.objects().size() == 1U);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeFlipsSelectedMeshesInPlaceThroughHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const int source_id = session.objects().front().id;
	const quader::QVec3 first_before = session.objects().front().document.vertices.front().position;

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                flip_view_command(qec::ViewportFlipOrientation::Horizontal));

        EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	EXPECT_TRUE(session.selected_object_ids().front() == source_id);
	EXPECT_FALSE(session.objects().front().document.vertices.empty());
	EXPECT_NEAR(session.objects().front().document.vertices.front().position.x, -first_before.x, 0.0001);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsMirrorAndFlipQuickCommandsOutsideObjectMode) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
        session.set_component_selection(
            session.objects().front().id,
            qdr_test::vertex_selection({vertex_id}, vertex_id),
            qec::SelectionMode::Vertex);
        const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult mirror_result =
            qec::execute_runtime_command(
                session, runtime, mirror_axis_command(qec::TransformAxis::X));
        EXPECT_FALSE(mirror_result.command.ok);
	EXPECT_FALSE(mirror_result.command.changed);
	EXPECT_TRUE(mirror_result.command.message == "Select one or more mesh objects to mirror.");

        const qec::EditorRuntimeCommandResult flip_result =
            qec::execute_runtime_command(
                session, runtime,
                flip_view_command(qec::ViewportFlipOrientation::Vertical));
        EXPECT_FALSE(flip_result.command.ok);
	EXPECT_FALSE(flip_result.command.changed);
	EXPECT_TRUE(flip_result.command.message == "Select one or more mesh objects to flip.");

	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsMirrorAndFlipQuickCommandMismatchedSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;

	qec::EditorRuntimeCommand mirror_wrong_settings;
        mirror_wrong_settings.command_id =
            qec::EditorRuntimeCommandId::MirrorAxis;
        mirror_wrong_settings.settings = std::monostate {};
	const qec::EditorRuntimeCommandResult mirror_result =
		qec::execute_runtime_command(session, runtime, mirror_wrong_settings);
	EXPECT_FALSE(mirror_result.command.ok);
	EXPECT_FALSE(mirror_result.command.changed);
	EXPECT_TRUE(mirror_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand flip_wrong_settings;
        flip_wrong_settings.command_id = qec::EditorRuntimeCommandId::FlipView;
        flip_wrong_settings.settings = qec::MirrorAxisRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult flip_result =
		qec::execute_runtime_command(session, runtime, flip_wrong_settings);
	EXPECT_FALSE(flip_result.command.ok);
	EXPECT_FALSE(flip_result.command.changed);
	EXPECT_TRUE(flip_result.command.message == "Runtime command settings do not match the command id.");
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeDrivesBoxFootprintHeightAndCommit) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::size_t object_count_before = session.objects().size();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;
	EXPECT_TRUE(session.has_selection());

        const qec::EditorRuntimeCommandResult hover_result =
            qec::execute_runtime_command(
                session, runtime,
                box_ray_command(qec::EditorRuntimeCommandId::UpdateBoxHover));
        EXPECT_TRUE(hover_result.command.ok);
	EXPECT_TRUE(hover_result.command.changed);
	EXPECT_TRUE(runtime.status().box_active);
	EXPECT_TRUE(hover_result.runtime.box_active);

        const qec::EditorRuntimeCommandResult begin_result =
            qec::execute_runtime_command(
                session, runtime,
                box_ray_command(
                    qec::EditorRuntimeCommandId::BeginBoxFootprint));
        EXPECT_TRUE(begin_result.command.ok);
	EXPECT_TRUE(begin_result.command.changed);
        EXPECT_TRUE(runtime.box_state().drag_state ==
                    qec::BoxDragState::Footprint);
        EXPECT_FALSE(session.has_selection());
	EXPECT_TRUE(session.state().selection_revision != selection_revision_before);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult update_result =
            qec::execute_runtime_command(
                session, runtime,
                box_ray_command(qec::EditorRuntimeCommandId::UpdateBoxFootprint,
                                box_ground_ray(6.0F, 7.0F)));
        EXPECT_TRUE(update_result.command.ok);
        EXPECT_TRUE(runtime.box_state().drag_state ==
                    qec::BoxDragState::Footprint);
        EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult begin_height_result =
            qec::execute_runtime_command(
                session, runtime,
                box_height_command(
                    qec::EditorRuntimeCommandId::BeginBoxHeight));
        EXPECT_TRUE(begin_height_result.command.ok);
	EXPECT_TRUE(begin_height_result.command.changed);
        EXPECT_TRUE(runtime.box_state().drag_state ==
                    qec::BoxDragState::Height);
        const qec::BoxPreviewDescriptor preview = runtime.box_preview_descriptor(true);
	EXPECT_TRUE(preview.ok);
	EXPECT_FALSE(preview.bounds.empty);
	EXPECT_NEAR(preview.distance, 1.0F, 0.0001);

        const qec::EditorRuntimeCommandResult commit_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::CommitBox));
        EXPECT_TRUE(commit_result.command.ok);
	EXPECT_TRUE(commit_result.command.changed);
	EXPECT_FALSE(commit_result.runtime.box_active);
        EXPECT_TRUE(runtime.box_state().drag_state == qec::BoxDragState::Idle);
        EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
	EXPECT_TRUE(session.state().content_revision != content_revision_before);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeCreatesABoxFromBoundsThroughHistory) {
	qec::EditorSession session;
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, create_box_from_bounds_command());

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.objects().front().selected);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Object);
        EXPECT_TRUE(session.state().content_revision != content_revision_before);
	EXPECT_TRUE(session.state().selection_revision != selection_revision_before);
	EXPECT_TRUE(session.history_status().can_undo);
	EXPECT_FALSE(result.runtime.hover_active);
	EXPECT_FALSE(result.runtime.transform_drag_active);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsInvalidBoxBoundsWithoutHistory) {
	qec::EditorSession session;
	qec::EditorRuntime runtime;
	const qec::EditorRuntimeCommandResult failed =
		qec::execute_runtime_command(
			session,
			runtime,
			create_box_from_bounds_command({ 1.0F, 1.0F, 1.0F }, { 1.0F, 2.0F, 3.0F }));

	EXPECT_FALSE(failed.command.ok);
	EXPECT_FALSE(failed.command.changed);
	EXPECT_FALSE(session.history_status().can_undo);
	const qec::EditorRuntimeCommandResult created =
		qec::execute_runtime_command(session, runtime, create_box_from_bounds_command());

	EXPECT_TRUE(created.command.ok);
	EXPECT_TRUE(created.command.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.objects().front().id == 1);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeCancelsBoxWithoutHistory) {
	qec::EditorSession session;
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult begin_result =
            qec::execute_runtime_command(
                session, runtime,
                box_ray_command(
                    qec::EditorRuntimeCommandId::BeginBoxFootprint));
        EXPECT_TRUE(begin_result.command.ok);
	EXPECT_TRUE(runtime.status().box_active);

        const qec::EditorRuntimeCommandResult cancel_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::CancelBox));
        EXPECT_TRUE(cancel_result.command.ok);
	EXPECT_TRUE(cancel_result.command.changed);
	EXPECT_FALSE(cancel_result.runtime.box_active);
	EXPECT_FALSE(runtime.status().box_active);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsBoxCommitBeforeHeight) {
	qec::EditorSession session;
	qec::EditorRuntime runtime;

        const qec::EditorRuntimeCommandResult result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::CommitBox));
        EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Box height must be set before commit.");
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsBoxMismatchedSettings) {
	qec::EditorSession session;
	qec::EditorRuntime runtime;

        const qec::EditorRuntimeCommandResult hover_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::UpdateBoxHover));
        EXPECT_FALSE(hover_result.command.ok);
	EXPECT_FALSE(hover_result.command.changed);
	EXPECT_TRUE(hover_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand begin_height_wrong_settings;
        begin_height_wrong_settings.command_id =
            qec::EditorRuntimeCommandId::BeginBoxHeight;
        begin_height_wrong_settings.settings = qec::BoxRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult begin_height_result =
		qec::execute_runtime_command(session, runtime, begin_height_wrong_settings);
	EXPECT_FALSE(begin_height_result.command.ok);
	EXPECT_FALSE(begin_height_result.command.changed);
	EXPECT_TRUE(begin_height_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand commit_wrong_settings;
        commit_wrong_settings.command_id =
            qec::EditorRuntimeCommandId::CommitBox;
        commit_wrong_settings.settings = qec::BoxRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult commit_result =
		qec::execute_runtime_command(session, runtime, commit_wrong_settings);
	EXPECT_FALSE(commit_result.command.ok);
	EXPECT_FALSE(commit_result.command.changed);
	EXPECT_TRUE(commit_result.command.message == "Runtime command settings do not match the command id.");

        qec::EditorRuntimeCommand create_box_wrong_settings =
            no_settings_command(
                qec::EditorRuntimeCommandId::CreateBoxFromBounds);
        const qec::EditorRuntimeCommandResult create_box_result =
		qec::execute_runtime_command(session, runtime, create_box_wrong_settings);
	EXPECT_FALSE(create_box_result.command.ok);
	EXPECT_FALSE(create_box_result.command.changed);
	EXPECT_TRUE(create_box_result.command.message == "Runtime command settings do not match the command id.");
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeDrivesCutActivationModeAndCancelWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult activate_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::ActivateCut));
        EXPECT_TRUE(activate_result.command.ok);
	EXPECT_TRUE(activate_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() == qec::ViewportInteractionTool::Cut);
        EXPECT_TRUE(activate_result.runtime.active_tool ==
                    qec::ViewportInteractionTool::Cut);
        EXPECT_TRUE(activate_result.runtime.cut_active);
        EXPECT_TRUE(runtime.cut_state().mode == qec::CutMode::KeepBoth);
        EXPECT_TRUE(runtime.cut_state().target_object_ids.size() == 1U);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult update_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::UpdateCutPreview));
        EXPECT_TRUE(update_result.command.ok);
        EXPECT_TRUE(runtime.active_tool() == qec::ViewportInteractionTool::Cut);
        EXPECT_TRUE(update_result.runtime.cut_active);
	EXPECT_TRUE(runtime.cut_state().target_object_ids.size() == 1U);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult mode_result =
            qec::execute_runtime_command(session, runtime,
                                         set_cut_mode_command(static_cast<int>(
                                             qec::CutMode::KeepFront)));
        EXPECT_TRUE(mode_result.command.ok);
	EXPECT_TRUE(mode_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() == qec::ViewportInteractionTool::Cut);
        EXPECT_TRUE(runtime.cut_state().mode == qec::CutMode::KeepFront);
        EXPECT_TRUE(runtime.cut_state().target_object_ids.size() == 1U);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult cancel_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::CancelCut));
        EXPECT_TRUE(cancel_result.command.ok);
	EXPECT_TRUE(cancel_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_TRUE(cancel_result.runtime.active_tool ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_FALSE(cancel_result.runtime.cut_active);
	EXPECT_TRUE(runtime.cut_state().target_object_ids.empty());
        EXPECT_TRUE(runtime.cut_state().mode == qec::CutMode::KeepBoth);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsCutCommandsWithoutObjectSelection) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
        runtime.state().active_tool = qec::ViewportInteractionTool::Knife;
        runtime.state().knife.active = true;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult activate_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::ActivateCut));
        EXPECT_FALSE(activate_result.command.ok);
	EXPECT_FALSE(activate_result.command.changed);
	EXPECT_TRUE(activate_result.command.message == "Select one or more mesh objects to cut.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Knife);
        EXPECT_TRUE(runtime.state().knife.active);

        const qec::EditorRuntimeCommandResult click_result =
            qec::execute_runtime_command(
                session, runtime,
                cut_ray_command(qec::EditorRuntimeCommandId::ClickCut,
                                cut_ray(0.0F)));
        EXPECT_FALSE(click_result.command.ok);
	EXPECT_FALSE(click_result.command.changed);
	EXPECT_TRUE(click_result.command.message == "Select one or more mesh objects to cut.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Knife);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeDrivesCutInteractionsAndFinalize) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

        const qec::EditorRuntimeCommandResult first_click =
            qec::execute_runtime_command(
                session, runtime,
                cut_ray_command(qec::EditorRuntimeCommandId::ClickCut,
                                cut_ray(0.0F)));
        EXPECT_TRUE(first_click.command.ok);
	EXPECT_TRUE(first_click.command.changed);
        EXPECT_TRUE(runtime.active_tool() == qec::ViewportInteractionTool::Cut);
        EXPECT_TRUE(first_click.runtime.cut_active);
	EXPECT_TRUE(runtime.cut_state().points.size() == 1U);
	EXPECT_TRUE(runtime.cut_state().dragging_new_point);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult second_click =
            qec::execute_runtime_command(
                session, runtime,
                cut_ray_command(qec::EditorRuntimeCommandId::ClickCut,
                                cut_ray(1.0F)));
        EXPECT_TRUE(second_click.command.ok);
	EXPECT_TRUE(second_click.command.changed);
	EXPECT_TRUE(runtime.cut_state().points.size() == 2U);
	EXPECT_FALSE(runtime.cut_state().dragging_new_point);
	EXPECT_TRUE(runtime.cut_state().preview_valid);
	EXPECT_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult begin_point_drag =
		qec::execute_runtime_command(session, runtime, cut_point_drag_command(0));
	EXPECT_TRUE(begin_point_drag.command.ok);
	EXPECT_TRUE(begin_point_drag.command.changed);
	EXPECT_TRUE(runtime.cut_state().dragging_point_index == 0);

        const qec::EditorRuntimeCommandResult point_drag =
            qec::execute_runtime_command(
                session, runtime,
                cut_ray_command(qec::EditorRuntimeCommandId::UpdateCutHover,
                                cut_ray(-0.5F), true));
        EXPECT_TRUE(point_drag.command.ok);
	EXPECT_TRUE(point_drag.command.changed);
	EXPECT_TRUE(runtime.cut_state().points.size() == 2U);
	EXPECT_NEAR(runtime.cut_state().points[0].x, -0.5F, 0.0001);
	EXPECT_TRUE(runtime.cut_state().preview_valid);

        const qec::EditorRuntimeCommandResult end_point_drag =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::EndCutDrag));
        EXPECT_TRUE(end_point_drag.command.ok);
	EXPECT_TRUE(end_point_drag.command.changed);
	EXPECT_TRUE(runtime.cut_state().dragging_point_index == -1);
	EXPECT_FALSE(runtime.cut_state().dragging_line);

        const qec::EditorRuntimeCommandResult begin_line_drag =
            qec::execute_runtime_command(
                session, runtime,
                cut_ray_command(qec::EditorRuntimeCommandId::BeginCutLineDrag,
                                cut_ray(0.25F)));
        EXPECT_TRUE(begin_line_drag.command.ok);
	EXPECT_TRUE(begin_line_drag.command.changed);
	EXPECT_TRUE(runtime.cut_state().dragging_line);

        const qec::EditorRuntimeCommandResult line_drag =
            qec::execute_runtime_command(
                session, runtime,
                cut_ray_command(qec::EditorRuntimeCommandId::UpdateCutHover,
                                cut_ray(0.5F), true));
        EXPECT_TRUE(line_drag.command.ok);
	EXPECT_TRUE(line_drag.command.changed);
	EXPECT_TRUE(runtime.cut_state().points.size() == 2U);
	EXPECT_TRUE(runtime.cut_state().dragging_line);
	EXPECT_TRUE(runtime.cut_state().preview_valid);

        const qec::EditorRuntimeCommandResult end_line_drag =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::EndCutDrag));
        EXPECT_TRUE(end_line_drag.command.ok);
	EXPECT_TRUE(end_line_drag.command.changed);
	EXPECT_FALSE(runtime.cut_state().dragging_line);

        const qec::EditorRuntimeCommandResult finalize =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(qec::EditorRuntimeCommandId::FinalizeCut));
        EXPECT_TRUE(finalize.command.ok);
	EXPECT_TRUE(finalize.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_FALSE(finalize.runtime.cut_active);
	EXPECT_TRUE(runtime.cut_state().points.empty());
	EXPECT_TRUE(session.state().content_revision != content_revision_before);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsCutMismatchedSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	qec::EditorRuntimeCommand set_mode_missing_settings;
        set_mode_missing_settings.command_id =
            qec::EditorRuntimeCommandId::SetCutMode;
        set_mode_missing_settings.settings = std::monostate {};
	const qec::EditorRuntimeCommandResult set_mode_result =
		qec::execute_runtime_command(session, runtime, set_mode_missing_settings);
	EXPECT_FALSE(set_mode_result.command.ok);
	EXPECT_FALSE(set_mode_result.command.changed);
	EXPECT_TRUE(set_mode_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand cancel_extra_settings;
        cancel_extra_settings.command_id =
            qec::EditorRuntimeCommandId::CancelCut;
        cancel_extra_settings.settings = qec::SetCutModeRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult cancel_result =
		qec::execute_runtime_command(session, runtime, cancel_extra_settings);
	EXPECT_FALSE(cancel_result.command.ok);
	EXPECT_FALSE(cancel_result.command.changed);
	EXPECT_TRUE(cancel_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand hover_missing_settings;
        hover_missing_settings.command_id =
            qec::EditorRuntimeCommandId::UpdateCutHover;
        hover_missing_settings.settings = std::monostate {};
	const qec::EditorRuntimeCommandResult hover_result =
		qec::execute_runtime_command(session, runtime, hover_missing_settings);
	EXPECT_FALSE(hover_result.command.ok);
	EXPECT_FALSE(hover_result.command.changed);
	EXPECT_TRUE(hover_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand click_wrong_settings;
        click_wrong_settings.command_id = qec::EditorRuntimeCommandId::ClickCut;
        click_wrong_settings.settings = qec::CutPointDragRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult click_result =
		qec::execute_runtime_command(session, runtime, click_wrong_settings);
	EXPECT_FALSE(click_result.command.ok);
	EXPECT_FALSE(click_result.command.changed);
	EXPECT_TRUE(click_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand point_drag_wrong_settings;
        point_drag_wrong_settings.command_id =
            qec::EditorRuntimeCommandId::BeginCutPointDrag;
        point_drag_wrong_settings.settings = qec::CutRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult point_drag_result =
		qec::execute_runtime_command(session, runtime, point_drag_wrong_settings);
	EXPECT_FALSE(point_drag_result.command.ok);
	EXPECT_FALSE(point_drag_result.command.changed);
	EXPECT_TRUE(point_drag_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand line_drag_wrong_settings;
        line_drag_wrong_settings.command_id =
            qec::EditorRuntimeCommandId::BeginCutLineDrag;
        line_drag_wrong_settings.settings = qec::CutPointDragRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult line_drag_result =
		qec::execute_runtime_command(session, runtime, line_drag_wrong_settings);
	EXPECT_FALSE(line_drag_result.command.ok);
	EXPECT_FALSE(line_drag_result.command.changed);
	EXPECT_TRUE(line_drag_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand end_drag_extra_settings;
        end_drag_extra_settings.command_id =
            qec::EditorRuntimeCommandId::EndCutDrag;
        end_drag_extra_settings.settings = qec::CutRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult end_drag_result =
		qec::execute_runtime_command(session, runtime, end_drag_extra_settings);
	EXPECT_FALSE(end_drag_result.command.ok);
	EXPECT_FALSE(end_drag_result.command.changed);
	EXPECT_TRUE(end_drag_result.command.message == "Runtime command settings do not match the command id.");

	qec::EditorRuntimeCommand finalize_extra_settings;
        finalize_extra_settings.command_id =
            qec::EditorRuntimeCommandId::FinalizeCut;
        finalize_extra_settings.settings = qec::CutRayRuntimeCommandSettings {};
	const qec::EditorRuntimeCommandResult finalize_result =
		qec::execute_runtime_command(session, runtime, finalize_extra_settings);
	EXPECT_FALSE(finalize_result.command.ok);
	EXPECT_FALSE(finalize_result.command.changed);
	EXPECT_TRUE(finalize_result.command.message == "Runtime command settings do not match the command id.");

        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeDrivesMirrorFirstDragPreviewAndFinalize) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

        const qec::EditorRuntimeCommandResult activate_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::ActivateMirror));
        EXPECT_TRUE(activate_result.command.ok);
	EXPECT_TRUE(activate_result.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Mirror);
        EXPECT_TRUE(activate_result.runtime.mirror_active);
	EXPECT_TRUE(runtime.mirror_state().target_object_ids.size() == 1U);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult first_click =
            qec::execute_runtime_command(
                session, runtime,
                mirror_ray_command(qec::EditorRuntimeCommandId::ClickMirror,
                                   cut_ray(0.0F)));
        EXPECT_TRUE(first_click.command.ok);
	EXPECT_TRUE(first_click.command.changed);
	EXPECT_TRUE(runtime.mirror_state().plane.points.size() == 1U);
	EXPECT_TRUE(runtime.mirror_state().plane.dragging_new_point);
	EXPECT_FALSE(runtime.mirror_state().preview_valid);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult first_drag_preview =
            qec::execute_runtime_command(
                session, runtime,
                mirror_ray_command(
                    qec::EditorRuntimeCommandId::UpdateMirrorHover,
                    cut_ray(1.0F), true));
        EXPECT_TRUE(first_drag_preview.command.ok);
	EXPECT_TRUE(first_drag_preview.command.changed);
	EXPECT_TRUE(runtime.mirror_state().plane.points.size() == 2U);
	EXPECT_TRUE(runtime.mirror_state().plane.dragging_new_point);
	EXPECT_TRUE(runtime.mirror_state().preview_valid);
	EXPECT_TRUE(first_drag_preview.runtime.mirror_can_preview);
	EXPECT_TRUE(first_drag_preview.runtime.mirror_preview_valid);
	EXPECT_FALSE(session.history_status().can_undo);

        const qec::EditorRuntimeCommandResult end_new_point_drag =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::EndMirrorDrag));
        EXPECT_TRUE(end_new_point_drag.command.ok);
	EXPECT_TRUE(end_new_point_drag.command.changed);
	EXPECT_FALSE(runtime.mirror_state().plane.dragging_new_point);
	EXPECT_TRUE(runtime.mirror_state().preview_valid);

	const qec::EditorRuntimeCommandResult begin_point_drag =
		qec::execute_runtime_command(session, runtime, mirror_point_drag_command(0));
	EXPECT_TRUE(begin_point_drag.command.ok);
	EXPECT_TRUE(begin_point_drag.command.changed);
	EXPECT_TRUE(runtime.mirror_state().plane.dragging_point_index == 0);

        const qec::EditorRuntimeCommandResult point_drag =
            qec::execute_runtime_command(
                session, runtime,
                mirror_ray_command(
                    qec::EditorRuntimeCommandId::UpdateMirrorHover,
                    cut_ray(-0.5F), true));
        EXPECT_TRUE(point_drag.command.ok);
	EXPECT_TRUE(point_drag.command.changed);
	EXPECT_NEAR(runtime.mirror_state().plane.points[0].x, -0.5F, 0.0001);
	EXPECT_TRUE(runtime.mirror_state().preview_valid);

        const qec::EditorRuntimeCommandResult end_point_drag =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::EndMirrorDrag));
        EXPECT_TRUE(end_point_drag.command.ok);
	EXPECT_TRUE(end_point_drag.command.changed);
	EXPECT_TRUE(runtime.mirror_state().plane.dragging_point_index == -1);

        const qec::EditorRuntimeCommandResult finalize =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::FinalizeMirror));
        EXPECT_TRUE(finalize.command.ok);
	EXPECT_TRUE(finalize.command.changed);
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Select);
        EXPECT_FALSE(finalize.runtime.mirror_active);
	EXPECT_TRUE(runtime.mirror_state().plane.points.empty());
	EXPECT_TRUE(session.state().content_revision != content_revision_before);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsMirrorCommandsWithoutObjectSelection) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	session.clear_selection();
	qec::EditorRuntime runtime;
        runtime.state().active_tool = qec::ViewportInteractionTool::Poly;
        runtime.state().poly.active = true;
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

        const qec::EditorRuntimeCommandResult activate_result =
            qec::execute_runtime_command(
                session, runtime,
                no_settings_command(
                    qec::EditorRuntimeCommandId::ActivateMirror));
        EXPECT_FALSE(activate_result.command.ok);
	EXPECT_FALSE(activate_result.command.changed);
	EXPECT_TRUE(activate_result.command.message == "Select one or more mesh objects to mirror.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Poly);
        EXPECT_TRUE(runtime.state().poly.active);

        const qec::EditorRuntimeCommandResult click_result =
            qec::execute_runtime_command(
                session, runtime,
                mirror_ray_command(qec::EditorRuntimeCommandId::ClickMirror,
                                   cut_ray(0.0F)));
        EXPECT_FALSE(click_result.command.ok);
	EXPECT_FALSE(click_result.command.changed);
	EXPECT_TRUE(click_result.command.message == "Select one or more mesh objects to mirror.");
        EXPECT_TRUE(runtime.active_tool() ==
                    qec::ViewportInteractionTool::Poly);
        EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadePickSelectAppliesModifierPolicyWithoutHistory) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_selection();
        session.set_selection_mode(qec::SelectionMode::Face);
        session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult replace_result =
		qec::execute_runtime_command(session, runtime, pick_select_command());
	EXPECT_TRUE(replace_result.command.ok);
	EXPECT_TRUE(replace_result.command.changed);
        EXPECT_TRUE(session.selection_mode() == qec::SelectionMode::Face);
        EXPECT_TRUE(session.active_selection().kind ==
                    qec::ActiveSelectionKind::Face);
        EXPECT_TRUE(session.state().selection_revision != selection_revision_before);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

	const std::uint64_t selected_revision = session.state().selection_revision;
	const qec::EditorRuntimeCommandResult add_same_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray(), 1, false, true));
	EXPECT_TRUE(add_same_result.command.ok);
	EXPECT_FALSE(add_same_result.command.changed);
	EXPECT_TRUE(session.state().selection_revision == selected_revision);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

	const qec::EditorRuntimeCommandResult remove_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray(), 1, true, false));
	EXPECT_TRUE(remove_result.command.ok);
	EXPECT_TRUE(remove_result.command.changed);
	EXPECT_FALSE(session.has_selection());
	EXPECT_TRUE(session.state().selection_revision > selected_revision);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadePickSelectDragStrokesPaintComponentSelection) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_selection();
        session.set_selection_mode(qec::SelectionMode::Face);
        session.clear_history();
	const std::uint64_t content_revision_before = session.state().content_revision;

	const qec::EditorRuntimeCommandResult first_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray()));
	EXPECT_TRUE(first_result.command.ok);
	EXPECT_TRUE(first_result.command.changed);
	EXPECT_TRUE(selected_face_count(session) == 1U);

	const qec::EditorRuntimeCommandResult paint_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(top_center_ray(), 0));
	EXPECT_TRUE(paint_result.command.ok);
	EXPECT_TRUE(paint_result.command.changed);
	EXPECT_TRUE(selected_face_count(session) == 2U);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);

	const std::uint64_t painted_revision = session.state().selection_revision;
	const qec::EditorRuntimeCommandResult same_paint_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(top_center_ray(), 0));
	EXPECT_TRUE(same_paint_result.command.ok);
	EXPECT_FALSE(same_paint_result.command.changed);
	EXPECT_TRUE(session.state().selection_revision == painted_revision);
	EXPECT_TRUE(selected_face_count(session) == 2U);

	const qec::EditorRuntimeCommandResult remove_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(top_center_ray(), 0, true, false));
	EXPECT_TRUE(remove_result.command.ok);
	EXPECT_TRUE(remove_result.command.changed);
	EXPECT_TRUE(selected_face_count(session) == 1U);
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadePaintSelectIgnoresComponentModeFaceFallback) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	session.clear_selection();
        session.set_selection_mode(qec::SelectionMode::Vertex);
        session.clear_history();
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult face_interior_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_center_ray(), 0));
	EXPECT_TRUE(face_interior_result.command.ok);
	EXPECT_FALSE(face_interior_result.command.changed);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
	EXPECT_FALSE(session.has_selection());

	const qec::EditorRuntimeCommandResult vertex_result =
		qec::execute_runtime_command(session, runtime, pick_select_command(front_right_top_vertex_ray(), 0));
	EXPECT_TRUE(vertex_result.command.ok);
	EXPECT_TRUE(vertex_result.command.changed);
	EXPECT_TRUE(selected_vertex_count(session) == 1U);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeCtrlComponentClickIgnoresFaceFallback) {
	qec::EditorSession edge_session = qdr_test::selected_cube_session();
	qec::EditorRuntime edge_runtime;
	edge_session.clear_selection();
        edge_session.set_selection_mode(qec::SelectionMode::Edge);
        EXPECT_TRUE(qec::execute_runtime_command(edge_session, edge_runtime, pick_select_command(front_right_edge_ray())).command.changed);
	EXPECT_TRUE(selected_edge_count(edge_session) == 1U);
	const std::uint64_t edge_revision = edge_session.state().selection_revision;

	const qec::EditorRuntimeCommandResult edge_face_interior_result =
		qec::execute_runtime_command(edge_session, edge_runtime, pick_select_command(front_center_ray(), 1, true, false));
	EXPECT_TRUE(edge_face_interior_result.command.ok);
	EXPECT_FALSE(edge_face_interior_result.command.changed);
	EXPECT_TRUE(edge_session.state().selection_revision == edge_revision);
	EXPECT_TRUE(selected_edge_count(edge_session) == 1U);

	qec::EditorSession vertex_session = qdr_test::selected_cube_session();
	qec::EditorRuntime vertex_runtime;
	vertex_session.clear_selection();
        vertex_session.set_selection_mode(qec::SelectionMode::Vertex);
        EXPECT_TRUE(qec::execute_runtime_command(vertex_session, vertex_runtime, pick_select_command(front_right_top_vertex_ray())).command.changed);
	EXPECT_TRUE(selected_vertex_count(vertex_session) == 1U);
	const std::uint64_t vertex_revision = vertex_session.state().selection_revision;

	const qec::EditorRuntimeCommandResult vertex_face_interior_result =
		qec::execute_runtime_command(vertex_session, vertex_runtime, pick_select_command(front_center_ray(), 1, true, false));
	EXPECT_TRUE(vertex_face_interior_result.command.ok);
	EXPECT_FALSE(vertex_face_interior_result.command.changed);
	EXPECT_TRUE(vertex_session.state().selection_revision == vertex_revision);
	EXPECT_TRUE(selected_vertex_count(vertex_session) == 1U);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode) {
	qec::EditorSession face_session = qdr_test::selected_cube_session();
	qec::EditorRuntime face_runtime;
	face_session.clear_selection();
        face_session.set_selection_mode(qec::SelectionMode::Face);
        const qec::EditorRuntimeCommandResult face_result =
		qec::execute_runtime_command(face_session, face_runtime, pick_select_command(front_center_ray(), 2));
	EXPECT_TRUE(face_result.command.ok);
	EXPECT_TRUE(face_result.command.changed);
        EXPECT_TRUE(face_session.selection_mode() == qec::SelectionMode::Face);
        EXPECT_TRUE(face_session.active_selection().kind ==
                    qec::ActiveSelectionKind::Face);
        EXPECT_TRUE(face_result.command.changed == (face_session.state().selection_revision > 0U));
	EXPECT_FALSE(face_session.history_status().can_undo);

	qec::EditorSession edge_session = qdr_test::selected_cube_session();
	qec::EditorRuntime edge_runtime;
	edge_session.clear_selection();
        edge_session.set_selection_mode(qec::SelectionMode::Edge);
        const qec::EditorRuntimeCommandResult edge_result =
		qec::execute_runtime_command(edge_session, edge_runtime, pick_select_command({ { 0.0F, 1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } }, 2));
	EXPECT_TRUE(edge_result.command.ok);
        EXPECT_TRUE(edge_session.selection_mode() == qec::SelectionMode::Edge);
        EXPECT_FALSE(edge_session.history_status().can_undo);

	qec::EditorSession vertex_session = qdr_test::selected_cube_session();
	qec::EditorRuntime vertex_runtime;
	vertex_session.clear_selection();
        vertex_session.set_selection_mode(qec::SelectionMode::Vertex);
        const qec::EditorRuntimeCommandResult vertex_result =
		qec::execute_runtime_command(vertex_session, vertex_runtime, pick_select_command({ { 1.0F, 1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } }, 2));
	EXPECT_TRUE(vertex_result.command.ok);
        EXPECT_TRUE(vertex_session.selection_mode() ==
                    qec::SelectionMode::Vertex);
        EXPECT_FALSE(vertex_session.history_status().can_undo);
}

TEST(QdrModelingRuntimeCommandTests, EditorRuntimeCommandFacadeRejectsPickSelectWithMismatchedSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::EditorRuntime runtime;
	qec::EditorRuntimeCommand command;
        command.command_id = qec::EditorRuntimeCommandId::PickSelect;
        command.settings = std::monostate {};
	const std::uint64_t content_revision_before = session.state().content_revision;
	const std::uint64_t selection_revision_before = session.state().selection_revision;

	const qec::EditorRuntimeCommandResult result =
		qec::execute_runtime_command(session, runtime, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message == "Runtime command settings do not match the command id.");
	EXPECT_TRUE(session.state().content_revision == content_revision_before);
	EXPECT_TRUE(session.state().selection_revision == selection_revision_before);
}
