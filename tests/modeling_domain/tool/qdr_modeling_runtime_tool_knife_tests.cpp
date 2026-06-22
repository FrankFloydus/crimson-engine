////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>

#include <modeling/runtime/editor_runtime.h>
#include <modeling/interactions/knife_tool.h>
#include <modeling/interactions/tool_snapping.h>
#include <modeling/runtime/tool_registry.h>
#include <modeling/interactions/interaction_api.h>
#include <modeling/polygon/document.h>

TEST_CASE("knife tool exposes stable registry contract and reset ownership", "[editor-core][tool][knife]")
{
	const quader_editor_core::EditorTool& tool = quader_editor_core::knife_tool();
	CHECK(tool.tool_id() == quader_editor_core::ViewportInteractionTool::knife);
	CHECK(std::string(tool.id()) == "knife");
	CHECK(std::string(tool.label()) == "Knife");
	CHECK(quader_editor_core::editor_tool_for_kind(quader_editor_core::ViewportInteractionTool::knife) == &tool);
	CHECK(quader_editor_core::editor_tool_for_id("knife") == &tool);

	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::knife;
	runtime.knife.active = true;
	runtime.knife.has_current = true;
	runtime.knife.preview_points.push_back({});

	tool.reset_runtime_state(runtime, true);
	CHECK(runtime.knife.active);
	CHECK_FALSE(runtime.knife.has_current);
	CHECK(runtime.knife.preview_points.empty());

	tool.reset_runtime_state(runtime, false);
	CHECK_FALSE(runtime.knife.active);
}

TEST_CASE("knife preview reports stroke overlay state", "[editor-core][tool][knife]")
{
	quader_editor_core::KnifeToolState state;
	state.active = true;
	state.has_active_stroke = true;
	state.has_current = true;
	state.current_target_valid = true;
	state.target_object_id = 7;
	state.previous_point_index = 1;
	state.points = {
		{ quader_poly::KnifePointTargetKind::face_point },
		{ quader_poly::KnifePointTargetKind::face_point },
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

	CHECK(preview.active);
	CHECK(preview.has_active_stroke);
	CHECK(preview.has_current);
	CHECK(preview.target_object_id == 7);
	CHECK(preview.previous_point.x == Catch::Approx(1.0F));
	CHECK(preview.current_point.z == Catch::Approx(1.0F));
	CHECK(preview.point_count == 2U);
	CHECK(preview.segment_count == 1U);
	CHECK(preview.preview_point_count == 4U);
	CHECK(preview.preview_line_count == 2U);
	CHECK(preview.can_commit);
}

TEST_CASE("knife snapping and accept flow stay in editor-core", "[editor-core][tool][knife]")
{
	const quader_poly::Vec3 snapped =
		quader_editor_core::snap_knife_point_to_grid({ 0.49F, 0.26F, -0.49F }, 0.5, true);
	CHECK(snapped.x == Catch::Approx(0.5F));
	CHECK(snapped.y == Catch::Approx(0.5F));
	CHECK(snapped.z == Catch::Approx(-0.5F));

	const quader_poly::Vec3 raw =
		quader_editor_core::snap_knife_point_to_grid({ 0.49F, 0.26F, -0.49F }, 0.5, false);
	CHECK(raw.x == Catch::Approx(0.49F));
	CHECK(raw.y == Catch::Approx(0.26F));
	CHECK(raw.z == Catch::Approx(-0.49F));

	const quader_poly::Vec3 constrained =
		quader_editor_core::constrain_knife_point_to_axis(
			{ 0.0F, 0.0F, 0.0F },
			{ 1.2F, 0.0F, 0.4F },
			{ 0.0F, 1.0F, 0.0F },
			0.5,
			true);
	CHECK(constrained.x == Catch::Approx(1.0F));
	CHECK(constrained.y == Catch::Approx(0.0F));
	CHECK(constrained.z == Catch::Approx(0.0F));

	quader_editor_core::EditorSession session;
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	session.set_objects({ object });

	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;

	REQUIRE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		{ { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } },
		camera,
		0.5,
		true,
		false));
	CHECK(state.active);
	CHECK(state.has_current);
	CHECK(state.current_target_valid);
	CHECK(state.target_object_id == 7);
	CHECK(state.status == quader_editor_core::KnifeToolStatus::preview);
	CHECK(state.current_point.y == Catch::Approx(1.0F));

	REQUIRE(quader_editor_core::accept_knife_current_point(session, state, camera, 0.5));
	CHECK(state.has_active_stroke);
	REQUIRE(state.points.size() == 1U);
	CHECK_FALSE(state.has_current);
	CHECK(state.status == quader_editor_core::KnifeToolStatus::previous_set);

	REQUIRE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		{ { 0.5F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } },
		camera,
		0.5,
		true,
		false));
	REQUIRE(quader_editor_core::accept_knife_current_point(session, state, camera, 0.5));
	CHECK(state.has_active_stroke);
	REQUIRE(state.points.size() == 2U);
	REQUIRE(state.segments.size() == 1U);
	CHECK(state.segments[0].first_point == 0U);
	CHECK(state.segments[0].second_point == 1U);
	CHECK_FALSE(state.preview_lines.empty());
}

TEST_CASE("knife snapped face points stay projected onto tilted mesh surfaces", "[editor-core][tool][knife]")
{
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
	REQUIRE(face_id != quader_poly::invalid_element_id);

	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = document;
	quader_editor_core::EditorSession session({ object });

	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	camera.transform.origin = { 0.25F, 5.0F, -1.25F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 0.0F, 1.0F };
	camera.transform.z_axis = { 0.0F, 1.0F, 0.0F };
	camera.viewport_size = { 1000.0F, 1000.0F };

	const quader_poly::Ray ray {
		camera.transform.origin,
		{ 0.0F, -1.0F, 0.0F },
	};
	REQUIRE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		ray,
		camera,
		1.0,
		true,
		false));

	REQUIRE(state.has_current);
	REQUIRE(state.current_target.kind == quader_poly::KnifePointTargetKind::face_point);
	CHECK(state.current_target.face_id == face_id);

	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	REQUIRE(face != nullptr);
	const quader_poly::Vec3 normal = quader_poly::face_normal(document, *face);
	const quader_poly::Vertex *origin = quader_poly::find_vertex(document, bottom_left);
	REQUIRE(origin != nullptr);
	const float signed_distance = quader_poly::dot(state.current_point - origin->position, normal);
	CHECK(std::abs(signed_distance) <= 0.0001F);
	CHECK(state.current_point.y != Catch::Approx(2.0F));
	CHECK(state.current_target.position.y == Catch::Approx(state.current_point.y));
}
