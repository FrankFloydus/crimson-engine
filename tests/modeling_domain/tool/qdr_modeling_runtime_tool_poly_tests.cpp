////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/interaction_api.hpp>

TEST(QdrModelingRuntimeToolPolyTests, ViewportGridPointPlacementCanSnapOrPreserveRawPositions) {
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;

	const quader_editor_core::CutGridPointResult snapped =
		quader_editor_core::viewport_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5,
			true);
	EXPECT_TRUE(snapped.ok);
	EXPECT_NEAR(snapped.position.x, 0.5F, 0.0001);
	EXPECT_NEAR(snapped.position.y, 0.0F, 0.0001);
	EXPECT_NEAR(snapped.position.z, 1.5F, 0.0001);

	const quader_editor_core::CutGridPointResult unsnapped =
		quader_editor_core::viewport_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5,
			false);
	EXPECT_TRUE(unsnapped.ok);
	EXPECT_NEAR(unsnapped.position.x, 0.49F, 0.0001);
	EXPECT_NEAR(unsnapped.position.y, 0.0F, 0.0001);
	EXPECT_NEAR(unsnapped.position.z, 1.51F, 0.0001);
}

TEST(QdrModelingRuntimeToolPolyTests, PolyToolPreviewsPlacedVerticesStraightenedHoverLineAndClosure) {
	quader_editor_core::PolyToolState state;
	state.active = true;

	EXPECT_TRUE(quader_editor_core::update_poly_hover(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, false, 1.0, true).ok);
	EXPECT_TRUE(quader_editor_core::accept_poly_current_point(state).ok);
	EXPECT_TRUE(quader_editor_core::update_poly_hover(state, { 1.0F, 0.0F, 0.4F }, { 0.0F, 1.0F, 0.0F }, true, 1.0, true).ok);
	EXPECT_NEAR(state.current_point.x, 1.0F, 0.0001);
	EXPECT_NEAR(state.current_point.z, 0.0F, 0.0001);
	EXPECT_TRUE(quader_editor_core::accept_poly_current_point(state).ok);
	EXPECT_TRUE(quader_editor_core::update_poly_hover(state, { 1.0F, 0.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, false, 1.0, true).ok);
	EXPECT_TRUE(quader_editor_core::accept_poly_current_point(state).ok);

	EXPECT_TRUE(state.points.size() == 3);
	EXPECT_TRUE(quader_editor_core::poly_can_commit(state));
	quader_editor_core::PolyToolPreview preview = quader_editor_core::poly_tool_preview(state);
	EXPECT_TRUE(preview.active);
	EXPECT_TRUE(preview.finalizable);
	EXPECT_TRUE(preview.point_count == 3U);
	EXPECT_TRUE(preview.line_count == 2U);

	EXPECT_TRUE(quader_editor_core::update_poly_hover(state, { 0.005F, 0.0F, 0.005F }, { 0.0F, 1.0F, 0.0F }, false, 1.0, false).ok);
	EXPECT_TRUE(quader_editor_core::accept_poly_current_point(state).ok);
	EXPECT_TRUE(state.closed);
	EXPECT_FALSE(state.has_current);
	preview = quader_editor_core::poly_tool_preview(state);
	EXPECT_TRUE(preview.closed);
	EXPECT_TRUE(preview.line_count == 3U);
}

TEST(QdrModelingRuntimeToolPolyTests, PolyToolCommitsAPolygonObjectThroughEditorHistory) {
	quader_editor_core::EditorSession session;
	session.set_objects({});
	quader_editor_core::PolyToolState state;
	state.active = true;
	state.face_normal = { 0.0F, 1.0F, 0.0F };
	state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 1.0F },
		{ 0.0F, 0.0F, 1.0F },
	};
	quader_editor_core::rebuild_poly_overlay(state);

	const quader_editor_core::CommandResult result = session.commit_poly_tool(state);
	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.objects()[0].selected);
	EXPECT_TRUE(session.objects()[0].document.vertices.size() == 4U);
	EXPECT_TRUE(session.objects()[0].document.faces.size() == 1U);
	EXPECT_FALSE(state.active);

	quader_editor_core::PolyToolState short_state;
	short_state.active = true;
	short_state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	const quader_editor_core::CommandResult short_result = session.commit_poly_tool(short_state);
	EXPECT_FALSE(short_result.ok);
	EXPECT_TRUE(short_state.points.size() == 2U);
}
