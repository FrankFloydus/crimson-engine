////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/cut_tool.hpp>
#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/interactions/interaction_api.hpp>

#include <vector>

TEST(QdrModelingRuntimeToolCutTests, CutToolCapturesTargetObjectsFromCurrentSelection) {
	quader_editor_core::RectangleSelectionState selection;
	selection.selected_object_ids = { 4, 8 };
	selection.active_object_id = 4;

	quader_editor_core::CutToolState state;
	const quader_editor_core::CutToolResult first =
		quader_editor_core::capture_first_cut_point(state, selection, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	EXPECT_TRUE(first.ok);
	EXPECT_TRUE(first.changed);
	EXPECT_TRUE(first.target_count == 2);
	EXPECT_TRUE(state.target_object_ids.size() == 2);
	EXPECT_TRUE(state.target_object_ids[0] == 4);
	EXPECT_TRUE(state.target_object_ids[1] == 8);
	EXPECT_TRUE(state.source_object_id == -1);
	EXPECT_TRUE(state.dragging_new_point);
	EXPECT_TRUE(quader_editor_core::cut_is_active(state));
	EXPECT_TRUE(quader_editor_core::cut_is_dragging(state));
}

TEST(QdrModelingRuntimeToolCutTests, CutAndKnifeRuntimeHelpersKeepSeparateOwnership) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool =
            quader_editor_core::ViewportInteractionTool::Knife;
        runtime.knife.active = true;
	runtime.knife.has_current = true;

	quader_editor_core::reset_editor_runtime_knife_state(runtime, true);
	EXPECT_TRUE(runtime.knife.active);
	EXPECT_FALSE(runtime.knife.has_current);

	quader_editor_core::set_editor_runtime_knife_active(runtime, false);
	EXPECT_FALSE(runtime.knife.active);
	quader_editor_core::set_editor_runtime_knife_active(runtime, true);
	EXPECT_TRUE(runtime.knife.active);

	quader_editor_core::EditorRuntime editor_runtime;
	quader_editor_core::KnifeToolState knife;
	knife.active = true;
	knife.has_current = true;
        knife.status = quader_editor_core::KnifeToolStatus::Preview;
        editor_runtime.set_knife_state(knife);
	EXPECT_TRUE(editor_runtime.status().knife_active);
	EXPECT_TRUE(editor_runtime.status().knife_has_current);

	editor_runtime.set_knife_active(false);
	EXPECT_FALSE(editor_runtime.status().knife_active);

        runtime.active_tool = quader_editor_core::ViewportInteractionTool::Cut;
        runtime.cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	runtime.cut.target_object_ids = { 1 };
	runtime.cut_hover.valid = true;
	runtime.cut_hover.object_id = 1;
	runtime.cut_hover.position = { 0.5F, 0.0F, 0.0F };
	EXPECT_TRUE(quader_editor_core::editor_runtime_status(runtime).cut_active);
	EXPECT_TRUE(quader_editor_core::editor_runtime_status(runtime).cut_can_preview);

        runtime.active_tool =
            quader_editor_core::ViewportInteractionTool::Select;
        quader_editor_core::reset_editor_runtime_cut_state(runtime, false);
	EXPECT_FALSE(quader_editor_core::editor_runtime_status(runtime).cut_active);
	EXPECT_FALSE(runtime.cut_hover.valid);
	EXPECT_TRUE(runtime.cut_hover.object_id == -1);
}

TEST(QdrModelingRuntimeToolCutTests, CutHoverIsDurableEditorRuntimeStateClearedByActivationResets) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool = quader_editor_core::ViewportInteractionTool::Cut;
        runtime.cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	runtime.cut.target_object_ids = { 1 };
	runtime.cut_hover.valid = true;
	runtime.cut_hover.object_id = 1;
	runtime.cut_hover.position = { 0.5F, 0.0F, 0.0F };

	quader_editor_core::EditorRuntimeToolActivationRequest request;
        request.requested_tool =
            quader_editor_core::ViewportInteractionTool::Knife;
        const quader_editor_core::EditorRuntimeToolActivationResult result =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);

	EXPECT_TRUE(result.applied);
	EXPECT_TRUE(result.plan.reset_cut_tool);
        EXPECT_TRUE(runtime.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Knife);
        EXPECT_FALSE(quader_editor_core::cut_is_active(runtime.cut));
	EXPECT_FALSE(runtime.cut_hover.valid);
	EXPECT_TRUE(runtime.cut_hover.object_id == -1);
}

TEST(QdrModelingRuntimeToolCutTests, CutToolUsesTwoSnappedWorldGridPoints) {
	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Face;
        selection.active_object_id = 5;
	selection.selected_faces = { { 5, 2 } };

	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
        const quader_editor_core::CutGridPointResult grid_point =
		quader_editor_core::cut_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5);
	EXPECT_TRUE(grid_point.ok);
	EXPECT_TRUE(grid_point.position.x == 0.5F);
	EXPECT_TRUE(grid_point.position.y == 0.0F);
	EXPECT_TRUE(grid_point.position.z == 1.5F);

	quader_editor_core::CutToolState state;
	EXPECT_TRUE(quader_editor_core::capture_first_cut_point(state, selection, grid_point.position, grid_point.plane_normal).ok);
	EXPECT_FALSE(quader_editor_core::capture_second_cut_point(state, grid_point.position).ok);
	EXPECT_TRUE(quader_editor_core::capture_second_cut_point(state, { 1.5F, 0.0F, 1.5F }).ok);

	EXPECT_TRUE(state.points.size() == 2);
	EXPECT_TRUE(state.target_object_ids.size() == 1);
	EXPECT_TRUE(state.target_object_ids[0] == 5);
	EXPECT_TRUE(quader_editor_core::cut_can_preview(state));
	const std::vector<quader::QVec3> plane_points = quader_editor_core::cut_plane_points(state);
	EXPECT_TRUE(plane_points.size() == 3);
	EXPECT_TRUE(plane_points[2].y > plane_points[0].y);

	const std::vector<quader::QVec3> overlay_points =
		quader_editor_core::cut_overlay_points(state, { 2.0F, 0.0F, 2.0F }, true);
	EXPECT_TRUE(overlay_points.size() == 2);
	EXPECT_TRUE(overlay_points[0].x == state.points[0].x);
	EXPECT_TRUE(overlay_points[0].y == state.points[0].y);
	EXPECT_TRUE(overlay_points[0].z == state.points[0].z);
	EXPECT_TRUE(overlay_points[1].x == state.points[1].x);
	EXPECT_TRUE(overlay_points[1].y == state.points[1].y);
	EXPECT_TRUE(overlay_points[1].z == state.points[1].z);
}

TEST(QdrModelingRuntimeToolCutTests, CutRayPolicyUpdatesRuntimeOwnedCutStateAndHoverTogether) {
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Object;
        selection.selected_object_ids = { 7 };
	selection.active_object_id = 7;

	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool = quader_editor_core::ViewportInteractionTool::Cut;

        quader_editor_core::CutRayUpdateInput first_input;
        first_input.camera.projection =
            quader_editor_core::CameraProjection::Perspective;
        first_input.ray = { { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	first_input.grid_size = 0.5;

	const quader_editor_core::CutToolResult hover_result =
		quader_editor_core::update_cut_hover_from_ray(
			runtime,
			selection,
			objects,
			first_input);
	EXPECT_TRUE(hover_result.ok);
	EXPECT_TRUE(hover_result.changed);
	EXPECT_TRUE(runtime.cut_hover.valid);
	EXPECT_TRUE(runtime.cut_hover.position.x == 0.0F);
	EXPECT_TRUE(runtime.cut_hover.position.y == 1.0F);
	EXPECT_TRUE(runtime.cut_hover.position.z == 0.0F);
	EXPECT_TRUE(runtime.cut.target_object_ids.size() == 1U);
	EXPECT_TRUE(runtime.cut.target_object_ids[0] == 7);

	const quader_editor_core::CutToolResult first_click =
		quader_editor_core::click_cut_from_ray(
			runtime,
			selection,
			objects,
			first_input);
	EXPECT_TRUE(first_click.ok);
	EXPECT_TRUE(runtime.cut.points.size() == 1U);
	EXPECT_TRUE(runtime.cut.dragging_new_point);
	EXPECT_TRUE(runtime.cut_hover.valid);

	quader_editor_core::CutRayUpdateInput second_input = first_input;
	second_input.ray = { { 1.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	const quader_editor_core::CutToolResult second_click =
		quader_editor_core::click_cut_from_ray(
			runtime,
			selection,
			objects,
			second_input);
	EXPECT_TRUE(second_click.ok);
	EXPECT_TRUE(runtime.cut.points.size() == 2U);
	EXPECT_FALSE(runtime.cut.dragging_new_point);
	EXPECT_TRUE(runtime.cut.dragging_point_index == 1);
	EXPECT_TRUE(quader_editor_core::cut_preview_metadata(runtime.cut).preview_valid);
}

TEST(QdrModelingRuntimeToolCutTests, CutPreviewMetadataInvalidatesOnModeAndPointChanges) {
	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	state.target_object_ids = { 1 };

        const quader_editor_core::CutPreviewRequest request =
            quader_editor_core::cut_preview_request(
                state, quader_editor_core::CutMode::KeepBoth);
        EXPECT_TRUE(request.ok);
	EXPECT_TRUE(request.can_preview);
	EXPECT_TRUE(request.point_count == 2);
	EXPECT_TRUE(request.target_count == 1);
	EXPECT_TRUE(request.object_ids[0] == 1);

	quader_editor_core::CutPreviewCounts counts;
	counts.target_count = 1;
	counts.split_count = 1;
	counts.removed_count = 0;
        EXPECT_TRUE(
            quader_editor_core::set_cut_preview_result(
                state, quader_editor_core::CutMode::KeepBoth, true, counts)
                .ok);
        EXPECT_TRUE(quader_editor_core::cut_preview_metadata(state).preview_valid);

        const quader_editor_core::CutToolResult mode_change =
            quader_editor_core::set_cut_mode(
                state, quader_editor_core::CutMode::KeepFront);
        EXPECT_TRUE(mode_change.changed);
	EXPECT_FALSE(quader_editor_core::cut_preview_metadata(state).preview_valid);

	const quader_editor_core::CutToolResult committed =
		quader_editor_core::commit_reset_cut(state, { 2, 1 });
	EXPECT_TRUE(committed.ok);
	EXPECT_TRUE(committed.target_count == 1);
	EXPECT_TRUE(committed.dirty_object_ids.size() == 2);
	EXPECT_FALSE(quader_editor_core::cut_is_active(state));
}

TEST(QdrModelingRuntimeToolCutTests, CutPreviewPolicyRetainsOnlyHostPlaneAndTargetState) {
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { 7 };
        state.mode = quader_editor_core::CutMode::KeepBoth;

        const quader_editor_core::CutToolResult result =
		quader_editor_core::refresh_cut_preview(state, objects);
	EXPECT_TRUE(result.ok);

	const quader_editor_core::CutPreviewMetadata metadata =
		quader_editor_core::cut_preview_metadata(state);
	EXPECT_TRUE(metadata.preview_valid);
	EXPECT_TRUE(metadata.target_count == 1);
	EXPECT_TRUE(metadata.split_count == 0);
	EXPECT_TRUE(metadata.removed_count == 0);
}

TEST(QdrModelingRuntimeToolCutTests, CutLineDragMovesTheWholePlacedPlane) {
	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 1.0F, 0.0F };
	state.target_object_ids = { 1 };
	state.preview_valid = true;

	EXPECT_TRUE(quader_editor_core::begin_cut_line_drag(state, { 0.5F, 0.0F, 0.0F }).ok);
	EXPECT_TRUE(quader_editor_core::cut_is_dragging(state));

	const quader_editor_core::CutToolResult moved =
		quader_editor_core::update_dragged_cut_line(state, { 1.5F, 0.0F, 0.0F });
	EXPECT_TRUE(moved.ok);
	EXPECT_TRUE(moved.changed);
	EXPECT_TRUE(state.points.size() == 2U);
	EXPECT_TRUE(state.points[0].x == 1.0F);
	EXPECT_TRUE(state.points[0].y == 0.0F);
	EXPECT_TRUE(state.points[0].z == 0.0F);
	EXPECT_TRUE(state.points[1].x == 2.0F);
	EXPECT_TRUE(state.points[1].y == 0.0F);
	EXPECT_TRUE(state.points[1].z == 0.0F);
	EXPECT_TRUE(state.face_normal.y == 1.0F);
	EXPECT_FALSE(state.preview_valid);

	EXPECT_TRUE(quader_editor_core::end_cut_line_drag(state).ok);
	EXPECT_FALSE(quader_editor_core::cut_is_dragging(state));
}

TEST(QdrModelingRuntimeToolCutTests, CapturedFaceScopeIgnoresLaterSelectionChanges) {
  quader_editor_core::RectangleSelectionState selection;
  selection.mode = quader_editor_core::SelectionMode::Face;
  selection.selected_object_ids = {9};
  selection.selected_faces = {{4, 12}, {4, 18}, {7, 23}};
  quader_editor_core::EditorRuntimeState runtime;
  ASSERT_TRUE(quader_editor_core::set_cut_targets_from_selection(runtime.cut, selection).ok);
  EXPECT_EQ(runtime.cut.scope, quader_editor_core::CutScope::Face);
  EXPECT_EQ(runtime.cut.target_object_ids, (std::vector<int>{4, 7}));

  selection.mode = quader_editor_core::SelectionMode::Object;
  selection.selected_object_ids = {42};
  selection.selected_faces.clear();
  quader_editor_core::CutRayUpdateInput input;
  input.ray = {{0.0F, 5.0F, 0.0F}, {0.0F, -1.0F, 0.0F}};
  input.surface_hit_resolved = true;
  ASSERT_TRUE(quader_editor_core::update_cut_hover_from_ray(runtime, selection, {}, input).ok);
  ASSERT_TRUE(quader_editor_core::click_cut_from_ray(runtime, selection, {}, input).ok);
  EXPECT_EQ(runtime.cut.scope, quader_editor_core::CutScope::Face);
  EXPECT_EQ(runtime.cut.target_object_ids, (std::vector<int>{4, 7}));
  ASSERT_EQ(runtime.cut.target_faces.size(), 3U);
  EXPECT_EQ(runtime.cut.target_faces[0].face_index, 12);
  EXPECT_EQ(runtime.cut.target_faces[1].face_index, 18);
  EXPECT_EQ(runtime.cut.target_faces[2].face_index, 23);
}

TEST(QdrModelingRuntimeToolCutTests, EdgeScopeRejectsDiscardWithoutChangingAcceptedPreview) {
  quader_editor_core::RectangleSelectionState selection;
  selection.mode = quader_editor_core::SelectionMode::Edge;
  selection.selected_edges = {{4, 3, 8}};
  quader_editor_core::CutToolState state;
  ASSERT_TRUE(quader_editor_core::set_cut_targets_from_selection(state, selection).ok);
  ASSERT_EQ(state.scope, quader_editor_core::CutScope::Edge);
  state.preview_valid = true;
  state.preview_counts.split_count = 1;
  EXPECT_FALSE(quader_editor_core::set_cut_mode(state, quader_editor_core::CutMode::KeepFront).ok);
  EXPECT_FALSE(quader_editor_core::cut_preview_request(state, quader_editor_core::CutMode::KeepBack).ok);
  EXPECT_EQ(state.mode, quader_editor_core::CutMode::KeepBoth);
  EXPECT_TRUE(state.preview_valid);
  EXPECT_EQ(state.preview_counts.split_count, 1);
  ASSERT_EQ(state.target_edges.size(), 1U);
  EXPECT_EQ(state.target_edges.front().a_index, 3);
  EXPECT_EQ(state.target_edges.front().b_index, 8);
}

TEST(QdrModelingRuntimeToolCutTests, VertexScopeDoesNotStartCut) {
  quader_editor_core::RectangleSelectionState selection;
  selection.mode = quader_editor_core::SelectionMode::Vertex;
  selection.selected_object_ids = {4};
  selection.selected_vertices = {{4, 3}};
  quader_editor_core::CutToolState state;
  EXPECT_FALSE(quader_editor_core::capture_first_cut_point(
      state, selection, {}, {0.0F, 1.0F, 0.0F}).ok);
  EXPECT_FALSE(state.scope_captured);
  EXPECT_TRUE(state.points.empty());
  EXPECT_TRUE(state.target_object_ids.empty());
}

TEST(QdrModelingRuntimeToolCutTests, SurfacePlanePersistsForSecondPointAndBothDragGestures) {
  quader_editor_core::RectangleSelectionState selection;
  selection.selected_object_ids = {4};
  quader_editor_core::EditorRuntimeState runtime;
  quader_editor_core::CutRayUpdateInput input;
  input.ray = {{0.2F, 8.0F, 0.2F}, {0.0F, -1.0F, 0.0F}};
  input.grid_size = 1.0;
  input.snap_enabled = true;
  input.surface_hit_resolved = true;
  input.resolved_surface_hit = quader_editor_core::PickHitRecord{
      .hit = true,
      .object_id = 44,
      .local_position = {0.2F, 3.25F, 0.2F},
      .local_normal = {0.0F, 1.0F, 0.0F},
  };
  ASSERT_TRUE(quader_editor_core::click_cut_from_ray(runtime, selection, {}, input).ok);
  ASSERT_EQ(runtime.cut.points.size(), 1U);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].y, 3.25F);
  EXPECT_EQ(runtime.cut.source_object_id, 44);

  input.ray.origin.x = 2.1F;
  input.resolved_surface_hit->local_position.y = 7.0F;
  input.resolved_surface_hit->local_normal = {1.0F, 0.0F, 0.0F};
  input.camera.projection = quader_editor_core::CameraProjection::Orthographic;
  input.camera.transform.z_axis = {1.0F, 0.0F, 0.0F};
  ASSERT_TRUE(quader_editor_core::click_cut_from_ray(runtime, selection, {}, input).ok);
  ASSERT_EQ(runtime.cut.points.size(), 2U);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].x, 2.0F);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].y, 3.25F);

  ASSERT_TRUE(quader_editor_core::begin_cut_point_drag(runtime.cut, 0).ok);
  input.dragging = true;
  input.ray.origin.x = -1.1F;
  ASSERT_TRUE(quader_editor_core::update_cut_hover_from_ray(runtime, selection, {}, input).ok);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].x, -1.0F);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].y, 3.25F);
  EXPECT_FLOAT_EQ(runtime.cut.face_normal.y, 1.0F);

  input.ray.origin.x = 1.0F;
  ASSERT_TRUE(quader_editor_core::begin_cut_line_drag_from_ray(runtime, input).ok);
  input.ray.origin.x = 2.0F;
  ASSERT_TRUE(quader_editor_core::update_cut_hover_from_ray(runtime, selection, {}, input).ok);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].x, 0.0F);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].x, 3.0F);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].y, 3.25F);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].y, 3.25F);
}

TEST(QdrModelingRuntimeToolCutTests, TiltedSurfaceSnappingPreservesTheSurfacePlane) {
  const quader::QVec3 normal = quader::normalize(quader::QVec3{0.0F, 1.0F, 1.0F});
  const auto point = quader_editor_core::cut_support_plane_point_from_ray(
      {{0.3F, 0.2F, 5.0F}, {0.0F, 0.0F, -1.0F}},
      {0.0F, 0.0F, 2.25F}, normal, 1.0);
  ASSERT_TRUE(point.ok);
  EXPECT_NEAR(point.position.y + point.position.z, 2.25F, 0.00001F);
  EXPECT_FLOAT_EQ(point.position.x, 0.0F);
}

TEST(QdrModelingRuntimeToolCutTests, AuthoritativeSurfaceMissKeepsTheGridFallback) {
  quader_editor_core::PolygonObject object;
  object.id = 4;
  object.document = quader_poly::make_cube({});
  const std::vector<quader_editor_core::PolygonObject> objects{object};
  quader_editor_core::RectangleSelectionState selection;
  selection.selected_object_ids = {4};
  quader_editor_core::EditorRuntimeState runtime;
  quader_editor_core::CutRayUpdateInput input;
  input.ray = {{0.0F, 5.0F, 0.0F}, {0.0F, -1.0F, 0.0F}};
  input.surface_hit_resolved = true;
  ASSERT_TRUE(quader_editor_core::click_cut_from_ray(runtime, selection, objects, input).ok);
  ASSERT_EQ(runtime.cut.points.size(), 1U);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].y, 0.0F);
  EXPECT_EQ(runtime.cut.source_object_id, -1);
}

TEST(QdrModelingRuntimeToolCutTests, RuntimeActivationCapturesComponentsBeforeHover) {
  quader_editor_core::PolygonObject object;
  object.id = 4;
  object.selected = true;
  object.document = quader_poly::make_cube({});
  object.selection.mode = quader_poly::SelectionMode::Face;
  object.selection.faces = {object.document.faces.front().id};
  std::vector<quader_editor_core::PolygonObject> objects{object};
  quader_editor_core::EditorRuntimeSelectionView selection{
      .object_records = &objects,
      .mode = quader_editor_core::SelectionMode::Face,
      .active_object_id = 4,
  };
  quader_editor_core::EditorRuntime runtime;
  const auto activated = quader_editor_core::execute_runtime_command(
      selection, runtime, {.command_id = quader_editor_core::EditorRuntimeCommandId::ActivateCut});
  ASSERT_TRUE(activated.command.ok);
  ASSERT_TRUE(runtime.cut_state().scope_captured);
  ASSERT_EQ(runtime.cut_state().target_faces.size(), 1U);
  const int captured_face = runtime.cut_state().target_faces.front().face_index;
  objects.front().selection.faces = {object.document.faces.back().id};
  const auto reactivated = quader_editor_core::execute_runtime_command(
      selection, runtime, {.command_id = quader_editor_core::EditorRuntimeCommandId::ActivateCut});
  EXPECT_TRUE(reactivated.command.ok);
  EXPECT_FALSE(reactivated.command.changed);
  EXPECT_EQ(runtime.cut_state().target_faces.front().face_index, captured_face);
}

TEST(QdrModelingRuntimeToolCutTests, EndpointDragIsContinuousWithTemporaryGridAndAxisConstraints) {
  quader_editor_core::EditorRuntimeState runtime;
  runtime.cut.points = {{0.2F, 3.25F, 0.3F}, {2.2F, 3.25F, 0.3F}};
  runtime.cut.face_normal = {0.0F, 1.0F, 0.0F};
  runtime.cut.support_plane_origin = runtime.cut.points.front();
  runtime.cut.support_plane_valid = true;
  runtime.cut.scope_captured = true;
  runtime.cut.target_object_ids = {4};
  ASSERT_TRUE(quader_editor_core::begin_cut_point_drag(runtime.cut, 1).ok);
  quader_editor_core::CutRayUpdateInput input;
  input.ray = {{2.37F, 8.0F, 0.66F}, {0.0F, -1.0F, 0.0F}};
  input.grid_size = 1.0;
  input.dragging = true;
  const auto update = [&] {
    return quader_editor_core::update_cut_hover_from_ray(runtime, {}, {}, input);
  };
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[1].x, 2.37F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[1].z, 0.66F, 0.00001F);
  EXPECT_FALSE(update().changed);
  input.snap_enabled = true;
  ASSERT_TRUE(update().ok);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].x, 2.0F);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].z, 1.0F);
  input.snap_enabled = false;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[1].x, 2.37F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[1].z, 0.66F, 0.00001F);
  input.constrain_axis = true;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[1].x, 2.37F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[1].z, 0.3F, 0.00001F);
  input.snap_enabled = true;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[1].x, 2.0F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[1].z, 0.3F, 0.00001F);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].y, 3.25F);
  input.snap_enabled = false;
  input.constrain_axis = false;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[1].z, 0.66F, 0.00001F);
}

TEST(QdrModelingRuntimeToolCutTests, LineDragKeepsCursorOffsetAndDoesNotAccumulateSnapDrift) {
  quader_editor_core::EditorRuntimeState runtime;
  runtime.cut.points = {{0.2F, 3.25F, 0.3F}, {2.2F, 3.25F, 0.3F}};
  runtime.cut.face_normal = {0.0F, 1.0F, 0.0F};
  runtime.cut.support_plane_origin = runtime.cut.points.front();
  runtime.cut.support_plane_valid = true;
  runtime.cut.scope_captured = true;
  runtime.cut.target_object_ids = {4};
  quader_editor_core::CutRayUpdateInput input;
  input.ray = {{1.1F, 8.0F, 0.42F}, {0.0F, -1.0F, 0.0F}};
  input.grid_size = 1.0;
  ASSERT_TRUE(quader_editor_core::begin_cut_line_drag_from_ray(runtime, input).ok);
  input.dragging = true;
  const auto update = [&] {
    return quader_editor_core::update_cut_hover_from_ray(runtime, {}, {}, input);
  };
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[0].z, 0.3F, 0.00001F);
  input.ray.origin.x += 0.37F;
  input.ray.origin.z += 0.18F;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[0].x, 0.57F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[0].z, 0.48F, 0.00001F);
  input.snap_enabled = true;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[0].x, 1.0F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[0].z, 0.0F, 0.00001F);
  input.snap_enabled = false;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[0].x, 0.57F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[0].z, 0.48F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[1].x - runtime.cut.points[0].x, 2.0F, 0.00001F);
  input.constrain_axis = true;
  ASSERT_TRUE(update().ok);
  EXPECT_NEAR(runtime.cut.points[0].x, 0.57F, 0.00001F);
  EXPECT_NEAR(runtime.cut.points[0].z, 0.3F, 0.00001F);
  EXPECT_FLOAT_EQ(runtime.cut.points[0].y, 3.25F);
}

TEST(QdrModelingRuntimeToolCutTests, ParallelAndBehindSupportPlaneRaysPreserveAcceptedPoints) {
  quader_editor_core::EditorRuntimeState runtime;
  runtime.cut.points = {{0.0F, 3.25F, 0.0F}, {2.0F, 3.25F, 0.0F}};
  runtime.cut.face_normal = {0.0F, 1.0F, 0.0F};
  runtime.cut.scope_captured = true;
  runtime.cut.target_object_ids = {4};
  ASSERT_TRUE(quader_editor_core::begin_cut_point_drag(runtime.cut, 1).ok);
  quader_editor_core::CutRayUpdateInput input;
  input.ray = {{0.0F, 8.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
  input.dragging = true;
  EXPECT_FALSE(quader_editor_core::update_cut_hover_from_ray(runtime, {}, {}, input).ok);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].x, 2.0F);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].y, 3.25F);
  input.ray.direction = {0.0F, 1.0F, 0.0F};
  EXPECT_FALSE(quader_editor_core::update_cut_hover_from_ray(runtime, {}, {}, input).ok);
  EXPECT_FLOAT_EQ(runtime.cut.points[1].x, 2.0F);
}

TEST(QdrModelingRuntimeToolCutTests, RuntimeEndpointDragRetainsThePressOffset) {
  quader_editor_core::EditorRuntime runtime;
  ASSERT_TRUE(runtime.activate_tool({
      .requested_tool = quader_editor_core::ViewportInteractionTool::Cut,
      .selected_object_count = 1,
      .selected_cut_target_object_count = 1,
  }).applied);
  quader_editor_core::CutToolState state;
  state.points = {{0.2F, 3.25F, 0.3F}, {2.2F, 3.25F, 0.3F}};
  state.face_normal = {0.0F, 1.0F, 0.0F};
  state.support_plane_origin = state.points.front();
  state.support_plane_valid = true;
  state.scope_captured = true;
  state.target_object_ids = {4};
  runtime.set_cut_state(state);
  quader_editor_core::CutRayRuntimeCommandSettings settings;
  settings.ray = {{0.27F, 8.0F, 0.31F}, {0.0F, -1.0F, 0.0F}};
  const quader_editor_core::EditorRuntimeSelectionView selection;
  const auto update = [&] {
    return quader_editor_core::execute_runtime_command(selection, runtime, {
        .command_id = quader_editor_core::EditorRuntimeCommandId::UpdateCutHover,
        .settings = settings,
    });
  };
  ASSERT_TRUE(update().command.ok);
  ASSERT_TRUE(quader_editor_core::execute_runtime_command(selection, runtime, {
      .command_id = quader_editor_core::EditorRuntimeCommandId::BeginCutPointDrag,
      .settings = quader_editor_core::CutPointDragRuntimeCommandSettings{.point_index = 0},
  }).command.ok);
  settings.dragging = true;
  ASSERT_TRUE(update().command.ok);
  EXPECT_NEAR(runtime.cut_state().points.front().x, 0.2F, 0.00001F);
  EXPECT_NEAR(runtime.cut_state().points.front().z, 0.3F, 0.00001F);
  settings.ray.origin.x += 0.37F;
  settings.ray.origin.z += 0.18F;
  ASSERT_TRUE(update().command.ok);
  EXPECT_NEAR(runtime.cut_state().points.front().x, 0.57F, 0.00001F);
  EXPECT_NEAR(runtime.cut_state().points.front().z, 0.48F, 0.00001F);
  EXPECT_FLOAT_EQ(runtime.cut_state().points.front().y, 3.25F);
}
