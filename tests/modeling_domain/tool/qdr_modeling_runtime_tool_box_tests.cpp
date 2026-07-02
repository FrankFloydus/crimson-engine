////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/interaction_api.hpp>

#include <optional>
#include <vector>

TEST(QdrModelingRuntimeToolBoxTests, BoxFootprintSnapsDirectionalPairsAndPreservesSignedSize) {
	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);

	const quader_editor_core::BoxToolStateResult begin =
		quader_editor_core::begin_box_footprint(
			state,
			{ 0.2F, 0.0F, 0.2F },
			{ 1.2F, 0.0F, 2.8F },
			true,
			1.0);
	EXPECT_TRUE(begin.ok);
        EXPECT_TRUE(begin.state.drag_state ==
                    quader_editor_core::BoxDragState::Footprint);

        const quader_editor_core::BoxPreviewDescriptor descriptor =
		quader_editor_core::box_preview_descriptor(begin.state, true, 1.0);
	EXPECT_TRUE(descriptor.ok);
	EXPECT_TRUE(descriptor.footprint.footprint.size() == 4);
	EXPECT_NEAR(descriptor.footprint.size.x, 2.0F, 0.0001);
	EXPECT_NEAR(descriptor.footprint.size.y, 3.0F, 0.0001);
	EXPECT_FALSE(descriptor.bounds.empty);
	EXPECT_NEAR(descriptor.bounds.min.y, 0.0F, 0.0001);
	EXPECT_NEAR(descriptor.bounds.max.y, 0.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolBoxTests, BoxRuntimeResetHelperRestoresDefaultGridSize) {
	quader_editor_core::EditorRuntimeState runtime;
	runtime.box.preview_valid = true;
	runtime.box_grid_size = 9.0;

	quader_editor_core::reset_editor_runtime_box_state(runtime);
	EXPECT_FALSE(quader_editor_core::editor_runtime_status(runtime).box_active);
        EXPECT_NEAR(runtime.box_grid_size,
                    quader_editor_core::kDefaultEditorRuntimeBoxGridSize,
                    0.0001);
}

TEST(QdrModelingRuntimeToolBoxTests, BoxHeightPreviewSnapsToGridAndFlipsNormalForNegativeHeight) {
	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::begin_box_footprint(
			state,
			{ 0.0F, 0.0F, 0.0F },
			{ 1.0F, 0.0F, 1.0F },
			true,
			1.0).ok);

	EXPECT_TRUE(quader_editor_core::begin_box_height(state, { 100.0F, 100.0F }, 0.0, true, true, 1.0).ok);
	const quader_editor_core::BoxToolStateResult updated =
		quader_editor_core::update_box_height_preview(state, -3.0, true, { 100.0F, 120.0F }, true, 1.0);
	EXPECT_TRUE(updated.ok);
        EXPECT_TRUE(updated.state.drag_state ==
                    quader_editor_core::BoxDragState::Height);
        EXPECT_NEAR(updated.state.height_distance, -2.0, 0.0001);

	const quader_editor_core::BoxPreviewDescriptor descriptor =
		quader_editor_core::box_preview_descriptor(updated.state, true, 1.0);
	EXPECT_TRUE(descriptor.ok);
	EXPECT_NEAR(descriptor.distance, 2.0, 0.0001);
	EXPECT_NEAR(descriptor.normal.y, -1.0F, 0.0001);
	EXPECT_NEAR(descriptor.bounds.min.y, -2.0F, 0.0001);
	EXPECT_NEAR(descriptor.bounds.max.y, 0.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolBoxTests, BoxHeightScreenFallbackUsesVerticalMotionWhenAxisDistanceIsUnavailable) {
	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }).ok);
	EXPECT_TRUE(quader_editor_core::begin_box_footprint(
			state,
			{ 0.0F, -1.0F, -1.0F },
			{ 0.0F, 1.0F, 1.0F },
			false,
			1.0).ok);

	EXPECT_TRUE(quader_editor_core::begin_box_height(
			state,
			{ 400.0F, 300.0F },
			0.0,
			false,
			true,
			1.0).ok);

	const quader_editor_core::BoxToolStateResult downward =
		quader_editor_core::update_box_height_preview(
			state,
			0.0,
			false,
			{ 400.0F, 396.0F },
			true,
			1.0);
	EXPECT_TRUE(downward.ok);
	EXPECT_NEAR(downward.state.height_distance, -1.0, 0.0001);

	const quader_editor_core::BoxToolStateResult sideways =
		quader_editor_core::update_box_height_preview(
			state,
			0.0,
			false,
			{ 496.0F, 300.0F },
			true,
			1.0);
	EXPECT_TRUE(sideways.ok);
	EXPECT_NEAR(sideways.state.height_distance, 1.0, 0.0001);

	EXPECT_TRUE(quader_editor_core::begin_box_height(
			state,
			{ 400.0F, 300.0F },
			1.0,
			true,
			true,
			1.0).ok);
	const quader_editor_core::BoxToolStateResult axis_distance =
		quader_editor_core::update_box_height_preview(
			state,
			4.0,
			true,
			{ 496.0F, 300.0F },
			true,
			1.0);
	EXPECT_TRUE(axis_distance.ok);
	EXPECT_NEAR(axis_distance.state.height_distance, 4.0, 0.0001);
}

TEST(QdrModelingRuntimeToolBoxTests, BoxPreviewStateAndResetClearTransientBrushIdentity) {
	quader_editor_core::BoxToolState state;
        state.drag_state = quader_editor_core::BoxDragState::Height;
        quader_editor_core::set_box_preview_state(state, 42, true);
	EXPECT_TRUE(state.preview_brush_id == 42);
	EXPECT_TRUE(state.preview_valid);

	quader_editor_core::clear_box_preview_state(state);
	EXPECT_TRUE(state.preview_brush_id == -1);
	EXPECT_FALSE(state.preview_valid);

	quader_editor_core::reset_box(state);
        EXPECT_TRUE(state.drag_state == quader_editor_core::BoxDragState::Idle);
        EXPECT_TRUE(state.preview_brush_id == -1);
	EXPECT_FALSE(state.preview_valid);
	EXPECT_TRUE(state.hover_cell_points.empty());
}

TEST(QdrModelingRuntimeToolBoxTests, BoxConstructionRayHoverCellAndSyncPreservePreviewContract) {
	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);

	const quader_editor_core::BoxConstructionPointResult point =
		quader_editor_core::box_construction_point_from_ray(
			state.plane,
			{ { 8.0F, 64.0F, 24.0F }, { 0.0F, -1.0F, 0.0F } });
	EXPECT_TRUE(point.ok);
	EXPECT_NEAR(point.position.y, 0.0F, 0.0001);
	EXPECT_NEAR(point.distance, 64.0, 0.0001);

	const quader_editor_core::BoxToolStateResult hover =
		quader_editor_core::update_box_hover_cell(state, { 33.0F, 0.0F, 33.0F }, true, 16.0);
	EXPECT_TRUE(hover.ok);
	EXPECT_TRUE(state.hover_cell_points.size() == 4);
	EXPECT_NEAR(state.hover_cell_points[0].x, 32.0F, 0.0001);
	EXPECT_NEAR(state.hover_cell_points[2].x, 48.0F, 0.0001);

	EXPECT_TRUE(quader_editor_core::begin_box_footprint(
			state,
			{ 0.0F, 0.0F, 0.0F },
			{ 32.0F, 0.0F, 32.0F },
			true,
			16.0).ok);
	EXPECT_TRUE(quader_editor_core::begin_box_height(state, { 100.0F, 100.0F }, 0.0, false, true, 16.0).ok);
	EXPECT_TRUE(quader_editor_core::update_box_height_preview(state, 0.0, false, { 100.0F, 244.0F }, true, 16.0).ok);
	quader_editor_core::set_box_preview_state(state, 42, true);

	const quader_editor_core::BoxPreviewDescriptor descriptor =
		quader_editor_core::box_preview_descriptor(state, true, 16.0);
	EXPECT_TRUE(descriptor.ok);
	EXPECT_NEAR(descriptor.signed_height, -32.0, 0.0001);
	EXPECT_NEAR(descriptor.distance, 32.0, 0.0001);
	EXPECT_NEAR(descriptor.normal.y, -1.0F, 0.0001);
	EXPECT_TRUE(descriptor.preview_brush_id == 42);
	EXPECT_TRUE(descriptor.preview_valid);

	quader_editor_core::BoxToolState copied;
	quader_editor_core::sync_box_state(copied, state);
	EXPECT_TRUE(copied.preview_brush_id == 42);
	EXPECT_NEAR(copied.plane.normal.y, 1.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolBoxTests, BoxConstructionPlaneCanBeSeededFromAMeshSurfaceBelowTheGrid) {
	quader_editor_core::EditorSession session;
	EXPECT_TRUE(session.create_box_from_bounds({ -1.0F, -2.0F, -1.0F }, { 1.0F, -1.0F, 1.0F }).changed);

	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Face;
        const quader_poly::Ray ray { { 0.0F, 4.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	const std::optional<quader_editor_core::PickHitRecord> hit =
		session.pick_nearest({ ray, options });
	EXPECT_TRUE(hit.has_value());
	EXPECT_NEAR(hit->local_position.y, -1.0F, 0.0001);
	EXPECT_NEAR(hit->local_normal.y, 1.0F, 0.0001);

	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::set_box_construction_plane(state, hit->local_position, hit->local_normal).ok);
	const quader_editor_core::BoxConstructionPointResult point =
		quader_editor_core::box_construction_point_from_ray(state.plane, ray);
	EXPECT_TRUE(point.ok);
	EXPECT_NEAR(point.position.y, -1.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolBoxTests, BoxPreviewCornersCommitAWallPlanarOrientedVolume) {
	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::set_box_construction_plane(state, { 2.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }).ok);
	EXPECT_TRUE(quader_editor_core::begin_box_footprint(
			state,
			{ 2.0F, 0.0F, 0.0F },
			{ 2.0F, -2.0F, 3.0F },
			false,
			1.0).ok);
        state.drag_state = quader_editor_core::BoxDragState::Height;
        state.height_distance = 2.0;

	const quader_editor_core::BoxPreviewDescriptor preview =
		quader_editor_core::box_preview_descriptor(state, false, 1.0);
	EXPECT_TRUE(preview.ok);
	const std::vector<quader::QVec3> corners = quader_editor_core::box_preview_corners(preview);
	EXPECT_TRUE(corners.size() == 8);

	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult result = session.create_box_from_corners(corners);
	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 1);
	const quader_poly::Document& document = session.objects().front().document;
	EXPECT_TRUE(document.vertices.size() == 8);
	EXPECT_TRUE(document.faces.size() == 6);
	EXPECT_FALSE(quader_poly::compile_document(document).indices.empty());

	int base_vertex_count = 0;
	int top_vertex_count = 0;
	for (const quader_poly::Vertex& vertex : document.vertices) {
		if (qdr_test::nearly_equal(vertex.position.x, 2.0F, 0.0001)) {
			++base_vertex_count;
		}
		if (qdr_test::nearly_equal(vertex.position.x, 4.0F, 0.0001)) {
			++top_vertex_count;
		}
	}
	EXPECT_TRUE(base_vertex_count == 4);
	EXPECT_TRUE(top_vertex_count == 4);

	bool found_base_face = false;
	bool found_top_face = false;
	for (const quader_poly::Face& face : document.faces) {
		bool all_base_x = true;
		bool all_top_x = true;
		for (quader_poly::ElementId vertex_id : face.vertices) {
			const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
			EXPECT_TRUE(vertex != nullptr);
			all_base_x = all_base_x &&qdr_test::nearly_equal(vertex->position.x, 2.0F, 0.0001);
			all_top_x = all_top_x &&qdr_test::nearly_equal(vertex->position.x, 4.0F, 0.0001);
		}
		const quader::QVec3 normal = quader_poly::face_normal(document, face);
		if (all_base_x) {
			found_base_face = true;
			EXPECT_NEAR(normal.x, -1.0F, 0.0001);
		}
		if (all_top_x) {
			found_top_face = true;
			EXPECT_NEAR(normal.x, 1.0F, 0.0001);
		}
	}
	EXPECT_TRUE(found_base_face);
	EXPECT_TRUE(found_top_face);
}
