////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <modeling/runtime/editor_runtime.h>
#include <modeling/interactions/interaction_api.h>

#include <vector>

TEST_CASE("mirror tool captures object targets from current selection", "[editor-core][tool][mirror]")
{
	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::object;
	selection.selected_object_ids = { 4, 8 };
	selection.active_object_id = 4;

	quader_editor_core::MirrorToolState state;
	const quader_editor_core::MirrorToolResult first =
		quader_editor_core::capture_first_mirror_point(state, selection, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	REQUIRE(first.ok);
	CHECK(first.changed);
	CHECK(first.target_count == 2);
	REQUIRE(state.target_object_ids.size() == 2U);
	CHECK(state.target_object_ids[0] == 4);
	CHECK(state.target_object_ids[1] == 8);
	CHECK(state.plane.dragging_new_point);
	CHECK(quader_editor_core::mirror_is_active(state));
	CHECK(quader_editor_core::mirror_is_dragging(state));
}

TEST_CASE("mirror target policy is object mode only", "[editor-core][tool][mirror]")
{
	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::face;
	selection.active_object_id = 5;
	selection.selected_faces = { { 5, 2 } };

	quader_editor_core::MirrorToolState state;
	const quader_editor_core::MirrorToolResult result =
		quader_editor_core::capture_first_mirror_point(state, selection, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });

	REQUIRE(result.ok);
	CHECK(state.target_object_ids.empty());
	CHECK_FALSE(quader_editor_core::mirror_can_preview(state));
}

TEST_CASE("mirror tool uses two snapped world-grid points", "[editor-core][tool][mirror]")
{
	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::object;
	selection.selected_object_ids = { 5 };
	selection.active_object_id = 5;

	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	const quader_editor_core::CutGridPointResult grid_point =
		quader_editor_core::cut_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5);
	REQUIRE(grid_point.ok);

	quader_editor_core::MirrorToolState state;
	REQUIRE(quader_editor_core::capture_first_mirror_point(state, selection, grid_point.position, grid_point.plane_normal).ok);
	CHECK_FALSE(quader_editor_core::capture_second_mirror_point(state, grid_point.position).ok);
	REQUIRE(quader_editor_core::capture_second_mirror_point(state, { 1.5F, 0.0F, 1.5F }).ok);

	REQUIRE(state.plane.points.size() == 2U);
	REQUIRE(state.target_object_ids.size() == 1U);
	CHECK(state.target_object_ids[0] == 5);
	CHECK(quader_editor_core::mirror_can_preview(state));
	const std::vector<quader_poly::Vec3> plane_points = quader_editor_core::mirror_plane_points(state);
	REQUIRE(plane_points.size() == 3U);
	CHECK(plane_points[2].y > plane_points[0].y);
}

TEST_CASE("mirror preview policy evaluates target meshes outside native transport", "[editor-core][tool][mirror]")
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { 7 };

	const quader_editor_core::MirrorToolResult result =
		quader_editor_core::refresh_mirror_preview(state, objects);
	REQUIRE(result.ok);

	const quader_editor_core::MirrorPreviewMetadata metadata =
		quader_editor_core::mirror_preview_metadata(state);
	CHECK(metadata.preview_valid);
	CHECK(metadata.target_count == 1);
	CHECK(metadata.mirrored_count == 1);

	const std::vector<quader_poly::Document> documents =
		quader_editor_core::mirror_preview_documents(state, objects);
	REQUIRE(documents.size() == 1U);
	CHECK_FALSE(quader_poly::compile_document(documents.front()).vertices.empty());
}

TEST_CASE("mirror preview validates on the first drag after the first point", "[editor-core][tool][mirror]")
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::object;
	selection.selected_object_ids = { 7 };
	selection.active_object_id = 7;

	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;

	quader_editor_core::EditorRuntime runtime;
	quader_editor_core::MirrorRayUpdateInput first_input;
	first_input.camera = camera;
	first_input.ray = { { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	first_input.grid_size = 0.5;

	const quader_editor_core::MirrorToolResult first =
		runtime.click_mirror_from_ray(selection, objects, first_input);
	REQUIRE(first.ok);
	CHECK(first.changed);
	REQUIRE(runtime.mirror_state().plane.points.size() == 1U);
	CHECK(runtime.mirror_state().plane.dragging_new_point);
	CHECK_FALSE(runtime.mirror_state().preview_valid);

	quader_editor_core::MirrorRayUpdateInput drag_input = first_input;
	drag_input.ray = { { 1.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	drag_input.dragging = true;

	const quader_editor_core::MirrorToolResult drag =
		runtime.update_mirror_hover_from_ray(selection, objects, drag_input);
	REQUIRE(drag.ok);
	CHECK(drag.changed);
	REQUIRE(runtime.mirror_state().plane.points.size() == 2U);
	CHECK(runtime.mirror_state().plane.dragging_new_point);

	const quader_editor_core::MirrorPreviewMetadata metadata =
		quader_editor_core::mirror_preview_metadata(runtime.mirror_state());
	CHECK(metadata.can_preview);
	CHECK(metadata.preview_valid);
	CHECK(metadata.target_count == 1);

	const std::vector<quader_poly::Document> documents =
		runtime.mirror_preview_documents(objects);
	REQUIRE(documents.size() == 1U);
	CHECK_FALSE(quader_poly::compile_document(documents.front()).vertices.empty());
}

TEST_CASE("mirror line drag moves the whole placed plane", "[editor-core][tool][mirror]")
{
	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 1.0F, 0.0F };
	state.target_object_ids = { 1 };
	state.preview_valid = true;

	REQUIRE(quader_editor_core::begin_mirror_line_drag(state, { 0.5F, 0.0F, 0.0F }).ok);
	CHECK(quader_editor_core::mirror_is_dragging(state));

	const quader_editor_core::MirrorToolResult moved =
		quader_editor_core::update_dragged_mirror_line(state, { 1.5F, 0.0F, 0.0F });
	REQUIRE(moved.ok);
	CHECK(moved.changed);
	REQUIRE(state.plane.points.size() == 2U);
	CHECK(state.plane.points[0].x == 1.0F);
	CHECK(state.plane.points[1].x == 2.0F);
	CHECK_FALSE(state.preview_valid);

	REQUIRE(quader_editor_core::end_mirror_line_drag(state).ok);
	CHECK_FALSE(quader_editor_core::mirror_is_dragging(state));
}
