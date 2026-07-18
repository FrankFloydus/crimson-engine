////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/cut_tool.hpp>
#include <editor/modeling/runtime/editor_runtime.hpp>
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
	EXPECT_TRUE(runtime.cut_hover.position.y == 0.0F);
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
