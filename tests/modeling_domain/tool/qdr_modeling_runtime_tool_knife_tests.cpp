////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/knife_tool.hpp>
#include <editor/interactions/tool_snapping.hpp>
#include <editor/modeling/runtime/tool_registry.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_picking_helpers.hpp>

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
	EXPECT_TRUE(preview.preview_point_count == 3U);
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

	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	quader_editor_core::EditorSession session({ std::move(object) });

	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;

        EXPECT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		{ { 0.0F, 2.0F, 1.0F }, { 0.0F, -1.0F, 0.0F } },
		0.5,
		true,
		false));
	EXPECT_TRUE(state.active);
	EXPECT_TRUE(state.has_current);
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.current_object_id == 7);
	EXPECT_TRUE(state.target_object_id < 0);
        EXPECT_TRUE(state.status ==
                    quader_editor_core::KnifeToolStatus::Preview);
        EXPECT_NEAR(state.current_point.y, 1.0F, 0.0001);

	EXPECT_TRUE(quader_editor_core::accept_knife_current_point(state, 0.5));
	EXPECT_TRUE(state.has_active_stroke);
	EXPECT_TRUE(state.target_object_id == 7);
	EXPECT_TRUE(state.points.size() == 1U);
	EXPECT_FALSE(state.has_current);
        EXPECT_TRUE(state.status ==
                    quader_editor_core::KnifeToolStatus::PreviousSet);

        EXPECT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		{ { 0.5F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } },
		0.5,
		true,
		false));
	EXPECT_TRUE(quader_editor_core::accept_knife_current_point(state, 0.5));
	EXPECT_TRUE(state.has_active_stroke);
	EXPECT_TRUE(state.points.size() == 2U);
	EXPECT_TRUE(state.segments.size() == 1U);
	EXPECT_TRUE(state.segments[0].first_point == 0U);
	EXPECT_TRUE(state.segments[0].second_point == 1U);
	EXPECT_FALSE(state.preview_lines.empty());
	EXPECT_FALSE(quader_editor_core::knife_tool_preview(state).current_closes_stroke);
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session, state, hover,
		{{0.5F, 2.0F, 0.5F}, {0.0F, -1.0F, 0.0F}},
		0.5, true, false));
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(state, 0.5));
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session, state, hover,
		{{0.0F, 2.0F, 1.0F}, {0.0F, -1.0F, 0.0F}},
		0.5, true, false));
	EXPECT_TRUE(quader_editor_core::knife_tool_preview(state).current_closes_stroke);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(state, 0.5));
	EXPECT_FALSE(state.has_active_stroke);
	EXPECT_FALSE(quader_editor_core::knife_tool_preview(state).current_closes_stroke);
}

TEST(QdrModelingRuntimeToolKnifeTests,
	KnifeEdgeGridSnapQuantizesTopologyDistanceOnSlantedEdges) {
	quader_poly::Document document;
	const quader_poly::ElementId first =
		quader_poly::add_vertex(document, { 0.3F, -0.4F, 1.1F });
	const quader_poly::ElementId second =
		quader_poly::add_vertex(document, { 3.3F, 3.6F, 1.1F });
	const quader_poly::Edge edge = quader_poly::make_edge(first, second);
	const quader::QVec3 raw_point = { 1.14F, 0.72F, 1.1F };

	const std::optional<quader::QVec3> snapped =
		quader_editor_core::snap_knife_point_to_edge_grid(
			document, edge, raw_point, 1.0, true);

	ASSERT_TRUE(snapped.has_value());
	EXPECT_NEAR(snapped->x, 0.9F, 0.0001F);
	EXPECT_NEAR(snapped->y, 0.4F, 0.0001F);
	EXPECT_NEAR(snapped->z, 1.1F, 0.0001F);
	const quader_poly::Vertex *origin =
		quader_poly::find_vertex(document, edge.a);
	ASSERT_TRUE(origin != nullptr);
	EXPECT_NEAR(quader::length(*snapped - origin->position), 1.0F, 0.0001F);
	EXPECT_TRUE(quader::length_squared(
		quader::cross(*snapped - origin->position,
			raw_point - origin->position)) <= 0.0001F);
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
	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	ASSERT_TRUE(face != nullptr);
	quader_editor_core::PickHitRecord seed_hit;
	seed_hit.hit = true;
	seed_hit.object_id = object.id;
	seed_hit.kind = quader_poly::ElementKind::Edge;
	seed_hit.edge = quader_poly::make_edge(bottom_left, bottom_right);
	seed_hit.face_id = face_id;
	seed_hit.local_position = { 0.0F, 0.0F, -2.0F };
	seed_hit.local_normal = quader_poly::face_normal(document, *face);
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, seed_hit, 1.0, false, false));
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(state, 1.0));

	const quader_poly::Ray ray {
		{ 0.25F, 5.0F, -1.25F },
		{ 0.0F, -1.0F, 0.0F },
	};
	EXPECT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session,
		state,
		hover,
		ray,
		1.0,
		true,
		false));

	EXPECT_TRUE(state.has_current);
        EXPECT_TRUE(state.current_target.kind ==
                    quader_poly::KnifePointTargetKind::FacePoint);
        EXPECT_TRUE(state.current_target.face_id == face_id);

	const quader::QVec3 normal = quader_poly::face_normal(document, *face);
	const quader_poly::Vertex *origin = quader_poly::find_vertex(document, bottom_left);
	EXPECT_TRUE(origin != nullptr);
	const float signed_distance = quader::dot(state.current_point - origin->position, normal);
	EXPECT_TRUE(std::abs(signed_distance) <= 0.0001F);
	EXPECT_FALSE(qdr_test::nearly_equal(state.current_point.y, 2.0F, 0.0001));
	EXPECT_NEAR(state.current_target.position.y, state.current_point.y, 0.0001);
}

TEST(QdrModelingRuntimeToolKnifeTests, EdgeSeedRestrictsKnifeTargetsToItsTwoIncidentFaces) {
	quader_poly::Document document;
	const quader_poly::ElementId seed_a =
		quader_poly::add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const quader_poly::ElementId seed_b =
		quader_poly::add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const quader_poly::ElementId first_far_b =
		quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId first_far_a =
		quader_poly::add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const quader_poly::ElementId second_far_a =
		quader_poly::add_vertex(document, { 0.0F, 0.0F, 1.0F });
	const quader_poly::ElementId second_far_b =
		quader_poly::add_vertex(document, { 1.0F, 0.0F, 1.0F });
	const quader_poly::ElementId outside_far_b =
		quader_poly::add_vertex(document, { 1.0F, 2.0F, 0.0F });
	const quader_poly::ElementId outside_far_a =
		quader_poly::add_vertex(document, { 0.0F, 2.0F, 0.0F });

	const quader_poly::ElementId first_face = quader_poly::add_face(
		document,
		std::array<quader_poly::ElementId, 4> {
			seed_a, seed_b, first_far_b, first_far_a,
		});
	const quader_poly::ElementId second_face = quader_poly::add_face(
		document,
		std::array<quader_poly::ElementId, 4> {
			seed_b, seed_a, second_far_a, second_far_b,
		});
	const quader_poly::ElementId outside_face = quader_poly::add_face(
		document,
		std::array<quader_poly::ElementId, 4> {
			first_far_a, first_far_b, outside_far_b, outside_far_a,
		});
	ASSERT_TRUE(first_face != quader_poly::kInvalidElementId);
	ASSERT_TRUE(second_face != quader_poly::kInvalidElementId);
	ASSERT_TRUE(outside_face != quader_poly::kInvalidElementId);

	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = document;
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;

	quader_editor_core::PickHitRecord seed_hit;
	seed_hit.hit = true;
	seed_hit.object_id = object.id;
	seed_hit.kind = quader_poly::ElementKind::Edge;
	seed_hit.edge = quader_poly::make_edge(seed_a, seed_b);
	seed_hit.face_id = first_face;
	seed_hit.local_position = { 0.5F, 0.0F, 0.0F };
	seed_hit.local_normal = { 0.0F, 0.0F, 1.0F };
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, seed_hit, 1.0, false, false));
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		state, 1.0));
	ASSERT_TRUE(state.has_active_stroke);
	ASSERT_TRUE(state.points.size() == 1U);
	EXPECT_TRUE(state.points.front().face_id == first_face);

	quader_editor_core::PickHitRecord face_hit;
	face_hit.hit = true;
	face_hit.object_id = object.id;
	face_hit.kind = quader_poly::ElementKind::Face;
	face_hit.face_id = first_face;
	face_hit.local_position = { 0.5F, 0.5F, 0.0F };
	face_hit.local_normal = { 0.0F, 0.0F, 1.0F };
	EXPECT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, face_hit, 1.0, false, false));
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.current_target.face_id == first_face);

	face_hit.face_id = second_face;
	face_hit.local_position = { 0.5F, 0.0F, 0.5F };
	face_hit.local_normal = { 0.0F, 1.0F, 0.0F };
	EXPECT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, face_hit, 1.0, false, false));
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.current_target.face_id == second_face);

	quader_editor_core::PickHitRecord boundary_hit;
	boundary_hit.hit = true;
	boundary_hit.object_id = object.id;
	boundary_hit.kind = quader_poly::ElementKind::Edge;
	boundary_hit.edge = quader_poly::make_edge(first_far_a, first_far_b);
	boundary_hit.face_id = outside_face;
	boundary_hit.local_position = { 0.5F, 1.0F, 0.0F };
	boundary_hit.local_normal = { 0.0F, 0.0F, 1.0F };
	EXPECT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, boundary_hit, 1.0, false, false));
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.current_target.face_id == first_face);
	EXPECT_TRUE(hover.face_id == first_face);

	quader_editor_core::PickHitRecord outside_hit = boundary_hit;
	outside_hit.edge = quader_poly::make_edge(outside_far_a, outside_far_b);
	outside_hit.local_position = { 0.5F, 2.0F, 0.0F };
	EXPECT_FALSE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, outside_hit, 1.0, false, false));
	EXPECT_FALSE(state.current_target_valid);
	EXPECT_TRUE(state.status ==
		quader_editor_core::KnifeToolStatus::InvalidTarget);
	EXPECT_TRUE(hover.kind == quader_editor_core::SelectionHoverKind::None);
	EXPECT_TRUE(hover.object_id < 0);
}

namespace {

/**
 * Stores the three-face topology fixture used by Knife locality regressions.
 */
struct KnifeIncidentFaceMesh {
	quader_poly::Document document;
	quader_poly::ElementId seed_a = quader_poly::kInvalidElementId;
	quader_poly::ElementId seed_b = quader_poly::kInvalidElementId;
	quader_poly::ElementId first_far_a = quader_poly::kInvalidElementId;
	quader_poly::ElementId first_far_b = quader_poly::kInvalidElementId;
	quader_poly::ElementId second_far_a = quader_poly::kInvalidElementId;
	quader_poly::ElementId second_far_b = quader_poly::kInvalidElementId;
	quader_poly::ElementId outside_far_a = quader_poly::kInvalidElementId;
	quader_poly::ElementId outside_far_b = quader_poly::kInvalidElementId;
	quader_poly::ElementId first_face = quader_poly::kInvalidElementId;
	quader_poly::ElementId second_face = quader_poly::kInvalidElementId;
	quader_poly::ElementId outside_face = quader_poly::kInvalidElementId;
};

/**
 * Stores a four-face fan whose center vertex exercises vertex-seeded Knife
 * locality independently of one pick-attributed face.
 */
struct KnifeVertexFanMesh {
	quader_poly::Document document;
	quader_poly::ElementId center = quader_poly::kInvalidElementId;
	quader_poly::ElementId east = quader_poly::kInvalidElementId;
	quader_poly::ElementId north = quader_poly::kInvalidElementId;
	quader_poly::ElementId west = quader_poly::kInvalidElementId;
	quader_poly::ElementId south = quader_poly::kInvalidElementId;
	std::array<quader_poly::ElementId, 4U> faces {};
};

KnifeIncidentFaceMesh make_knife_incident_face_mesh()
{
	KnifeIncidentFaceMesh mesh;
	mesh.seed_a = quader_poly::add_vertex(mesh.document, { 0.0F, 0.0F, 0.0F });
	mesh.seed_b = quader_poly::add_vertex(mesh.document, { 1.0F, 0.0F, 0.0F });
	mesh.first_far_b = quader_poly::add_vertex(mesh.document, { 1.0F, 1.0F, 0.0F });
	mesh.first_far_a = quader_poly::add_vertex(mesh.document, { 0.0F, 1.0F, 0.0F });
	mesh.second_far_a = quader_poly::add_vertex(mesh.document, { 0.0F, 0.0F, 1.0F });
	mesh.second_far_b = quader_poly::add_vertex(mesh.document, { 1.0F, 0.0F, 1.0F });
	mesh.outside_far_b = quader_poly::add_vertex(mesh.document, { 1.0F, 2.0F, 0.0F });
	mesh.outside_far_a = quader_poly::add_vertex(mesh.document, { 0.0F, 2.0F, 0.0F });
	mesh.first_face = quader_poly::add_face(
		mesh.document,
		std::array<quader_poly::ElementId, 4> {
			mesh.seed_a, mesh.seed_b, mesh.first_far_b, mesh.first_far_a,
		});
	mesh.second_face = quader_poly::add_face(
		mesh.document,
		std::array<quader_poly::ElementId, 4> {
			mesh.seed_b, mesh.seed_a, mesh.second_far_a, mesh.second_far_b,
		});
	mesh.outside_face = quader_poly::add_face(
		mesh.document,
		std::array<quader_poly::ElementId, 4> {
			mesh.first_far_a,
			mesh.first_far_b,
			mesh.outside_far_b,
			mesh.outside_far_a,
		});
	return mesh;
}

KnifeVertexFanMesh make_knife_vertex_fan_mesh()
{
	KnifeVertexFanMesh mesh;
	mesh.center = quader_poly::add_vertex(mesh.document, { 0.0F, 0.0F, 0.0F });
	mesh.east = quader_poly::add_vertex(mesh.document, { 1.0F, 0.0F, 0.0F });
	mesh.north = quader_poly::add_vertex(mesh.document, { 0.0F, 1.0F, 0.0F });
	mesh.west = quader_poly::add_vertex(mesh.document, { -1.0F, 0.0F, 0.0F });
	mesh.south = quader_poly::add_vertex(mesh.document, { 0.0F, -1.0F, 0.0F });
	mesh.faces = {
		quader_poly::add_face(mesh.document,
			std::array { mesh.center, mesh.east, mesh.north }),
		quader_poly::add_face(mesh.document,
			std::array { mesh.center, mesh.north, mesh.west }),
		quader_poly::add_face(mesh.document,
			std::array { mesh.center, mesh.west, mesh.south }),
		quader_poly::add_face(mesh.document,
			std::array { mesh.center, mesh.south, mesh.east }),
	};
	return mesh;
}

quader_editor_core::PickHitRecord make_knife_edge_hit(
		int object_id,
		quader_poly::Edge edge,
		quader_poly::ElementId face_id,
		quader::QVec3 position)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
	hit.kind = quader_poly::ElementKind::Edge;
	hit.edge = quader_poly::make_edge(edge.a, edge.b);
	hit.face_id = face_id;
	hit.local_position = position;
	hit.local_normal = { 0.0F, 0.0F, 1.0F };
	return hit;
}

quader_editor_core::PickHitRecord make_knife_face_hit(
		int object_id,
		quader_poly::ElementId face_id,
		quader::QVec3 position)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
	hit.kind = quader_poly::ElementKind::Face;
	hit.face_id = face_id;
	hit.local_position = position;
	hit.local_normal = { 0.0F, 0.0F, 1.0F };
	return hit;
}

quader_editor_core::PickHitRecord make_knife_vertex_hit(
		int object_id,
		quader_poly::ElementId vertex_id,
		quader_poly::ElementId face_id,
		quader::QVec3 position)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
	hit.kind = quader_poly::ElementKind::Vertex;
	hit.vertex_id = vertex_id;
	hit.face_id = face_id;
	hit.local_position = position;
	hit.local_normal = { 0.0F, 0.0F, 1.0F };
	return hit;
}

bool knife_points_nearly_equal(quader::QVec3 left, quader::QVec3 right)
{
	return qdr_test::nearly_equal(left.x, right.x) &&
		qdr_test::nearly_equal(left.y, right.y) &&
		qdr_test::nearly_equal(left.z, right.z);
}

bool seed_knife_from_shared_edge(
		quader_editor_core::EditorSession& session,
		quader_editor_core::KnifeToolState& state,
		quader_editor_core::SelectionHoverState& hover,
		const KnifeIncidentFaceMesh& mesh,
		int object_id)
{
	const quader_editor_core::PickHitRecord seed_hit = make_knife_edge_hit(
		object_id,
		quader_poly::make_edge(mesh.seed_a, mesh.seed_b),
		mesh.first_face,
		{ 0.5F, 0.0F, 0.0F });
	return quader_editor_core::update_knife_preview_from_hit(
			session, state, hover, seed_hit, 0.25, false, false) &&
		quader_editor_core::accept_knife_current_point(
			state, 0.25) &&
			state.has_active_stroke && state.points.size() == 1U &&
			state.segments.empty();
}

quader_poly::Document make_knife_pick_grid(std::size_t side_count)
{
	quader_poly::Document document;
	std::vector<quader_poly::ElementId> vertices(
		(side_count + 1U) * (side_count + 1U));
	for (std::size_t y = 0U; y <= side_count; ++y) {
		for (std::size_t x = 0U; x <= side_count; ++x) {
			vertices[y * (side_count + 1U) + x] = quader_poly::add_vertex(
				document,
				{
					static_cast<float>(x),
					static_cast<float>(y),
					0.0F,
				});
		}
	}
	for (std::size_t y = 0U; y < side_count; ++y) {
		for (std::size_t x = 0U; x < side_count; ++x) {
			const std::size_t row = y * (side_count + 1U);
			const std::size_t next_row = (y + 1U) * (side_count + 1U);
			(void)quader_poly::add_face(
				document,
				std::array {
					vertices[row + x],
					vertices[row + x + 1U],
					vertices[next_row + x + 1U],
					vertices[next_row + x],
				});
		}
	}
	return document;
}

} // namespace

TEST(QdrModelingRuntimeToolKnifeTests,
	OrderedPickPreparesEachMeshOnceAndScalesWithTopology)
{
	const auto measure = [](std::size_t side_count) {
		quader_editor_core::PolygonObject object;
		object.id = 7;
		object.document = make_knife_pick_grid(side_count);
		quader_editor_core::EditorSession session({std::move(object)});
		quader_poly::PickOptions options;
		options.mode = quader_poly::SelectionMode::Vertex;
		options.pick_backfaces = true;
		options.prefer_front_facing_faces = true;
		constexpr std::array kPriorityModes {
			quader_editor_core::SelectionMode::Vertex,
			quader_editor_core::SelectionMode::Edge,
			quader_editor_core::SelectionMode::Face,
		};
		quader_poly::document_internal::PickWorkCounters work;
		const std::optional<quader_editor_core::PickHitRecord> hit =
			quader_editor_core::pick_nearest_priority(
				session.objects(),
				{
					{{0.5F, 0.5F, 2.0F}, {0.0F, 0.0F, -1.0F}},
					options,
					-1,
				},
				kPriorityModes,
				{},
				{},
				{},
				&work);
		EXPECT_TRUE(hit.has_value());
		if (hit.has_value()) {
			EXPECT_TRUE(hit->kind == quader_poly::ElementKind::Face);
		}
		const std::size_t face_count = side_count * side_count;
		EXPECT_EQ(work.fact_sets_built, 1U);
		EXPECT_EQ(work.face_facts_built, face_count);
		EXPECT_EQ(work.face_triangle_facts_built, face_count * 2U);
		EXPECT_EQ(work.surface_triangle_tests, face_count * 2U);
		EXPECT_EQ(
			work.component_candidates_tested,
			(side_count + 1U) * (side_count + 1U) +
				2U * side_count * (side_count + 1U));
		return work;
	};

	const quader_poly::document_internal::PickWorkCounters small =
		measure(16U);
	const quader_poly::document_internal::PickWorkCounters large =
		measure(32U);
	EXPECT_LE(large.total(), small.total() * 4U);
}

TEST(QdrModelingRuntimeToolKnifeTests,
	HoverDoesNotLockTargetObjectUntilTheFirstPointIsAccepted) {
	const KnifeIncidentFaceMesh mesh = make_knife_incident_face_mesh();
	quader_editor_core::PolygonObject first;
	first.id = 7;
	first.document = mesh.document;
	quader_editor_core::PolygonObject second = first;
	second.id = 9;
	quader_editor_core::EditorSession session({first, second});
	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	const quader_poly::Edge seed_edge =
		quader_poly::make_edge(mesh.seed_a, mesh.seed_b);

	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover,
		make_knife_edge_hit(first.id, seed_edge, mesh.first_face,
			{0.25F, 0.0F, 0.0F}),
		0.25, false, false));
	EXPECT_EQ(state.current_object_id, first.id);
	EXPECT_LT(state.target_object_id, 0);

	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover,
		make_knife_edge_hit(second.id, seed_edge, mesh.first_face,
			{0.75F, 0.0F, 0.0F}),
		0.25, false, false));
	EXPECT_EQ(state.current_object_id, second.id);
	EXPECT_LT(state.target_object_id, 0);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(state, 0.25));
	EXPECT_EQ(state.target_object_id, second.id);
	EXPECT_LT(state.current_object_id, 0);

	EXPECT_FALSE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover,
		make_knife_edge_hit(first.id, seed_edge, mesh.first_face,
			{0.5F, 0.0F, 0.0F}),
		0.25, false, false));
	EXPECT_EQ(state.target_object_id, second.id);
	EXPECT_FALSE(state.has_current);
}

TEST(QdrModelingRuntimeToolKnifeTests, SameIncidentEdgeEndpointIsStableAcrossVisibleAndBehindFaceAttribution) {
	const KnifeIncidentFaceMesh mesh = make_knife_incident_face_mesh();
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = mesh.document;
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	ASSERT_TRUE(seed_knife_from_shared_edge(
		session, state, hover, mesh, object.id));

	const quader_poly::Edge boundary_edge =
		quader_poly::make_edge(mesh.first_far_a, mesh.first_far_b);
	const quader_editor_core::PickHitRecord visible_hit = make_knife_edge_hit(
		object.id, boundary_edge, mesh.first_face, { 0.25F, 1.0F, 0.0F });
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, visible_hit, 0.25, false, false));
	ASSERT_TRUE(state.current_target_valid);
	ASSERT_TRUE(state.preview_lines.size() == 1U);
	const quader_poly::KnifePointTarget visible_target = state.current_target;
	const quader::QVec3 visible_point = state.current_point;
	const quader_editor_core::SelectionOverlayLine visible_line = state.preview_lines.front();

	quader_editor_core::PickHitRecord behind_hit = visible_hit;
	behind_hit.face_id = mesh.outside_face;
	behind_hit.local_position = { 0.25F, 1.0F, 3.0F };
	behind_hit.local_normal = { 0.0F, 0.0F, -1.0F };
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, behind_hit, 0.25, true, false));

	EXPECT_TRUE(state.has_current);
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.current_target.kind == visible_target.kind);
	EXPECT_TRUE(state.current_target.edge == visible_target.edge);
	EXPECT_TRUE(state.current_target.face_id == visible_target.face_id);
	EXPECT_NEAR(state.current_target.edge_factor, visible_target.edge_factor, 0.0001);
	EXPECT_TRUE(knife_points_nearly_equal(state.current_point, visible_point));
	ASSERT_TRUE(state.preview_lines.size() == 1U);
	EXPECT_TRUE(knife_points_nearly_equal(
		state.preview_lines.front().start, visible_line.start));
	EXPECT_TRUE(knife_points_nearly_equal(
		state.preview_lines.front().end, visible_line.end));
	EXPECT_TRUE(state.preview_lines.front().color_abgr == visible_line.color_abgr);
	EXPECT_TRUE(hover.kind == quader_editor_core::SelectionHoverKind::Edge);
	EXPECT_TRUE(hover.edge == boundary_edge);
	EXPECT_TRUE(hover.face_id == mesh.first_face);
}

TEST(QdrModelingRuntimeToolKnifeTests, OutsideIncidentFaceHitClearsKnifeHoverWithoutInvalidPreviewLine) {
	const KnifeIncidentFaceMesh mesh = make_knife_incident_face_mesh();
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = mesh.document;
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	ASSERT_TRUE(seed_knife_from_shared_edge(
		session, state, hover, mesh, object.id));

	const quader_editor_core::PickHitRecord outside_hit = make_knife_edge_hit(
		object.id,
		quader_poly::make_edge(mesh.outside_far_a, mesh.outside_far_b),
		mesh.outside_face,
		{ 0.5F, 2.0F, 0.0F });
	EXPECT_FALSE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, outside_hit, 0.25, false, false));

	EXPECT_FALSE(state.has_current);
	EXPECT_FALSE(state.current_target_valid);
	EXPECT_TRUE(state.preview_lines.empty());
	const quader_editor_core::KnifeToolPreview preview =
		quader_editor_core::knife_tool_preview(state);
	EXPECT_FALSE(preview.has_current);
	EXPECT_TRUE(preview.preview_line_count == 0U);
	EXPECT_TRUE(hover.kind == quader_editor_core::SelectionHoverKind::None);
	EXPECT_TRUE(hover.object_id < 0);
}

TEST(QdrModelingRuntimeToolKnifeTests, AcceptedEdgePointAdvancesKnifeRestrictionToItsIncidentFaces) {
	const KnifeIncidentFaceMesh mesh = make_knife_incident_face_mesh();
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = mesh.document;
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;
	ASSERT_TRUE(seed_knife_from_shared_edge(
		session, state, hover, mesh, object.id));

	const quader_editor_core::PickHitRecord outside_face_hit = make_knife_face_hit(
		object.id, mesh.outside_face, { 0.5F, 1.5F, 0.0F });
	EXPECT_FALSE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, outside_face_hit, 0.25, false, false));
	EXPECT_FALSE(quader_editor_core::accept_knife_current_point(
		state, 0.25));
	EXPECT_TRUE(state.points.size() == 1U);
	EXPECT_TRUE(state.segments.empty());
	EXPECT_TRUE(state.preview_lines.empty());

	const quader_editor_core::PickHitRecord boundary_hit = make_knife_edge_hit(
		object.id,
		quader_poly::make_edge(mesh.first_far_a, mesh.first_far_b),
		mesh.outside_face,
		{ 0.5F, 1.0F, 0.0F });
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, boundary_hit, 0.25, false, false));
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		state, 0.25));
	ASSERT_TRUE(state.points.size() == 2U);
	ASSERT_TRUE(state.segments.size() == 1U);
	EXPECT_TRUE(state.points.back().edge == boundary_hit.edge);

	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, outside_face_hit, 0.25, false, false));
	EXPECT_TRUE(state.current_target_valid);
	EXPECT_TRUE(state.current_target.face_id == mesh.outside_face);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		state, 0.25));
	EXPECT_TRUE(state.points.size() == 3U);
	ASSERT_TRUE(state.segments.size() == 2U);
	EXPECT_TRUE(state.segments[0].first_point == 0U);
	EXPECT_TRUE(state.segments[0].second_point == 1U);
	EXPECT_TRUE(state.segments[1].first_point == 1U);
	EXPECT_TRUE(state.segments[1].second_point == 2U);
}

TEST(QdrModelingRuntimeToolKnifeTests,
	HighValenceVertexSeedCanContinueOntoAnyIncidentFaceOrEdge) {
	const KnifeVertexFanMesh mesh = make_knife_vertex_fan_mesh();
	ASSERT_TRUE(std::ranges::all_of(mesh.faces, [](quader_poly::ElementId face_id) {
		return face_id != quader_poly::kInvalidElementId;
	}));
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = mesh.document;
	quader_editor_core::EditorSession session({ object });
	const quader_editor_core::PickHitRecord seed_hit = make_knife_vertex_hit(
		object.id, mesh.center, quader_poly::kInvalidElementId,
		{ 0.0F, 0.0F, 0.0F });

	quader_editor_core::KnifeToolState face_state;
	quader_editor_core::SelectionHoverState face_hover;
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, face_state, face_hover, seed_hit, 0.25, false, false));
	ASSERT_TRUE(face_state.current_target.kind ==
		quader_poly::KnifePointTargetKind::ExistingVertex);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		face_state, 0.25));
	ASSERT_TRUE(face_state.has_active_stroke);
	ASSERT_TRUE(face_state.points.size() == 1U);
	EXPECT_TRUE(face_state.points.front().vertex_id == mesh.center);

	const quader_editor_core::PickHitRecord incident_face_hit =
		make_knife_face_hit(object.id, mesh.faces[2],
			{ -0.25F, -0.25F, 0.0F });
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, face_state, face_hover, incident_face_hit,
		0.25, false, false));
	EXPECT_TRUE(face_state.current_target.kind ==
		quader_poly::KnifePointTargetKind::FacePoint);
	EXPECT_TRUE(face_state.current_target.face_id == mesh.faces[2]);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		face_state, 0.25));
	ASSERT_TRUE(face_state.segments.size() == 1U);
	EXPECT_TRUE(face_state.segments.front().first_point == 0U);
	EXPECT_TRUE(face_state.segments.front().second_point == 1U);

	quader_editor_core::KnifeToolState edge_state;
	quader_editor_core::SelectionHoverState edge_hover;
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, edge_state, edge_hover, seed_hit, 0.25, false, false));
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		edge_state, 0.25));
	const quader_editor_core::PickHitRecord incident_edge_hit =
		make_knife_edge_hit(
			object.id, quader_poly::make_edge(mesh.north, mesh.west),
			mesh.faces[1], { -0.5F, 0.5F, 0.0F });
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_hit(
		session, edge_state, edge_hover, incident_edge_hit,
		0.25, false, false));
	EXPECT_TRUE(edge_state.current_target.kind ==
		quader_poly::KnifePointTargetKind::ExistingEdge);
	EXPECT_TRUE(edge_state.current_target.edge == incident_edge_hit.edge);
	EXPECT_TRUE(edge_state.current_target.face_id == mesh.faces[1]);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		edge_state, 0.25));
	ASSERT_TRUE(edge_state.segments.size() == 1U);
}

TEST(QdrModelingRuntimeToolKnifeTests,
	FaceInteriorCannotSeedKnifeStroke) {
	const KnifeVertexFanMesh mesh = make_knife_vertex_fan_mesh();
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = mesh.document;
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	quader_editor_core::SelectionHoverState hover;

	const quader_editor_core::PickHitRecord face_hit = make_knife_face_hit(
		object.id, mesh.faces[0], { 0.25F, 0.25F, 0.0F });
	EXPECT_FALSE(quader_editor_core::update_knife_preview_from_hit(
		session, state, hover, face_hit, 0.25, false, false));
	EXPECT_FALSE(state.has_current);
	EXPECT_FALSE(state.current_target_valid);
	EXPECT_TRUE(state.status ==
		quader_editor_core::KnifeToolStatus::InvalidTarget);
	EXPECT_FALSE(quader_editor_core::accept_knife_current_point(
		state, 0.25));
	EXPECT_FALSE(state.has_active_stroke);
	EXPECT_TRUE(state.points.empty());
	EXPECT_TRUE(state.segments.empty());
}

TEST(QdrModelingRuntimeToolKnifeTests,
	ReversedWindingStillAllowsVisibleVertexAndEdgeSeedsFromOutside) {
	quader_poly::Document document;
	const quader_poly::ElementId bottom_left =
		quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId top_left =
		quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId top_right =
		quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId bottom_right =
		quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId reversed_face = quader_poly::add_face(
		document,
		std::array { bottom_left, top_left, top_right, bottom_right });
	ASSERT_TRUE(reversed_face != quader_poly::kInvalidElementId);
	const quader_poly::Face *face =
		quader_poly::find_face(document, reversed_face);
	ASSERT_TRUE(face != nullptr);
	ASSERT_TRUE(quader_poly::face_normal(document, *face).z < 0.0F);

	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = document;
	quader_editor_core::EditorSession session({ object });

	quader_editor_core::KnifeToolState vertex_state;
	quader_editor_core::SelectionHoverState vertex_hover;
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session, vertex_state, vertex_hover,
		{{ -1.0F, -1.0F, 2.0F }, { 0.0F, 0.0F, -1.0F }},
		0.25, false, false));
	ASSERT_TRUE(vertex_state.current_target.kind ==
		quader_poly::KnifePointTargetKind::ExistingVertex);
	EXPECT_TRUE(vertex_state.current_target.vertex_id == bottom_left);
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		vertex_state, 0.25));
	EXPECT_TRUE(vertex_state.has_active_stroke);

	quader_editor_core::KnifeToolState edge_state;
	quader_editor_core::SelectionHoverState edge_hover;
	ASSERT_TRUE(quader_editor_core::update_knife_preview_from_pick(
		session, edge_state, edge_hover,
		{{ 0.0F, -1.0F, 2.0F }, { 0.0F, 0.0F, -1.0F }},
		0.25, false, false));
	ASSERT_TRUE(edge_state.current_target.kind ==
		quader_poly::KnifePointTargetKind::ExistingEdge);
	EXPECT_TRUE(edge_state.current_target.edge ==
		quader_poly::make_edge(bottom_left, bottom_right));
	ASSERT_TRUE(quader_editor_core::accept_knife_current_point(
		edge_state, 0.25));
	EXPECT_TRUE(edge_state.has_active_stroke);
}
