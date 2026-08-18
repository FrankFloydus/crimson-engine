////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kHiddenSlot = 99U;

quader_editor_core::PolygonObject cube_object(int id, quader::QVec3 offset = {})
{
	quader_editor_core::PolygonObject object;
	object.id = id;
	object.document = quader_poly::make_cube({});
	for (quader_poly::Vertex& vertex : object.document.edit().vertices) {
		vertex.position.x += offset.x;
		vertex.position.y += offset.y;
		vertex.position.z += offset.z;
	}
	return object;
}

quader_editor_core::PolygonObject open_top_cube_object(int id)
{
	quader_editor_core::PolygonObject object = cube_object(id);
	quader_poly::Document& document = object.document;
	document.faces.erase(
		std::remove_if(
			document.faces.begin(),
			document.faces.end(),
			[&document](const quader_poly::Face& face) {
				return quader::dot(
					quader_poly::face_normal(document, face),
					{ 0.0F, 1.0F, 0.0F }) > 0.5F;
			}),
		document.faces.end());
	return object;
}

quader_poly::Ray front_center_ray()
{
	return { { 0.0F, 0.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

quader_poly::Ray miss_ray()
{
	return { { 50.0F, 50.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

bool nearly_equal(float a, float b)
{
	return std::abs(a - b) <= 0.000001F;
}

quader_poly::PickOptions hidden_slot_pick_options(quader_poly::SelectionMode mode)
{
	quader_poly::PickOptions options;
	options.mode = mode;
	options.has_hidden_material_slot = true;
        options.hidden_material_slot = kHiddenSlot;
        return options;
}

quader_editor_core::PolygonObject stacked_face_object()
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	quader_poly::Document& document = object.document;
	const quader_poly::ElementId back0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId back1 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId back2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId back3 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId front0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId front1 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId front2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.2F });
	const quader_poly::ElementId front3 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.2F });
	[[maybe_unused]] const quader_poly::ElementId back_face =
		quader_poly::add_face(document, std::array { back0, back1, back2, back3 }, 0U);
        [[maybe_unused]] const quader_poly::ElementId front_face =
            quader_poly::add_face(document,
                                  std::array{front0, front1, front2, front3},
                                  kHiddenSlot);
        return object;
}

quader_editor_core::PolygonObject adjacent_face_object()
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	quader_poly::Document& document = object.document;
	const quader_poly::ElementId left0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId shared0 = quader_poly::add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const quader_poly::ElementId shared1 = quader_poly::add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const quader_poly::ElementId left1 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId right0 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId right1 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	[[maybe_unused]] const quader_poly::ElementId visible_face =
		quader_poly::add_face(document, std::array { left0, shared0, shared1, left1 }, 0U);
        [[maybe_unused]] const quader_poly::ElementId hidden_face =
            quader_poly::add_face(document,
                                  std::array{shared0, right0, right1, shared1},
                                  kHiddenSlot);
        return object;
}

quader_editor_core::PolygonObject single_quad_object(int id, bool backfacing)
{
	quader_editor_core::PolygonObject object;
	object.id = id;
	quader_poly::Document& document = object.document;
	const quader_poly::ElementId v0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId v1 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId v2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId v3 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	if (backfacing) {
		[[maybe_unused]] const quader_poly::ElementId face =
			quader_poly::add_face(document, std::array { v0, v3, v2, v1 }, 0U);
	} else {
		[[maybe_unused]] const quader_poly::ElementId face =
			quader_poly::add_face(document, std::array { v0, v1, v2, v3 }, 0U);
	}
	object.selected = true;
	return object;
}

quader_editor_core::PickObjectTransform scaled_pick_transform(
		int object_id,
		float x,
		float y,
		float z)
{
	quader_editor_core::PickObjectTransform transform;
	transform.object_id = object_id;
	transform.world_from_object.values[0] = x;
	transform.world_from_object.values[5] = y;
	transform.world_from_object.values[10] = z;
	transform.object_from_world.values[0] = 1.0F / x;
	transform.object_from_world.values[5] = 1.0F / y;
	transform.object_from_world.values[10] = 1.0F / z;
	return transform;
}

quader_editor_core::PolygonObject occluded_back_edge_object()
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	quader_poly::Document& document = object.document;
	const quader_poly::ElementId front0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId front1 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId front2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.2F });
	const quader_poly::ElementId front3 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.2F });
	const quader_poly::ElementId back0 = quader_poly::add_vertex(document, { -0.5F, -0.05F, 0.0F });
	const quader_poly::ElementId back1 = quader_poly::add_vertex(document, { 0.5F, -0.05F, 0.0F });
	const quader_poly::ElementId back2 = quader_poly::add_vertex(document, { 0.5F, 0.05F, 0.0F });
	const quader_poly::ElementId back3 = quader_poly::add_vertex(document, { -0.5F, 0.05F, 0.0F });
	[[maybe_unused]] const quader_poly::ElementId front_face =
		quader_poly::add_face(document, std::array { front0, front1, front2, front3 }, 0U);
	[[maybe_unused]] const quader_poly::ElementId back_face =
		quader_poly::add_face(document, std::array { back0, back1, back2, back3 }, 0U);
	object.selected = true;
	return object;
}

/**
 * Stores backfacing exterior and interior source-wire pick fixture data.
 */
struct BackfacingOccludedInteriorFixture {
	quader_editor_core::PolygonObject object;
        quader_poly::ElementId near_face = quader_poly::kInvalidElementId;
        quader_poly::ElementId interior_face = quader_poly::kInvalidElementId;
        quader_poly::Edge interior_edge;
        quader_poly::ElementId interior_vertex = quader_poly::kInvalidElementId;
        quader_poly::Ray outside_ray;
	quader_poly::Ray inside_ray;
};

BackfacingOccludedInteriorFixture backfacing_occluded_interior_fixture()
{
	BackfacingOccludedInteriorFixture fixture;
	fixture.object.id = 7;
	quader_poly::Document& document = fixture.object.document;

	const quader_poly::ElementId near0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId near1 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.2F });
	const quader_poly::ElementId near2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.2F });
	const quader_poly::ElementId near3 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.2F });
	fixture.near_face = quader_poly::add_face(document, std::array { near0, near1, near2, near3 }, 0U);

	const quader_poly::ElementId far0 = quader_poly::add_vertex(document, { -0.5F, -0.05F, 0.0F });
	const quader_poly::ElementId far1 = quader_poly::add_vertex(document, { 0.5F, -0.05F, 0.0F });
	const quader_poly::ElementId far2 = quader_poly::add_vertex(document, { 0.5F, 0.05F, 0.0F });
	const quader_poly::ElementId far3 = quader_poly::add_vertex(document, { -0.5F, 0.05F, 0.0F });
	fixture.interior_face = quader_poly::add_face(document, std::array { far0, far1, far2, far3 }, 0U);
	fixture.interior_edge = quader_poly::make_edge(far0, far1);
	fixture.interior_vertex = far0;

	fixture.object.selected = true;
	fixture.outside_ray = { { 0.0F, -0.05F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
	fixture.inside_ray = { { 0.0F, -0.05F, 0.1F }, { 0.0F, 0.0F, -1.0F } };
	return fixture;
}

quader_editor_core::PickObjectBounds pick_bounds_for_object(const quader_editor_core::PolygonObject& object)
{
	quader_editor_core::PickObjectBounds bounds;
	bounds.object_id = object.id;
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		if (bounds.empty) {
			bounds.min = vertex.position;
			bounds.max = vertex.position;
			bounds.empty = false;
			continue;
		}
		bounds.min.x = std::min(bounds.min.x, vertex.position.x);
		bounds.min.y = std::min(bounds.min.y, vertex.position.y);
		bounds.min.z = std::min(bounds.min.z, vertex.position.z);
		bounds.max.x = std::max(bounds.max.x, vertex.position.x);
		bounds.max.y = std::max(bounds.max.y, vertex.position.y);
		bounds.max.z = std::max(bounds.max.z, vertex.position.z);
	}
	return bounds;
}

quader_editor_core::PickHitRecord face_hit(int object_id, quader_poly::ElementId face_id, float ray_distance)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
        hit.kind = quader_poly::ElementKind::Face;
        hit.face_id = face_id;
	hit.ray_distance = ray_distance;
	return hit;
}

quader_editor_core::PickHitRecord edge_hit(int object_id, quader_poly::Edge edge, float ray_distance)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
        hit.kind = quader_poly::ElementKind::Edge;
        hit.edge = edge;
	hit.ray_distance = ray_distance;
	return hit;
}

void hover_tracks_face_candidates_and_selected_feedback()
{
	std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };

        const quader_editor_core::SelectionHoverUpdateResult hover_result =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), true);

        ASSERT_TRUE(require_test(hover_result.changed, "face hover should report a changed target for a fresh hit"));
	ASSERT_TRUE(require_test(hover_result.hover.object_id == 7 && hover_result.hover.candidate_object_id == 7,
		"face hover should update both the active object and candidate object"));
        ASSERT_TRUE(require_test(hover_result.hover.kind ==
                             quader_editor_core::SelectionHoverKind::Face &&
                         hover_result.hover.face_id !=
                             quader_poly::kInvalidElementId,
                     "face hover should keep the picked face id"));
        ASSERT_TRUE(require_test(hover_result.hover.selected_modifier_pressed,
		"face hover should remember the selected-target modifier when hovering a component"));

	objects[0].selected = true;
        objects[0].selection.mode = quader_poly::SelectionMode::Face;
        objects[0].selection.faces.push_back(hover_result.hover.face_id);
	objects[0].selection.has_active = true;
        objects[0].selection.active_kind = quader_poly::ElementKind::Face;
        objects[0].selection.active_face = hover_result.hover.face_id;

        ASSERT_TRUE(require_test(
            quader_editor_core::selection_hover_target_matches_selection(
                objects, quader_editor_core::SelectionMode::Face,
                hover_result.hover),
            "hover feedback should recognize when the hovered face is already "
            "selected"));
        ASSERT_TRUE(require_test(quader_editor_core::selection_hover_target_is_selected(
                         objects, quader_editor_core::SelectionMode::Face,
                         hover_result.hover),
                     "selected-target hover should require the modifier flag"));

        const quader_editor_core::SelectionOverlayFrame frame =
            quader_editor_core::build_selection_overlay_frame(
                objects, quader_editor_core::SelectionMode::Face,
                hover_result.hover);
        ASSERT_TRUE(require_test(frame.hover_target_selected, "selection overlay frame should receive selected-hover feedback"));
}

void component_hover_preserves_candidate_on_miss()
{
	const std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	quader_editor_core::SelectionHoverState current_hover;
	current_hover.candidate_object_id = 7;

        const quader_editor_core::SelectionHoverUpdateResult edge_miss =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Edge, current_hover,
                miss_ray(), false);
        ASSERT_TRUE(require_test(!edge_miss.changed && edge_miss.hover.candidate_object_id == 7,
		"component hover misses should keep the previous candidate object"));
        ASSERT_TRUE(require_test(
            edge_miss.hover.object_id == -1 &&
                edge_miss.hover.kind ==
                    quader_editor_core::SelectionHoverKind::None,
            "component hover misses should clear the immediate hover target"));

        const quader_editor_core::SelectionHoverUpdateResult object_miss =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Object,
                current_hover, miss_ray(), false);
        ASSERT_TRUE(require_test(!object_miss.changed && object_miss.hover.candidate_object_id == 7,
		"object hover misses should keep the previous mesh source-wire candidate"));
}

void component_hover_rejects_single_mesh_occluded_source_edges()
{
	const std::vector<quader_editor_core::PolygonObject> objects { occluded_back_edge_object() };
	quader_editor_core::SelectionHoverState current_hover;
	current_hover.candidate_object_id = 7;
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Edge;
        options.edge_radius = 0.12F;
	options.pick_backfaces = true;

        const quader_editor_core::SelectionHoverUpdateResult hover =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Edge, current_hover,
                {{0.0F, -0.05F, 5.0F}, {0.0F, 0.0F, -1.0F}}, false, options);

        ASSERT_TRUE(require_test(hover.hover.candidate_object_id == 7,
		"occluded source-edge hover should keep the sticky component candidate"));
        ASSERT_TRUE(require_test(hover.hover.kind ==
                         quader_editor_core::SelectionHoverKind::None,
                     "occluded source-edge hover should not expose an edge "
                     "hidden behind the same mesh surface"));
}

void flipped_surface_picking_uses_nearest_face_not_farthest_frontface()
{
	const BackfacingOccludedInteriorFixture fixture = backfacing_occluded_interior_fixture();
	const std::vector<quader_editor_core::PolygonObject> objects { fixture.object };
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Face;
        options.pick_backfaces = true;
	options.prefer_front_facing_faces = false;

	const std::optional<quader_editor_core::PickHitRecord> outside_hit =
		quader_editor_core::pick_nearest(objects, { fixture.outside_ray, options });
        ASSERT_TRUE(require_test(outside_hit.has_value() &&
                         outside_hit->kind == quader_poly::ElementKind::Face &&
                         outside_hit->face_id == fixture.near_face,
                     "outside flipped face picking should hit the nearest "
                     "exterior/back-facing surface"));

        const std::optional<quader_editor_core::PickHitRecord> inside_hit =
		quader_editor_core::pick_nearest(objects, { fixture.inside_ray, options });
        ASSERT_TRUE(require_test(inside_hit.has_value() &&
                         inside_hit->kind == quader_poly::ElementKind::Face &&
                         inside_hit->face_id == fixture.interior_face,
                     "inside flipped face picking should hit the visible "
                     "interior surface"));

        std::vector<quader_editor_core::PickObjectFaceGeometry> geometry =
		quader_editor_core::build_pick_face_geometry(objects, options);
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects.front()) };
	const std::optional<quader_editor_core::PickHitRecord> cached_outside_hit =
		quader_editor_core::pick_nearest(objects, { fixture.outside_ray, options }, bounds, geometry);
	ASSERT_TRUE(require_test(cached_outside_hit.has_value() && cached_outside_hit->face_id == fixture.near_face,
		"cached outside flipped face picking should use the nearest exterior/back-facing surface"));
}

void flipped_source_wire_hover_rejects_occluded_interior_handles_from_outside()
{
	const BackfacingOccludedInteriorFixture fixture = backfacing_occluded_interior_fixture();
	const std::vector<quader_editor_core::PolygonObject> objects { fixture.object };
	quader_editor_core::SelectionHoverState current_hover;
	current_hover.candidate_object_id = fixture.object.id;

	quader_poly::PickOptions edge_options;
        edge_options.mode = quader_poly::SelectionMode::Edge;
        edge_options.edge_radius = 0.12F;
	edge_options.pick_backfaces = true;
        const quader_editor_core::SelectionHoverUpdateResult
            outside_edge_hover = quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Edge, current_hover,
                fixture.outside_ray, false, edge_options);
        ASSERT_TRUE(require_test(outside_edge_hover.hover.kind ==
                         quader_editor_core::SelectionHoverKind::None,
                     "outside source-wire hover should not pick an interior "
                     "edge occluded by a flipped surface"));

        const quader_editor_core::SelectionHoverUpdateResult inside_edge_hover =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Edge, current_hover,
                fixture.inside_ray, false, edge_options);
        ASSERT_TRUE(require_test(
            inside_edge_hover.hover.kind ==
                    quader_editor_core::SelectionHoverKind::Edge &&
                inside_edge_hover.hover.edge == fixture.interior_edge,
            "inside source-wire hover should pick the visible interior edge"));

        quader_poly::PickOptions vertex_options;
        vertex_options.mode = quader_poly::SelectionMode::Vertex;
        vertex_options.vertex_radius = 0.12F;
	vertex_options.pick_backfaces = true;
        const quader_editor_core::SelectionHoverUpdateResult
            outside_vertex_hover = quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Vertex,
                current_hover, {{-0.5F, -0.05F, 5.0F}, {0.0F, 0.0F, -1.0F}},
                false, vertex_options);
        ASSERT_TRUE(require_test(outside_vertex_hover.hover.kind ==
                         quader_editor_core::SelectionHoverKind::None,
                     "outside source-wire hover should not pick an interior "
                     "vertex occluded by a flipped surface"));

        const quader_editor_core::SelectionHoverUpdateResult
            inside_vertex_hover = quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Vertex,
                current_hover, {{-0.5F, -0.05F, 0.1F}, {0.0F, 0.0F, -1.0F}},
                false, vertex_options);
        ASSERT_TRUE(require_test(
            inside_vertex_hover.hover.kind ==
                    quader_editor_core::SelectionHoverKind::Vertex &&
                inside_vertex_hover.hover.vertex_id == fixture.interior_vertex,
            "inside source-wire hover should pick the visible interior vertex"));
}

void hover_can_use_precomputed_object_bounds()
{
	const std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };

        const quader_editor_core::SelectionHoverUpdateResult uncached =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), false);
        const quader_editor_core::SelectionHoverUpdateResult cached =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), false, {}, bounds);

        ASSERT_TRUE(require_test(cached.changed == uncached.changed &&
			cached.hover.object_id == uncached.hover.object_id &&
			cached.hover.kind == uncached.hover.kind &&
			cached.hover.face_id == uncached.hover.face_id,
		"precomputed object bounds should preserve hover picking results"));

	std::vector<quader_editor_core::PickObjectBounds> miss_bounds = bounds;
	miss_bounds[0].min = { 40.0F, 40.0F, 40.0F };
	miss_bounds[0].max = { 42.0F, 42.0F, 42.0F };
        const quader_editor_core::SelectionHoverUpdateResult missed =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), false, {}, miss_bounds);
        ASSERT_TRUE(require_test(!missed.changed &&
                         missed.hover.kind ==
                             quader_editor_core::SelectionHoverKind::None,
                     "precomputed object bounds should be used as the hover "
                     "broadphase filter"));
}

void hover_can_use_precomputed_face_geometry()
{
	const std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };
	std::vector<quader_editor_core::PickObjectFaceGeometry> geometry =
		quader_editor_core::build_pick_face_geometry(objects);
	ASSERT_TRUE(require_test(!geometry.empty() && geometry[0].object_id == 7 && !geometry[0].triangles.empty(),
		"face pick geometry cache should build triangles for the object"));

        const quader_editor_core::SelectionHoverUpdateResult uncached =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), false);
        const quader_editor_core::SelectionHoverUpdateResult cached =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), false, {}, bounds, geometry);

        ASSERT_TRUE(require_test(cached.changed == uncached.changed &&
			cached.hover.object_id == uncached.hover.object_id &&
			cached.hover.kind == uncached.hover.kind &&
			cached.hover.face_id == uncached.hover.face_id,
		"precomputed face geometry should preserve hover picking results"));

	geometry[0].triangles.clear();
        const quader_editor_core::SelectionHoverUpdateResult missed =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                front_center_ray(), false, {}, bounds, geometry);
        ASSERT_TRUE(require_test(!missed.changed &&
                         missed.hover.kind ==
                             quader_editor_core::SelectionHoverKind::None,
                     "cached face geometry should be used instead of "
                     "retriangulating the document on hover"));
}

void open_mesh_hover_picks_visible_back_sides()
{
	const std::vector<quader_editor_core::PolygonObject> objects { open_top_cube_object(7) };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Face;
        options.pick_backfaces = true;
	const quader_poly::Ray inside_to_side_ray { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };

	const std::optional<quader_editor_core::PickHitRecord> uncached_hit =
		quader_editor_core::pick_nearest(objects, { inside_to_side_ray, options });
        ASSERT_TRUE(require_test(uncached_hit.has_value() &&
                         uncached_hit->kind == quader_poly::ElementKind::Face,
                     "uncached face picking should hit a visible back side in "
                     "an open mesh"));

        std::vector<quader_editor_core::PickObjectFaceGeometry> geometry =
		quader_editor_core::build_pick_face_geometry(objects, options);
	const std::optional<quader_editor_core::PickHitRecord> cached_hit =
		quader_editor_core::pick_nearest(objects, { inside_to_side_ray, options }, bounds, geometry);
	ASSERT_TRUE(require_test(cached_hit.has_value() && cached_hit->face_id == uncached_hit->face_id,
		"cached face picking should also hit the visible back side"));

        const quader_editor_core::SelectionHoverUpdateResult hover =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Face, {},
                inside_to_side_ray, false, options, bounds, geometry);
        ASSERT_TRUE(require_test(hover.changed && hover.hover.object_id == 7 &&
                         hover.hover.kind ==
                             quader_editor_core::SelectionHoverKind::Face &&
                         hover.hover.face_id == cached_hit->face_id,
                     "face hover should produce hover wires for a visible back "
                     "side in an open mesh"));
}

void session_pick_selects_faces_through_shared_picking()
{
	std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
        quader_editor_core::SelectionMode mode =
            quader_editor_core::SelectionMode::Object;
        int active_object_id = -1;

        const bool selected = quader_editor_core::pick_select_face(
            objects, mode, quader_editor_core::SelectionEditOperation::Replace,
            front_center_ray(), active_object_id);

        ASSERT_TRUE(require_test(selected &&
                         mode == quader_editor_core::SelectionMode::Face,
                     "pick-select-face should switch to face selection mode"));
        ASSERT_TRUE(require_test(active_object_id == 7 && objects[0].selected && !objects[0].selection.faces.empty(),
		"pick-select-face should select a face on the hit object and make it active"));

	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Face;
        const std::optional<quader_editor_core::PickHitRecord> hit =
		quader_editor_core::pick_nearest(objects, { front_center_ray(), options });
        ASSERT_TRUE(require_test(hit.has_value() && hit->object_id == 7 &&
                         hit->kind == quader_poly::ElementKind::Face,
                     "pick_nearest should expose the same face hit through "
                     "editor-core records"));
}

void hidden_material_slot_filter_rebuilds_cached_face_geometry()
{
	const std::vector<quader_editor_core::PolygonObject> objects { stacked_face_object() };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };
	quader_poly::PickOptions face_options;
        face_options.mode = quader_poly::SelectionMode::Face;
        std::vector<quader_editor_core::PickObjectFaceGeometry> unfiltered_geometry =
		quader_editor_core::build_pick_face_geometry(objects, face_options);
        std::vector<quader_editor_core::PickObjectFaceGeometry>
            filtered_geometry = quader_editor_core::build_pick_face_geometry(
                objects,
                hidden_slot_pick_options(quader_poly::SelectionMode::Face));
        ASSERT_TRUE(require_test(unfiltered_geometry[0].triangles.size() > filtered_geometry[0].triangles.size(),
		"hidden-slot face geometry should exclude hidden triangles"));

	const std::optional<quader_editor_core::PickHitRecord> unfiltered_hit =
		quader_editor_core::pick_nearest(objects, { front_center_ray(), face_options }, bounds, unfiltered_geometry);
        const std::optional<quader_editor_core::PickHitRecord> filtered_hit =
            quader_editor_core::pick_nearest(
                objects,
                {front_center_ray(),
                 hidden_slot_pick_options(quader_poly::SelectionMode::Face)},
                bounds, filtered_geometry);
        ASSERT_TRUE(require_test(unfiltered_hit.has_value() && filtered_hit.has_value(),
		"cached picking should hit with and without hidden-slot filtering"));
	ASSERT_TRUE(require_test(unfiltered_hit->face_id != filtered_hit->face_id,
		"filtered cached picking should not reuse the unfiltered hidden face"));
}

void hidden_material_slot_filter_limits_all_face_selection()
{
	std::vector<quader_editor_core::PolygonObject> objects { adjacent_face_object() };
        quader_editor_core::SelectionMode mode =
            quader_editor_core::SelectionMode::Face;
        int active_object_id = -1;
        const bool selected = quader_editor_core::pick_select_all_faces(
            objects, mode, quader_editor_core::SelectionEditOperation::Replace,
            {{-0.5F, 0.0F, 5.0F}, {0.0F, 0.0F, -1.0F}}, active_object_id,
            hidden_slot_pick_options(quader_poly::SelectionMode::Face));
        ASSERT_TRUE(require_test(selected && objects[0].selection.faces.size() == 1U,
		"all-face selection should select only pickable visible faces"));
	const quader_poly::Face* selected_face = quader_poly::find_face(objects[0].document, objects[0].selection.faces.front());
	ASSERT_TRUE(require_test(selected_face != nullptr && selected_face->material_slot == 0U,
		"all-face selection should not include hidden material-slot faces"));
}

void hidden_material_slot_filter_limits_all_vertex_selection()
{
	std::vector<quader_editor_core::PolygonObject> objects { adjacent_face_object() };
        quader_editor_core::SelectionMode mode =
            quader_editor_core::SelectionMode::Vertex;
        int active_object_id = -1;
        const bool selected = quader_editor_core::pick_select_all_vertices(
            objects, mode, quader_editor_core::SelectionEditOperation::Replace,
            {{0.0F, 1.0F, 5.0F}, {0.0F, 0.0F, -1.0F}}, active_object_id,
            hidden_slot_pick_options(quader_poly::SelectionMode::Vertex));
        ASSERT_TRUE(require_test(selected && objects[0].selection.vertices.size() == 4U,
		"all-vertex selection should select only vertices incident to visible faces"));

	const quader_poly::ElementId hidden_only_vertex = objects[0].document.faces.back().vertices[1];
	ASSERT_TRUE(require_test(!quader_poly::selection_contains(objects[0].selection, hidden_only_vertex),
		"all-vertex selection should not include hidden-only vertices"));
}

void editor_session_pick_select_reports_only_material_selection_changes()
{
        quader_editor_core::PolygonObject object = cube_object(1);
        object.selected = true;
	quader_editor_core::EditorSession session({std::move(object)});
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);

        ASSERT_TRUE(require_test(session.pick_select_face(
                         quader_editor_core::SelectionEditOperation::Replace,
                         front_center_ray()),
                     "first face pick should report a changed selection"));
        const std::uint64_t selected_revision = session.state().selection_revision;

        ASSERT_TRUE(require_test(!session.pick_select_face(
                         quader_editor_core::SelectionEditOperation::Add,
                         front_center_ray()),
                     "adding an already selected face should not report a "
                     "selection change"));
        ASSERT_TRUE(require_test(session.state().selection_revision == selected_revision,
		"adding an already selected face should not bump selection revision"));

        ASSERT_TRUE(require_test(
            session.pick_select_face(
                quader_editor_core::SelectionEditOperation::Remove,
                front_center_ray()),
            "removing the selected face should report a changed selection"));
        ASSERT_TRUE(require_test(session.state().selection_revision > selected_revision,
		"removing the selected face should bump selection revision"));
}

void default_component_picking_has_editor_leeway()
{
	const quader_poly::Document document = quader_poly::make_cube({});

	quader_poly::PickOptions edge_options;
        edge_options.mode = quader_poly::SelectionMode::Edge;
        const quader_poly::PickResult near_edge = quader_poly::pick_element(
		document,
		{ { 0.0F, 1.09F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
		edge_options);
        ASSERT_TRUE(require_test(
            near_edge.hit && near_edge.kind == quader_poly::ElementKind::Edge,
            "default edge picking should allow a small near-edge screen miss"));

        quader_poly::PickOptions vertex_options;
        vertex_options.mode = quader_poly::SelectionMode::Vertex;
        const quader_poly::PickResult near_vertex = quader_poly::pick_element(
		document,
		{ { 1.078F, 1.078F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
		vertex_options);
        ASSERT_TRUE(require_test(near_vertex.hit &&
                         near_vertex.kind == quader_poly::ElementKind::Vertex,
                     "default vertex picking should allow a small near-vertex "
                     "screen miss"));
}

void selection_pick_options_normalize_host_radius_inputs()
{
	const quader_poly::PickOptions defaulted =
		quader_editor_core::selection_pick_options_from_radii(
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity());
	ASSERT_TRUE(require_test(nearly_equal(defaulted.vertex_radius, 0.12F),
		"selection pick options should default non-finite vertex radii"));
	ASSERT_TRUE(require_test(nearly_equal(defaulted.edge_radius, 0.10F),
		"selection pick options should default non-finite edge radii"));
	ASSERT_TRUE(require_test(defaulted.pick_backfaces,
		"selection pick options should keep non-selection tool callers two-sided by default"));

	const quader_poly::PickOptions front_only =
		quader_editor_core::selection_pick_options_from_radii(0.12, 0.10, false);
	ASSERT_TRUE(require_test(!front_only.pick_backfaces,
		"selection pick options should preserve an explicit front-facing-only request"));

	const quader_poly::PickOptions clamped =
		quader_editor_core::selection_pick_options_from_radii(-4.0, 50.0);
	ASSERT_TRUE(require_test(nearly_equal(clamped.vertex_radius, 0.001F),
		"selection pick options should clamp vertex radius to the editor minimum"));
	ASSERT_TRUE(require_test(nearly_equal(clamped.edge_radius, 2.0F),
		"selection pick options should clamp edge radius to the editor maximum"));

	const quader_poly::PickOptions preserved =
		quader_editor_core::selection_pick_options_from_radii(0.25, 0.75);
	ASSERT_TRUE(require_test(nearly_equal(preserved.vertex_radius, 0.25F),
		"selection pick options should preserve finite in-range vertex radii"));
	ASSERT_TRUE(require_test(nearly_equal(preserved.edge_radius, 0.75F),
		"selection pick options should preserve finite in-range edge radii"));
}

void backface_policy_filters_face_edge_and_vertex_clicks_and_hover()
{
	const std::array modes {
		quader_poly::SelectionMode::Face,
		quader_poly::SelectionMode::Edge,
		quader_poly::SelectionMode::Vertex,
	};
	const std::array editor_modes {
		quader_editor_core::SelectionMode::Face,
		quader_editor_core::SelectionMode::Edge,
		quader_editor_core::SelectionMode::Vertex,
	};
	const std::array rays {
		quader_poly::Ray { { 0.0F, 0.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
		quader_poly::Ray { { 0.0F, -1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
		quader_poly::Ray { { -1.0F, -1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
	};

	for (std::size_t index = 0U; index < modes.size(); ++index) {
		quader_poly::PickOptions options;
		options.mode = modes[index];
		options.vertex_radius = 0.05F;
		options.edge_radius = 0.05F;
		options.pick_backfaces = false;

		std::vector<quader_editor_core::PolygonObject> backfacing {
			single_quad_object(7, true),
		};
		ASSERT_TRUE(require_test(
			!quader_editor_core::pick_nearest(backfacing, { rays[index], options }).has_value(),
			"front-facing-only click picking should reject a solely back-facing component"));
		const quader_editor_core::SelectionHoverUpdateResult rejected_hover =
			quader_editor_core::update_selection_hover(
				backfacing, editor_modes[index],
				{}, rays[index], false, options);
		ASSERT_TRUE(require_test(
			rejected_hover.hover.object_id < 0,
			"front-facing-only hover should reject a solely back-facing component"));

		options.pick_backfaces = true;
		ASSERT_TRUE(require_test(
			quader_editor_core::pick_nearest(backfacing, { rays[index], options }).has_value(),
			"two-sided click picking should accept a back-facing component"));
		const quader_editor_core::SelectionHoverUpdateResult accepted_hover =
			quader_editor_core::update_selection_hover(
				backfacing, editor_modes[index],
				{}, rays[index], false, options);
		ASSERT_TRUE(require_test(
			accepted_hover.hover.object_id == 7,
			"two-sided hover should accept a back-facing component"));

		options.pick_backfaces = false;
		std::vector<quader_editor_core::PolygonObject> frontfacing {
			single_quad_object(8, false),
		};
		ASSERT_TRUE(require_test(
			quader_editor_core::pick_nearest(frontfacing, { rays[index], options }).has_value(),
			"front-facing-only click picking should retain front-facing components"));
	}

	quader_editor_core::PolygonObject loose;
	loose.id = 9;
	[[maybe_unused]] const quader_poly::ElementId loose_vertex =
		quader_poly::add_vertex(loose.document, {});
	quader_poly::PickOptions loose_options;
	loose_options.mode = quader_poly::SelectionMode::Vertex;
	loose_options.vertex_radius = 0.05F;
	loose_options.pick_backfaces = false;
	ASSERT_TRUE(require_test(
		quader_editor_core::pick_nearest(
			std::vector<quader_editor_core::PolygonObject> { loose },
			{ front_center_ray(), loose_options }).has_value(),
		"front-facing-only picking should retain loose vertices"));
}

void reflected_tiny_nonuniform_transforms_reverse_authored_facing()
{
	const std::vector<quader_editor_core::PolygonObject> objects {
		single_quad_object(7, false),
	};
	const std::vector<quader_editor_core::PickObjectFaceGeometry> face_geometry =
		quader_editor_core::build_pick_face_geometry(objects);
	const std::array positive_transform {
		scaled_pick_transform(7, 0.001F, 0.002F, 0.004F),
	};
	const std::array reflected_transform {
		scaled_pick_transform(7, -0.001F, 0.002F, 0.004F),
	};
	const std::array modes {
		quader_poly::SelectionMode::Face,
		quader_poly::SelectionMode::Edge,
		quader_poly::SelectionMode::Vertex,
	};
	const std::array priority_modes {
		quader_editor_core::SelectionMode::Face,
		quader_editor_core::SelectionMode::Edge,
		quader_editor_core::SelectionMode::Vertex,
	};
	const std::array positive_rays {
		quader_poly::Ray { { 0.0F, 0.0F, 0.02F }, { 0.0F, 0.0F, -1.0F } },
		quader_poly::Ray { { 0.0F, -0.002F, 0.02F }, { 0.0F, 0.0F, -1.0F } },
		quader_poly::Ray { { -0.001F, -0.002F, 0.02F }, { 0.0F, 0.0F, -1.0F } },
	};
	const std::array reflected_rays {
		positive_rays[0],
		positive_rays[1],
		quader_poly::Ray { { 0.001F, -0.002F, 0.02F }, { 0.0F, 0.0F, -1.0F } },
	};

	for (std::size_t index = 0U; index < modes.size(); ++index) {
		quader_poly::PickOptions options;
		options.mode = modes[index];
		options.vertex_radius = 0.00005F;
		options.edge_radius = 0.00005F;
		options.pick_backfaces = false;
		ASSERT_TRUE(require_test(
			quader_editor_core::pick_nearest(
				objects, { positive_rays[index], options }, {}, face_geometry,
				positive_transform).has_value(),
			"tiny nonuniform positive transforms should preserve authored front-facing selection"));
		ASSERT_TRUE(require_test(
			!quader_editor_core::pick_nearest(
				objects, { reflected_rays[index], options }, {}, face_geometry,
				reflected_transform).has_value(),
			"tiny reflected transforms should reverse authored component facing"));
		const std::array priority { priority_modes[index] };
		ASSERT_TRUE(require_test(
			!quader_editor_core::pick_nearest_priority(
				objects, { reflected_rays[index], options }, priority, {},
				face_geometry, reflected_transform).has_value(),
			"priority picking should apply the same reflected winding policy"));
		options.pick_backfaces = true;
		ASSERT_TRUE(require_test(
			quader_editor_core::pick_nearest(
				objects, { reflected_rays[index], options }, {}, face_geometry,
				reflected_transform).has_value(),
			"two-sided picking should retain reflected back-facing components"));
	}
}

void component_pick_distance_uses_geometry_ray_distance()
{
	std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Vertex;
        options.vertex_radius = 0.12F;

	const quader_poly::Ray ray {
		{ 1.05F, 1.0F, 5.0F },
		{ 0.0F, 0.0F, -2.0F },
	};
	const std::optional<quader_editor_core::PickHitRecord> hit =
		quader_editor_core::pick_nearest(objects, { ray, options });

        ASSERT_TRUE(require_test(hit.has_value() &&
                         hit->kind == quader_poly::ElementKind::Vertex,
                     "vertex picking should hit through non-unit rays"));
        ASSERT_TRUE(require_test(std::abs(hit->pick_distance - 0.05F) <= 0.0001F,
		"component pick distance should be perpendicular distance to the geometry ray"));
	ASSERT_TRUE(require_test(std::abs(hit->ray_distance - 4.0F) <= 0.0001F,
		"component ray distance should preserve document picking's normalized ray distance"));
}

void click_and_drag_pick_plans_stay_in_editor_core()
{
	const std::vector<quader_editor_core::PickHitRecord> surface_hits {
		face_hit(1, 11, 1.0F),
		face_hit(2, 12, 2.0F),
	};
	quader_editor_core::PickSelectionState object_selection;
        object_selection.mode = quader_editor_core::SelectionMode::Object;
        quader_editor_core::ClickPickRequest click_request;
	click_request.screen_position = { 12.0F, 24.0F };
	quader_editor_core::PickCycleState cycle;

	quader_editor_core::ClickPickPlan click_plan =
		quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click_request);
        ASSERT_TRUE(require_test(
            click_plan.has_target &&
                click_plan.hit_set == quader_editor_core::PickHitSet::Surface &&
                click_plan.hit_index == 0 && click_plan.used_pick_cycle,
            "replace click planning should choose the front surface hit "
            "through the pick cycle"));

        click_plan = quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click_request);
	ASSERT_TRUE(require_test(click_plan.has_target && click_plan.hit_index == 1 && click_plan.cycle.cycled,
		"repeat replace clicks at the same screen position should cycle to the next object hit"));

	const quader_poly::Edge selected_edge = quader_poly::make_edge(3, 4);
	const std::vector<quader_editor_core::PickHitRecord> handle_hits {
		edge_hit(2, selected_edge, 0.5F),
	};
	click_request.handles_enabled = true;
	click_plan = quader_editor_core::plan_click_pick(cycle, object_selection, handle_hits, surface_hits, click_request);
        ASSERT_TRUE(require_test(click_plan.has_target &&
                         click_plan.hit_set ==
                             quader_editor_core::PickHitSet::Handle &&
                         click_plan.hit_index == 0,
                     "click planning should prefer enabled handle hits before "
                     "surface hits"));

        quader_editor_core::PickSelectionState edge_selection;
        edge_selection.mode = quader_editor_core::SelectionMode::Edge;
        edge_selection.selected_edges.push_back(
            {2, quader_poly::ElementKind::Edge, quader_poly::kInvalidElementId,
             selected_edge});
        quader_editor_core::DragSeedPickRequest drag_request;
	drag_request.handles_enabled = true;
	drag_request.allow_selected_target = true;
	drag_request.additive = true;
	drag_request.preserve_selected_additive_target = true;
	const quader_editor_core::DragSeedPickPlan drag_plan =
		quader_editor_core::plan_drag_seed_pick(edge_selection, handle_hits, surface_hits, drag_request);
        ASSERT_TRUE(require_test(drag_plan.has_target &&
                         drag_plan.hit_set ==
                             quader_editor_core::PickHitSet::Handle &&
                         drag_plan.keep_existing_selection &&
                         drag_plan.preferred_object_id == 2,
                     "drag seed planning should preserve selected additive "
                     "handle targets when requested"));
}

} // namespace

TEST(QdrModelingRuntimePickingTests,
	HoverTracksFaceCandidatesAndSelectedFeedback) {
	hover_tracks_face_candidates_and_selected_feedback();
}

TEST(QdrModelingRuntimePickingTests, ComponentHoverPreservesCandidateOnMiss) {
	component_hover_preserves_candidate_on_miss();
}

TEST(QdrModelingRuntimePickingTests,
	ComponentHoverRejectsSingleMeshOccludedSourceEdges) {
	component_hover_rejects_single_mesh_occluded_source_edges();
}

TEST(QdrModelingRuntimePickingTests,
	FlippedSurfacePickingUsesNearestFaceNotFarthestFrontface) {
	flipped_surface_picking_uses_nearest_face_not_farthest_frontface();
}

TEST(QdrModelingRuntimePickingTests,
	FlippedSourceWireHoverRejectsOccludedInteriorHandlesFromOutside) {
	flipped_source_wire_hover_rejects_occluded_interior_handles_from_outside();
}

TEST(QdrModelingRuntimePickingTests, HoverCanUsePrecomputedObjectBounds) {
	hover_can_use_precomputed_object_bounds();
}

TEST(QdrModelingRuntimePickingTests, HoverCanUsePrecomputedFaceGeometry) {
	hover_can_use_precomputed_face_geometry();
}

TEST(QdrModelingRuntimePickingTests, OpenMeshHoverPicksVisibleBackSides) {
	open_mesh_hover_picks_visible_back_sides();
}

TEST(QdrModelingRuntimePickingTests,
	SessionPickSelectsFacesThroughSharedPicking) {
	session_pick_selects_faces_through_shared_picking();
}

TEST(QdrModelingRuntimePickingTests,
	HiddenMaterialSlotFilterRebuildsCachedFaceGeometry) {
	hidden_material_slot_filter_rebuilds_cached_face_geometry();
}

TEST(QdrModelingRuntimePickingTests,
	HiddenMaterialSlotFilterLimitsAllFaceSelection) {
	hidden_material_slot_filter_limits_all_face_selection();
}

TEST(QdrModelingRuntimePickingTests,
	HiddenMaterialSlotFilterLimitsAllVertexSelection) {
	hidden_material_slot_filter_limits_all_vertex_selection();
}

TEST(QdrModelingRuntimePickingTests,
	EditorSessionPickSelectReportsOnlyMaterialSelectionChanges) {
	editor_session_pick_select_reports_only_material_selection_changes();
}

TEST(QdrModelingRuntimePickingTests, DefaultComponentPickingHasEditorLeeway) {
	default_component_picking_has_editor_leeway();
}

TEST(QdrModelingRuntimePickingTests,
	SelectionPickOptionsNormalizeHostRadiusInputs) {
	selection_pick_options_normalize_host_radius_inputs();
}

TEST(QdrModelingRuntimePickingTests,
	BackfacePolicyFiltersFaceEdgeAndVertexClicksAndHover) {
	backface_policy_filters_face_edge_and_vertex_clicks_and_hover();
}

TEST(QdrModelingRuntimePickingTests,
	ReflectedTinyNonuniformTransformsReverseAuthoredFacing) {
	reflected_tiny_nonuniform_transforms_reverse_authored_facing();
}

TEST(QdrModelingRuntimePickingTests,
	ComponentPickDistanceUsesGeometryRayDistance) {
	component_pick_distance_uses_geometry_ray_distance();
}

TEST(QdrModelingRuntimePickingTests,
	ClickAndDragPickPlansStayInEditorCore) {
	click_and_drag_pick_plans_stay_in_editor_core();
}
