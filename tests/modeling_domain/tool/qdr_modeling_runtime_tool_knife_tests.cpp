////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <array>
#include <cmath>
#include <string>

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/knife_tool.hpp>
#include <editor/interactions/tool_snapping.hpp>
#include <editor/modeling/runtime/tool_registry.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <mesh/polygon/document.hpp>

TEST(QdrModelingRuntimeToolKnifeTests, KnifeToolExposesStableRegistryContractAndResetOwnership) {
	const quader_editor_core::EditorTool& tool = quader_editor_core::knife_tool();
        EXPECT_TRUE(tool.tool_id() ==
                    quader_editor_core::ViewportInteractionTool::Knife);
        EXPECT_TRUE(std::string(tool.id()) == "knife");
	EXPECT_TRUE(std::string(tool.label()) == "Knife");
        EXPECT_TRUE(quader_editor_core::editor_tool_for_kind(
                        quader_editor_core::ViewportInteractionTool::Knife) ==
                    &tool);
        EXPECT_TRUE(quader_editor_core::editor_tool_for_id("knife") == &tool);

	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool =
            quader_editor_core::ViewportInteractionTool::Knife;
        runtime.knife.active = true;
	runtime.knife.has_current = true;
	runtime.knife.preview_points.push_back({});

	tool.reset_runtime_state(runtime, true);
	EXPECT_TRUE(runtime.knife.active);
	EXPECT_FALSE(runtime.knife.has_current);
	EXPECT_TRUE(runtime.knife.preview_points.empty());

	tool.reset_runtime_state(runtime, false);
	EXPECT_FALSE(runtime.knife.active);
}

TEST(QdrModelingRuntimeToolKnifeTests, KnifePreviewReportsStrokeOverlayState) {
	quader_editor_core::KnifeToolState state;
	state.active = true;
	state.has_active_stroke = true;
	state.has_current = true;
	state.current_target_valid = true;
	state.target_object_id = 7;
	state.previous_point_index = 1;
        state.points = {
            {quader_poly::KnifePointTargetKind::FacePoint},
            {quader_poly::KnifePointTargetKind::FacePoint},
        };
        state.point_positions = {
		{ 0.0F, 1.0F, 0.0F },
		{ 1.0F, 1.0F, 0.0F },
	};
	state.current_point = { 1.0F, 1.0F, 1.0F };
	state.segments.push_back({ 0, 1 });
	state.intersection_preview_points.push_back({ 0.5F, 1.0F, 0.5F });

	quader_editor_core::rebuild_knife_overlay(state);
	const quader_editor_core::KnifeToolPreview preview = quader_editor_core::knife_tool_preview(state);

	EXPECT_TRUE(preview.active);
	EXPECT_TRUE(preview.has_active_stroke);
	EXPECT_TRUE(preview.has_current);
	EXPECT_TRUE(preview.target_object_id == 7);
	EXPECT_NEAR(preview.previous_point.x, 1.0F, 0.0001);
	EXPECT_NEAR(preview.current_point.z, 1.0F, 0.0001);
	EXPECT_TRUE(preview.point_count == 2U);
	EXPECT_TRUE(preview.segment_count == 1U);
	EXPECT_TRUE(preview.preview_point_count == 4U);
	EXPECT_TRUE(preview.preview_line_count == 2U);
	EXPECT_TRUE(preview.can_commit);
}

TEST(QdrModelingRuntimeToolKnifeTests, KnifeSnappingAndAcceptFlowStayInEditorCore) {
	const quader::QVec3 snapped =
		quader_editor_core::snap_knife_point_to_grid({ 0.49F, 0.26F, -0.49F }, 0.5, true);
	EXPECT_NEAR(snapped.x, 0.5F, 0.0001);
	EXPECT_NEAR(snapped.y, 0.5F, 0.0001);
	EXPECT_NEAR(snapped.z, -0.5F, 0.0001);

	const quader::QVec3 raw =
		quader_editor_core::snap_knife_point_to_grid({ 0.49F, 0.26F, -0.49F }, 0.5, false);
	EXPECT_NEAR(raw.x, 0.49F, 0.0001);
	EXPECT_NEAR(raw.y, 0.26F, 0.0001);
	EXPECT_NEAR(raw.z, -0.49F, 0.0001);

	const quader::QVec3 constrained =
		quader_editor_core::constrain_knife_point_to_axis(
			{ 0.0F, 0.0F, 0.0F },
			{ 1.2F, 0.0F, 0.4F },
			{ 0.0F, 1.0F, 0.0F },
			0.5,
			true);
	EXPECT_NEAR(constrained.x, 1.0F, 0.0001);
	EXPECT_NEAR(constrained.y, 0.0F, 0.0001);
	EXPECT_NEAR(constrained.z, 0.0F, 0.0001);

	quader_editor_core::EditorSession session;
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	session.set_objects({ object });

	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;

        EXPECT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		{ { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } },
		camera,
		0.5,
		true,
		false));
	EXPECT_TRUE(state.active);
	EXPECT_TRUE(state.has_current);
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.target_object_id == 7);
        EXPECT_TRUE(state.status ==
                    quader_editor_core::KnifeToolStatus::Preview);
        EXPECT_NEAR(state.current_point.y, 1.0F, 0.0001);

	EXPECT_TRUE(quader_editor_core::accept_knife_current_point(session, state, camera, 0.5));
	EXPECT_TRUE(state.has_active_stroke);
	EXPECT_TRUE(state.points.size() == 1U);
	EXPECT_FALSE(state.has_current);
        EXPECT_TRUE(state.status ==
                    quader_editor_core::KnifeToolStatus::PreviousSet);

        EXPECT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		{ { 0.5F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } },
		camera,
		0.5,
		true,
		false));
	EXPECT_TRUE(quader_editor_core::accept_knife_current_point(session, state, camera, 0.5));
	EXPECT_TRUE(state.has_active_stroke);
	EXPECT_TRUE(state.points.size() == 2U);
	EXPECT_TRUE(state.segments.size() == 1U);
	EXPECT_TRUE(state.segments[0].first_point == 0U);
	EXPECT_TRUE(state.segments[0].second_point == 1U);
	EXPECT_FALSE(state.preview_lines.empty());
}

TEST(QdrModelingRuntimeToolKnifeTests, KnifeSnappedFacePointsStayProjectedOntoTiltedMeshSurfaces) {
	quader_poly::Document document;
	const quader_poly::ElementId bottom_left = quader_poly::add_vertex(document, { -2.0F, 0.0F, -2.0F });
	const quader_poly::ElementId top_left = quader_poly::add_vertex(document, { -2.0F, 2.0F, 2.0F });
	const quader_poly::ElementId top_right = quader_poly::add_vertex(document, { 2.0F, 2.0F, 2.0F });
	const quader_poly::ElementId bottom_right = quader_poly::add_vertex(document, { 2.0F, 0.0F, -2.0F });
	const quader_poly::ElementId face_id =
		quader_poly::add_face(document, std::array<quader_poly::ElementId, 4> {
			bottom_left,
			top_left,
			top_right,
			bottom_right,
		});
        EXPECT_TRUE(face_id != quader_poly::kInvalidElementId);

        quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = document;
	quader_editor_core::EditorSession session({ object });

	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
        camera.transform.origin = { 0.25F, 5.0F, -1.25F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 0.0F, 1.0F };
	camera.transform.z_axis = { 0.0F, 1.0F, 0.0F };
	camera.viewport_size = { 1000.0F, 1000.0F };

	const quader_poly::Ray ray {
		camera.transform.origin,
		{ 0.0F, -1.0F, 0.0F },
	};
	EXPECT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		ray,
		camera,
		1.0,
		true,
		false));

	EXPECT_TRUE(state.has_current);
        EXPECT_TRUE(state.current_target.kind ==
                    quader_poly::KnifePointTargetKind::FacePoint);
        EXPECT_TRUE(state.current_target.face_id == face_id);

	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	EXPECT_TRUE(face != nullptr);
	const quader::QVec3 normal = quader_poly::face_normal(document, *face);
	const quader_poly::Vertex *origin = quader_poly::find_vertex(document, bottom_left);
	EXPECT_TRUE(origin != nullptr);
	const float signed_distance = quader::dot(state.current_point - origin->position, normal);
	EXPECT_TRUE(std::abs(signed_distance) <= 0.0001F);
	EXPECT_FALSE(qdr_test::nearly_equal(state.current_point.y, 2.0F, 0.0001));
	EXPECT_NEAR(state.current_target.position.y, state.current_point.y, 0.0001);
}
