////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <quader/modeling/neutral/polygon/document_builder.hpp>
#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

TEST(QdrModelingRuntimeToolBoxTests, BoxFootprintProgressesFromEmptyToLineToRectangle) {
	quader_editor_core::BoxToolState state;
	EXPECT_TRUE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);

	const quader_editor_core::BoxToolStateResult begin =
		quader_editor_core::begin_box_footprint(
			state,
			{ 0.2F, 0.0F, 0.2F },
			{ 0.2F, 0.0F, 0.2F },
			true,
			1.0);
	EXPECT_TRUE(begin.ok);
        EXPECT_TRUE(begin.state.drag_state ==
                    quader_editor_core::BoxDragState::Footprint);

        const quader_editor_core::BoxPreviewDescriptor zero_size =
		quader_editor_core::box_preview_descriptor(begin.state, true, 1.0);
	EXPECT_TRUE(zero_size.ok);
	EXPECT_TRUE(zero_size.footprint.footprint.empty());
	EXPECT_NEAR(zero_size.footprint.size.x, 0.0F, 0.0001);
	EXPECT_NEAR(zero_size.footprint.size.y, 0.0F, 0.0001);
	EXPECT_TRUE(zero_size.bounds.empty);

	const quader_editor_core::BoxToolStateResult line =
		quader_editor_core::update_box_footprint(
			state,
			{ 2.4F, 0.0F, 0.2F },
			true,
			1.0);
	EXPECT_TRUE(line.ok);
	const quader_editor_core::BoxPreviewDescriptor one_axis =
		quader_editor_core::box_preview_descriptor(line.state, true, 1.0);
	EXPECT_TRUE(one_axis.ok);
	ASSERT_EQ(one_axis.footprint.footprint.size(), 2U);
	EXPECT_NEAR(one_axis.footprint.size.x, 2.0F, 0.0001);
	EXPECT_NEAR(one_axis.footprint.size.y, 0.0F, 0.0001);
	EXPECT_NEAR(one_axis.footprint.signed_size.x, 2.0F, 0.0001);
	EXPECT_NEAR(one_axis.footprint.signed_size.y, 0.0F, 0.0001);
	EXPECT_FALSE(one_axis.bounds.empty);

	const quader_editor_core::BoxToolStateResult rectangle =
		quader_editor_core::update_box_footprint(
			state,
			{ 2.4F, 0.0F, 3.2F },
			true,
			1.0);
	EXPECT_TRUE(rectangle.ok);
	const quader_editor_core::BoxPreviewDescriptor two_axis =
		quader_editor_core::box_preview_descriptor(rectangle.state, true, 1.0);
	EXPECT_TRUE(two_axis.ok);
	ASSERT_EQ(two_axis.footprint.footprint.size(), 4U);
	EXPECT_NEAR(two_axis.footprint.size.x, 2.0F, 0.0001);
	EXPECT_NEAR(two_axis.footprint.size.y, 3.0F, 0.0001);
	EXPECT_NEAR(two_axis.footprint.signed_size.x, 2.0F, 0.0001);
	EXPECT_NEAR(two_axis.footprint.signed_size.y, -3.0F, 0.0001);
	EXPECT_FALSE(two_axis.bounds.empty);
	EXPECT_NEAR(two_axis.bounds.min.y, 0.0F, 0.0001);
	EXPECT_NEAR(two_axis.bounds.max.y, 0.0F, 0.0001);
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
}

TEST(QdrModelingRuntimeToolBoxTests, BoxConstructionRayHoverAndSyncPreservePreviewContract) {
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
		quader_editor_core::update_box_hover(state, { 33.0F, 0.0F, 33.0F }, true, 16.0);
	EXPECT_TRUE(hover.ok);
	EXPECT_NEAR(state.start_point.x, 32.0F, 0.0001);
	EXPECT_NEAR(state.start_point.z, 32.0F, 0.0001);
	EXPECT_NEAR(state.end_point.x, 32.0F, 0.0001);
	EXPECT_NEAR(state.end_point.z, 32.0F, 0.0001);
	const quader_editor_core::BoxPreviewDescriptor hover_preview =
		quader_editor_core::box_preview_descriptor(state, true, 16.0);
	EXPECT_TRUE(hover_preview.ok);
	EXPECT_TRUE(hover_preview.footprint.footprint.empty());
	EXPECT_TRUE(hover_preview.bounds.empty);

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
	quader_poly::DocumentBuilder builder;
	quader_editor_core::PolygonObject object;
	object.id = 1;
	object.document = builder.cube({ -1.0F, -2.0F, -1.0F },
	                               { 1.0F, -1.0F, 1.0F }).take();
	quader_editor_core::EditorSession session({ std::move(object) });

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

	std::vector<quader::modeling::Vec3> sdk_corners;
	sdk_corners.reserve(corners.size());
	for (const quader::QVec3 corner : corners) {
		sdk_corners.push_back({ corner.x, corner.y, corner.z });
	}
	quader::modeling::Result<quader::modeling::PolygonDocument> result =
		quader::modeling::PolygonDocument::make_box_from_corners(sdk_corners);
	ASSERT_TRUE(result.ok()) << result.error_message();
	const quader::modeling::PolygonDocument &document = result.value();
	EXPECT_EQ(document.vertex_count(), 8U);
	EXPECT_EQ(document.face_count(), 6U);
	quader::modeling::Result<quader::modeling::MeshPayload> compiled =
		document.compile_mesh();
	ASSERT_TRUE(compiled.ok()) << compiled.error_message();
	EXPECT_FALSE(compiled.value().indices.empty());
	quader::modeling::Result<quader::modeling::AuthoredPolygonPayload> authored =
		document.authored_payload();
	ASSERT_TRUE(authored.ok()) << authored.error_message();

	int base_vertex_count = 0;
	int top_vertex_count = 0;
	for (const quader::modeling::Vec3 position : authored.value().positions) {
		if (qdr_test::nearly_equal(position.x, 2.0F, 0.0001)) {
			++base_vertex_count;
		}
		if (qdr_test::nearly_equal(position.x, 4.0F, 0.0001)) {
			++top_vertex_count;
		}
	}
	EXPECT_TRUE(base_vertex_count == 4);
	EXPECT_TRUE(top_vertex_count == 4);

	bool found_base_face = false;
	bool found_top_face = false;
	const auto position_for = [&](quader::modeling::VertexId id) {
		const auto found = std::find(authored.value().vertices.begin(),
		                             authored.value().vertices.end(), id);
		EXPECT_NE(found, authored.value().vertices.end());
		if (found == authored.value().vertices.end()) {
			return quader::modeling::Vec3{};
		}
		return authored.value().positions[static_cast<std::size_t>(
			std::distance(authored.value().vertices.begin(), found))];
	};
	for (const quader::modeling::AuthoredPolygonFacePayload &face :
	     authored.value().faces) {
		bool all_base_x = true;
		bool all_top_x = true;
		for (const quader::modeling::VertexId vertex_id : face.vertices) {
			const quader::modeling::Vec3 position = position_for(vertex_id);
			all_base_x = all_base_x &&
				qdr_test::nearly_equal(position.x, 2.0F, 0.0001);
			all_top_x = all_top_x &&
				qdr_test::nearly_equal(position.x, 4.0F, 0.0001);
		}
		if (all_base_x) {
			found_base_face = true;
		}
		if (all_top_x) {
			found_top_face = true;
		}
	}
	EXPECT_TRUE(found_base_face);
	EXPECT_TRUE(found_top_face);
}
