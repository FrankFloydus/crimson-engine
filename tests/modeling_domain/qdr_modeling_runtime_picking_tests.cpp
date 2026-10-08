////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/move_tool.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
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

struct LoosePartFixture {
  quader_editor_core::PolygonObject object;
  std::array<std::array<quader_poly::ElementId, 6>, 2> vertices;
  std::array<std::array<quader_poly::Edge, 7>, 2> edges;
  std::array<std::array<quader_poly::ElementId, 2>, 2> faces;
};

/// Keeps authored grid identities explicit for typed loop and ring assertions.
struct ComponentTraversalGrid {
  quader_editor_core::PolygonObject object;
  std::array<std::array<quader_poly::ElementId, 5>, 5> vertices{};
  std::array<std::array<quader_poly::ElementId, 4>, 4> faces{};
};

ComponentTraversalGrid component_traversal_grid() {
  ComponentTraversalGrid fixture;
  fixture.object.id = 7;
  auto &document = fixture.object.document;
  for (std::size_t row = 0; row < fixture.vertices.size(); ++row) {
    for (std::size_t column = 0; column < fixture.vertices[row].size(); ++column) {
      fixture.vertices[row][column] = quader_poly::add_vertex(
          document, {static_cast<float>(column), static_cast<float>(row), 0.0F});
    }
  }
  for (std::size_t row = 0; row < fixture.faces.size(); ++row) {
    for (std::size_t column = 0; column < fixture.faces[row].size(); ++column) {
      fixture.faces[row][column] = quader_poly::add_face(
          document, std::array{fixture.vertices[row][column],
                               fixture.vertices[row][column + 1],
                               fixture.vertices[row + 1][column + 1],
                               fixture.vertices[row + 1][column]});
    }
  }
  return fixture;
}

quader_poly::Ray component_traversal_ray(float x, float y) {
  return {{x, y, 5.0F}, {0.0F, 0.0F, -1.0F}};
}

LoosePartFixture loose_part_fixture() {
  LoosePartFixture fixture;
  fixture.object.id = 7;
  const std::array positions{
      quader::QVec3{-1.0F, -1.0F, 0.0F},
      quader::QVec3{0.0F, -1.0F, 0.0F},
      quader::QVec3{0.0F, 1.0F, 0.0F},
      quader::QVec3{-1.0F, 1.0F, 0.0F},
      quader::QVec3{1.0F, -1.0F, 0.0F},
      quader::QVec3{1.0F, 1.0F, 0.0F},
  };
  for (std::size_t part = 0; part < fixture.vertices.size(); ++part) {
    auto &vertices = fixture.vertices[part];
    for (std::size_t index = 0; index < positions.size(); ++index) {
      auto position = positions[index];
      position.x += 4.0F * static_cast<float>(part);
      vertices[index] = quader_poly::add_vertex(fixture.object.document, position);
    }
    fixture.faces[part] = {
        quader_poly::add_face(fixture.object.document,
                             std::array{vertices[0], vertices[1], vertices[2],
                                        vertices[3]}),
        quader_poly::add_face(fixture.object.document,
                             std::array{vertices[1], vertices[4], vertices[5],
                                        vertices[2]}),
    };
    fixture.edges[part] = {
        quader_poly::make_edge(vertices[0], vertices[1]),
        quader_poly::make_edge(vertices[1], vertices[2]),
        quader_poly::make_edge(vertices[2], vertices[3]),
        quader_poly::make_edge(vertices[3], vertices[0]),
        quader_poly::make_edge(vertices[1], vertices[4]),
        quader_poly::make_edge(vertices[4], vertices[5]),
        quader_poly::make_edge(vertices[5], vertices[2]),
    };
  }
  return fixture;
}

quader_poly::Ray loose_part_ray(quader_editor_core::SelectionMode mode,
                                std::size_t part = 0) {
  const float x = mode == quader_editor_core::SelectionMode::Vertex ? -1.0F : -0.5F;
  const float y = mode == quader_editor_core::SelectionMode::Face ? 0.0F : -1.0F;
  return {{x + 4.0F * static_cast<float>(part), y, 5.0F}, {0.0F, 0.0F, -1.0F}};
}

void expect_loose_part_selection(const quader_poly::Selection &selection,
                                quader_editor_core::SelectionMode mode,
                                const LoosePartFixture &fixture,
                                std::size_t part) {
  if (mode == quader_editor_core::SelectionMode::Vertex) {
    EXPECT_TRUE(std::is_permutation(selection.vertices.begin(), selection.vertices.end(),
                                    fixture.vertices[part].begin(), fixture.vertices[part].end()));
  } else if (mode == quader_editor_core::SelectionMode::Edge) {
    const std::array expected{fixture.edges[part][0], fixture.edges[part][4]};
    EXPECT_TRUE(std::is_permutation(selection.edges.begin(), selection.edges.end(),
                                    expected.begin(), expected.end()));
  } else {
    EXPECT_TRUE(std::is_permutation(selection.faces.begin(), selection.faces.end(),
                                    fixture.faces[part].begin(), fixture.faces[part].end()));
  }
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

quader_editor_core::PolygonObject side_on_stair_object(int id)
{
  quader_editor_core::PolygonObject object;
  object.id = id;
  std::array<quader_poly::ElementId, 6> vertices;
  const std::array positions{
      quader::QVec3{-2.0F, 0.0F, 0.1875F},
      quader::QVec3{2.0F, 0.0F, 0.1875F},
      quader::QVec3{2.0F, 0.0F, -0.1875F},
      quader::QVec3{-2.0F, 0.0F, -0.1875F},
      quader::QVec3{-2.0F, -0.25F, 0.1875F},
      quader::QVec3{2.0F, -0.25F, 0.1875F},
  };
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(object.document, positions[index]);
  }
  (void)quader_poly::add_face(
      object.document,
      std::array{vertices[0], vertices[1], vertices[2], vertices[3]});
  (void)quader_poly::add_face(
      object.document,
      std::array{vertices[1], vertices[0], vertices[4], vertices[5]});
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

quader_editor_core::PickScreenContext component_pick_screen_context() {
  quader_editor_core::PickScreenContext context;
  context.camera.viewport_size = {800.0F, 800.0F};
  context.camera.fov_degrees = 90.0;
  context.screen_position = {400.0F, 400.0F};
  context.vertex_radius_pixels = 6.0;
  context.edge_radius_pixels = 6.0;
  return context;
}

void expect_same_component_hit(
    const quader_editor_core::PickHitRecord &actual,
    const quader_editor_core::PickHitRecord &expected) {
  EXPECT_EQ(actual.object_id, expected.object_id);
  EXPECT_EQ(actual.kind, expected.kind);
  EXPECT_EQ(actual.vertex_id, expected.vertex_id);
  EXPECT_EQ(actual.edge, expected.edge);
  EXPECT_NEAR(actual.local_position.x, expected.local_position.x, 0.00001F);
  EXPECT_NEAR(actual.local_position.y, expected.local_position.y, 0.00001F);
  EXPECT_NEAR(actual.local_position.z, expected.local_position.z, 0.00001F);
  EXPECT_NEAR(actual.pick_distance, expected.pick_distance, 0.00001F);
  EXPECT_NEAR(actual.ray_distance, expected.ray_distance, 0.00001F);
}

void expect_single_component_pick_queries(
    const std::vector<quader_editor_core::PolygonObject> &objects,
    const quader_editor_core::PickRequest &request,
    std::span<const quader_editor_core::PickObjectBounds> bounds,
    std::span<const quader_editor_core::PickObjectTransform> transforms,
    const std::optional<quader_editor_core::PickHitRecord> &expected) {
  const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
  const std::array priority{
      request.options.mode == quader_poly::SelectionMode::Vertex
          ? quader_editor_core::SelectionMode::Vertex
          : quader_editor_core::SelectionMode::Edge};
  for (const bool cached : std::array{false, true}) {
    SCOPED_TRACE(cached ? "cached" : "uncached");
    const auto query_bounds = cached ? bounds : decltype(bounds){};
    const auto query_geometry =
        cached ? std::span<const quader_editor_core::PickObjectFaceGeometry>{geometry}
               : std::span<const quader_editor_core::PickObjectFaceGeometry>{};
    std::vector nearest_hits{
        quader_editor_core::pick_nearest(
            objects, request, query_bounds, query_geometry, transforms),
        quader_editor_core::pick_nearest_priority(
            objects, request, priority, query_bounds, query_geometry, transforms)};
    if (!cached && transforms.empty()) {
      nearest_hits.push_back(quader_editor_core::pick_nearest(objects, request));
    }
    for (std::size_t index = 0; index < nearest_hits.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_EQ(nearest_hits[index].has_value(), expected.has_value());
      if (nearest_hits[index].has_value() && expected.has_value()) {
        expect_same_component_hit(*nearest_hits[index], *expected);
      }
    }
    const auto all = quader_editor_core::pick_all(
        objects, request, query_bounds, query_geometry, transforms);
    EXPECT_EQ(all.size(), expected.has_value() ? 1U : 0U);
    if (!all.empty() && expected.has_value()) {
      expect_same_component_hit(all.front(), *expected);
    }
    if (!cached && transforms.empty()) {
      const auto direct = quader_editor_core::pick_all(objects, request);
      EXPECT_EQ(direct.size(), expected.has_value() ? 1U : 0U);
      if (!direct.empty() && expected.has_value()) {
        expect_same_component_hit(direct.front(), *expected);
      }
    }
  }
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
	HiddenMaterialSlotSelectionInvertPreservesHiddenEdges) {
	quader_editor_core::PolygonObject object = adjacent_face_object();
	quader_poly::Selection& selection = object.selection;
	selection.mode = quader_poly::SelectionMode::Edge;

	const quader_poly::PickOptions options =
		hidden_slot_pick_options(quader_poly::SelectionMode::Edge);
	quader_poly::DocumentPickQuery query(object.document, options);
	quader_poly::ComponentSelectionUniverse eligible;
	eligible.mode = quader_poly::SelectionMode::Edge;
	std::vector<quader_poly::Edge> hidden_only;
	for (const quader_poly::Edge edge : query.edges()) {
		(query.edge_can_be_picked(edge) ? eligible.edges : hidden_only)
			.push_back(edge);
	}
	ASSERT_EQ(eligible.edges.size(), 4U);
	ASSERT_EQ(hidden_only.size(), 3U);

	selection.edges = { eligible.edges[0], hidden_only[0] };
	selection.has_active = true;
	selection.active_kind = quader_poly::ElementKind::Edge;
	selection.active_edge = hidden_only[0];
	const quader_poly::Selection initial = selection;

	ASSERT_TRUE(quader_poly::invert_component_selection(
		object.document, selection, eligible));
	EXPECT_EQ(selection.mode, quader_poly::SelectionMode::Edge);
	EXPECT_TRUE(selection.has_active);
	EXPECT_EQ(selection.active_kind, quader_poly::ElementKind::Edge);
	EXPECT_EQ(selection.active_edge, hidden_only[0]);
	EXPECT_TRUE(quader_poly::selection_contains(selection, hidden_only[0]));
	EXPECT_FALSE(quader_poly::selection_contains(selection, hidden_only[1]));
	EXPECT_FALSE(quader_poly::selection_contains(selection, hidden_only[2]));
	EXPECT_FALSE(quader_poly::selection_contains(selection, eligible.edges[0]));
	EXPECT_TRUE(quader_poly::selection_contains(selection, eligible.edges[1]));
	EXPECT_TRUE(quader_poly::selection_contains(selection, eligible.edges[2]));
	EXPECT_TRUE(quader_poly::selection_contains(selection, eligible.edges[3]));
	ASSERT_EQ(selection.edges.size(), 4U);

	ASSERT_TRUE(quader_poly::invert_component_selection(
		object.document, selection, eligible));
	EXPECT_EQ(selection.edges, initial.edges);
	EXPECT_EQ(selection.has_active, initial.has_active);
	EXPECT_EQ(selection.active_kind, initial.active_kind);
	EXPECT_EQ(selection.active_edge, initial.active_edge);
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

TEST(QdrModelingRuntimePickingTests,
     MoveSnapScratchRetainsCapacityAndSelectionSemantics) {
  std::vector objects{cube_object(11), cube_object(22)};
  objects.front().selected = true;
  constexpr double kVertexRadius = 2.0;
  const quader_poly::PickOptions options =
      quader_editor_core::move_vertex_snap_pick_options(kVertexRadius);
  std::vector<quader_editor_core::PickObjectBounds> bounds;
  bounds.reserve(objects.size());
  for (const auto &object : objects) {
    bounds.push_back(pick_bounds_for_object(object));
  }
  const auto face_geometry =
      quader_editor_core::build_pick_face_geometry(objects, options);

  const auto baseline = quader_editor_core::resolve_move_vertex_snap_query(
      objects, quader_editor_core::SelectionMode::Object, front_center_ray(),
      kVertexRadius, bounds, face_geometry, {}, true);
  quader_editor_core::PickAllWithSurfaceScratch scratch;
  const auto first = quader_editor_core::resolve_move_vertex_snap_query(
      objects, quader_editor_core::SelectionMode::Object, front_center_ray(),
      kVertexRadius, bounds, face_geometry, {}, true, &scratch);

  ASSERT_TRUE(baseline.vertex_target.has_value());
  ASSERT_TRUE(first.vertex_target.has_value());
  ASSERT_TRUE(baseline.nearest_surface_ray_distance.has_value());
  ASSERT_TRUE(first.nearest_surface_ray_distance.has_value());
  EXPECT_EQ(first.vertex_target->object_id, 22);
  EXPECT_EQ(first.vertex_target->object_id, baseline.vertex_target->object_id);
  EXPECT_EQ(first.vertex_target->vertex_id, baseline.vertex_target->vertex_id);
  EXPECT_FLOAT_EQ(first.vertex_target->position.x,
                  baseline.vertex_target->position.x);
  EXPECT_FLOAT_EQ(first.vertex_target->position.y,
                  baseline.vertex_target->position.y);
  EXPECT_FLOAT_EQ(first.vertex_target->position.z,
                  baseline.vertex_target->position.z);
  EXPECT_DOUBLE_EQ(first.vertex_target->ray_distance,
                   baseline.vertex_target->ray_distance);
  EXPECT_DOUBLE_EQ(first.vertex_target->pick_distance,
                   baseline.vertex_target->pick_distance);
  EXPECT_DOUBLE_EQ(*first.nearest_surface_ray_distance,
                   *baseline.nearest_surface_ray_distance);

  ASSERT_FALSE(scratch.local_surface_hits.empty());
  ASSERT_FALSE(scratch.hits.empty());
  const auto *surface_storage = scratch.local_surface_hits.data();
  const auto *hit_storage = scratch.hits.data();
  const std::size_t surface_capacity = scratch.local_surface_hits.capacity();
  const std::size_t hit_capacity = scratch.hits.capacity();
  const auto second = quader_editor_core::resolve_move_vertex_snap_query(
      objects, quader_editor_core::SelectionMode::Object, front_center_ray(),
      kVertexRadius, bounds, face_geometry, {}, true, &scratch);

  ASSERT_TRUE(second.vertex_target.has_value());
  EXPECT_EQ(second.vertex_target->object_id, first.vertex_target->object_id);
  EXPECT_EQ(scratch.local_surface_hits.data(), surface_storage);
  EXPECT_EQ(scratch.hits.data(), hit_storage);
  EXPECT_EQ(scratch.local_surface_hits.capacity(), surface_capacity);
  EXPECT_EQ(scratch.hits.capacity(), hit_capacity);
}

TEST(QdrModelingRuntimePickingTests,
     BoundedMoveVertexSnapFallsThroughToVisibleVertexOnSameObject) {
  quader_editor_core::PolygonObject object;
  object.id = 77;
  const quader_poly::ElementId visible_vertex = quader_poly::add_vertex(
      object.document, {0.025F, 0.0F, 4.0F});
  const quader_poly::ElementId occluded_vertex = quader_poly::add_vertex(
      object.document, {0.0F, 0.0F, 0.0F});
  const std::vector objects{object};

  // The authored Z distances are scaled by 0.25 into world space. This makes
  // the visible vertex (0.05, 0, 1) sit farther from the screen ray than the
  // occluded vertex (0, 0, 0), while remaining in front of a world-depth
  // barrier at ray distance 4.5. The barrier must be converted to the local
  // ray parameter before the document picker enumerates this object's points.
  const std::array transforms{
      scaled_pick_transform(object.id, 2.0F, 3.0F, 0.25F)};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = object.id,
      .empty = false,
      .min = {0.0F, 0.0F, 0.0F},
      .max = {0.05F, 0.0F, 1.0F},
  }};
  constexpr double kVertexRadius = 0.1;
  const quader_poly::Ray ray{{0.0F, 0.0F, 5.0F},
                             {0.0F, 0.0F, -1.0F}};
  quader_editor_core::PickAllWithSurfaceScratch scratch;

  const auto raw = quader_editor_core::resolve_move_vertex_snap_candidate(
      objects, quader_editor_core::SelectionMode::Object, ray, kVertexRadius,
      bounds, transforms, false, std::nullopt, &scratch);
  ASSERT_TRUE(raw.has_value());
  EXPECT_EQ(raw->object_id, object.id);
  EXPECT_EQ(raw->vertex_id, occluded_vertex);
  EXPECT_DOUBLE_EQ(raw->ray_distance, 5.0);
  EXPECT_DOUBLE_EQ(raw->pick_distance, 0.0);
  ASSERT_EQ(scratch.hits.size(), 1U);
  EXPECT_TRUE(scratch.local_surface_hits.empty());
  const auto *hit_storage = scratch.hits.data();
  const std::size_t hit_capacity = scratch.hits.capacity();

  const auto bounded = quader_editor_core::resolve_move_vertex_snap_candidate(
      objects, quader_editor_core::SelectionMode::Object, ray, kVertexRadius,
      bounds, transforms, false, 4.5, &scratch);
  ASSERT_TRUE(bounded.has_value());
  EXPECT_EQ(bounded->object_id, raw->object_id);
  EXPECT_EQ(bounded->vertex_id, visible_vertex);
  EXPECT_FLOAT_EQ(bounded->position.x, 0.05F);
  EXPECT_FLOAT_EQ(bounded->position.y, 0.0F);
  EXPECT_FLOAT_EQ(bounded->position.z, 1.0F);
  EXPECT_DOUBLE_EQ(bounded->ray_distance, 4.0);
  EXPECT_NEAR(bounded->pick_distance, 0.05, 0.000001);
  EXPECT_GT(bounded->pick_distance, raw->pick_distance);
  EXPECT_EQ(scratch.hits.data(), hit_storage);
  EXPECT_EQ(scratch.hits.capacity(), hit_capacity);
  EXPECT_TRUE(scratch.local_surface_hits.empty());

  const auto repeated = quader_editor_core::resolve_move_vertex_snap_candidate(
      objects, quader_editor_core::SelectionMode::Object, ray, kVertexRadius,
      bounds, transforms, false, 4.5, &scratch);
  ASSERT_TRUE(repeated.has_value());
  EXPECT_EQ(repeated->vertex_id, bounded->vertex_id);
  EXPECT_EQ(scratch.hits.data(), hit_storage);
  EXPECT_EQ(scratch.hits.capacity(), hit_capacity);
  EXPECT_TRUE(scratch.local_surface_hits.empty());
}

TEST(QdrModelingRuntimePickingTests,
     ObjectClickAndHoverRespectCulledNearFaces) {
  auto front = single_quad_object(8, true);
  for (auto &vertex : front.document.edit().vertices) {
    vertex.position.z = 2.0F;
  }
  const std::vector objects{front, single_quad_object(7, false)};
  for (const bool select_backfaces : std::array{false, true}) {
    const auto options = quader_editor_core::selection_pick_options_from_radii(
        0.05, 0.05, select_backfaces);
    const int expected = select_backfaces ? 8 : 7;
    const auto hover = quader_editor_core::update_selection_hover(
        objects, quader_editor_core::SelectionMode::Object, {},
        front_center_ray(), false, options);
    EXPECT_EQ(hover.hover.object_id, expected);
    quader_editor_core::EditorSession session(objects);
    const auto selected = quader_editor_core::apply_pick_selection(
        session, session.objects(),
        {.ray = front_center_ray(), .pick_options = options});
    ASSERT_TRUE(selected.hit);
    EXPECT_EQ(session.active_object_id(), expected);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ObjectDepthCycleSkipsFrontThenWrapsAcrossOrdinaryClicks) {
  quader_editor_core::EditorSession session(
      {cube_object(7), cube_object(8, {0.0F, 0.0F, -4.0F}),
       cube_object(9, {0.0F, 0.0F, -8.0F})});
  quader_editor_core::PickCycleState cycle;
  quader_editor_core::ApplyPickSelectionRequest request{
      .ray = front_center_ray(),
      .pick_options =
          quader_editor_core::selection_pick_options_from_radii(0.05, 0.05, false),
      .click_count = 2,
      .cycle_depth = true,
      .depth_cycle = &cycle,
  };
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .hit);
  EXPECT_EQ(session.active_object_id(), 8);
  for (const int expected : std::array{9, 7, 8}) {
    request.click_count = 1;
    request.cycle_depth = false;
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request)
                    .hit);
    EXPECT_EQ(session.active_object_id(), 7);
    request.click_count = 2;
    request.cycle_depth = true;
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request)
                    .hit);
    EXPECT_EQ(session.active_object_id(), expected);
    EXPECT_EQ(session.selected_object_ids().size(), 1U);
  }
  const auto revision = session.state().selection_revision;
  request.ray = miss_ray();
  const auto missed = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  EXPECT_FALSE(missed.hit);
  EXPECT_FALSE(missed.changed);
  EXPECT_EQ(session.active_object_id(), 8);
  EXPECT_EQ(session.state().selection_revision, revision);
}

TEST(QdrModelingRuntimePickingTests,
     FreshObjectDepthReplaceIgnoresPreviousPrimary) {
  for (const int previous_primary : std::array{8, 9}) {
    SCOPED_TRACE(previous_primary);
    quader_editor_core::EditorSession session(
        {cube_object(7), cube_object(8, {0.0F, 0.0F, -4.0F}),
         cube_object(9, {0.0F, 0.0F, -8.0F})});
    session.set_selected_object_ids({8, 9}, previous_primary);
    quader_editor_core::PickCycleState cycle;
    quader_editor_core::ApplyPickSelectionRequest request{
        .ray = front_center_ray(),
        .pick_options = quader_editor_core::selection_pick_options_from_radii(
            0.05, 0.05, false),
        .click_count = 2,
        .cycle_depth = true,
        .depth_cycle = &cycle,
    };
    // No front selection is applied first: a deferred single click leaves the
    // previous primary intact until this depth request chooses its own target.
    for (const int expected : std::array{8, 9, 7, 8}) {
      ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                      session, session.objects(), request)
                      .hit);
      EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{expected}));
      EXPECT_EQ(session.active_object_id(), expected);
    }

    request.click_count = 1;
    request.cycle_depth = false;
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request)
                    .hit);
    EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{7}));
  }
}

TEST(QdrModelingRuntimePickingTests,
     ObjectDepthAddPreservesFrontAndUnrelatedSelection) {
  for (const bool front_selected : std::array{false, true}) {
    SCOPED_TRACE(front_selected);
    quader_editor_core::EditorSession session(
        {cube_object(7), cube_object(8, {0.0F, 0.0F, -4.0F}),
         cube_object(9, {0.0F, 0.0F, -8.0F}),
         cube_object(10, {8.0F, 0.0F, 0.0F})});
    std::vector<int> selected{9, 10};
    if (front_selected) {
      selected.insert(selected.begin(), 7);
    }
    // An already active deeper mesh must not move a fresh Add past the first
    // mesh behind the ordinary surface.
    session.set_selected_object_ids(selected, 9);
    quader_editor_core::PickCycleState cycle;
    quader_editor_core::ApplyPickSelectionRequest request{
        .operation = quader_editor_core::SelectionEditOperation::Add,
        .ray = front_center_ray(),
        .pick_options = quader_editor_core::selection_pick_options_from_radii(
            0.05, 0.05, false),
        .click_count = 2,
        .cycle_depth = true,
        .depth_cycle = &cycle,
    };
    const auto added = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    ASSERT_TRUE(added.hit);
    EXPECT_TRUE(added.changed);
    std::vector<int> expected{8, 9, 10};
    if (front_selected) {
      expected.insert(expected.begin(), 7);
    }
    EXPECT_EQ(session.selected_object_ids(), expected);
    EXPECT_EQ(session.active_object_id(), 8);

    // A fresh Add choosing the same active target is an actual no-op.
    request.depth_cycle = nullptr;
    const auto revision = session.state().selection_revision;
    const auto repeated = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    EXPECT_TRUE(repeated.hit);
    EXPECT_FALSE(repeated.changed);
    EXPECT_EQ(session.selected_object_ids(), expected);
    EXPECT_EQ(session.active_object_id(), 8);
    EXPECT_EQ(session.state().selection_revision, revision);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ObjectDepthRemoveTraversesWithoutRemovingOtherTargets) {
  quader_editor_core::EditorSession session(
      {cube_object(7), cube_object(8, {0.0F, 0.0F, -4.0F}),
       cube_object(9, {0.0F, 0.0F, -8.0F}),
       cube_object(10, {8.0F, 0.0F, 0.0F})});
  session.set_selected_object_ids({7, 8, 9, 10}, 8);
  quader_editor_core::PickCycleState cycle;
  quader_editor_core::ApplyPickSelectionRequest request{
      .operation = quader_editor_core::SelectionEditOperation::Remove,
      .ray = front_center_ray(),
      .pick_options = quader_editor_core::selection_pick_options_from_radii(
          0.05, 0.05, false),
      .click_count = 2,
      .cycle_depth = true,
      .depth_cycle = &cycle,
  };
  const auto first = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  ASSERT_TRUE(first.hit);
  EXPECT_TRUE(first.changed);
  EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{7, 9, 10}));
  EXPECT_EQ(session.active_object_id(), 7);

  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .changed);
  EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{7, 10}));
  EXPECT_EQ(session.active_object_id(), 7);
  // The front target is removed only once traversal explicitly wraps to it.
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .changed);
  EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{10}));
  EXPECT_EQ(session.active_object_id(), 10);

  const auto revision = session.state().selection_revision;
  const auto already_removed = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  EXPECT_TRUE(already_removed.hit);
  EXPECT_FALSE(already_removed.changed);
  EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{10}));
  EXPECT_EQ(session.active_object_id(), 10);
  EXPECT_EQ(session.state().selection_revision, revision);
}

TEST(QdrModelingRuntimePickingTests,
     ObjectDepthEditTransitionsRestartAtOrdinarySurface) {
  quader_editor_core::EditorSession session(
      {cube_object(7), cube_object(8, {0.0F, 0.0F, -4.0F}),
       cube_object(9, {0.0F, 0.0F, -8.0F})});
  quader_editor_core::PickCycleState cycle;
  quader_editor_core::ApplyPickSelectionRequest request{
      .ray = front_center_ray(),
      .pick_options = quader_editor_core::selection_pick_options_from_radii(
          0.05, 0.05, false),
      .click_count = 2,
      .cycle_depth = true,
      .depth_cycle = &cycle,
  };
  for (const int expected : std::array{8, 9}) {
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request)
                    .hit);
    EXPECT_EQ(session.active_object_id(), expected);
  }

  request.operation = quader_editor_core::SelectionEditOperation::Add;
  for (const int expected : std::array{8, 9}) {
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request)
                    .hit);
    EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{8, 9}));
    EXPECT_EQ(session.active_object_id(), expected);
  }

  request.operation = quader_editor_core::SelectionEditOperation::Remove;
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .hit);
  EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{9}));
  EXPECT_EQ(session.active_object_id(), 9);
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .hit);
  EXPECT_TRUE(session.selected_object_ids().empty());
  EXPECT_EQ(session.active_object_id(), -1);
}

TEST(QdrModelingRuntimePickingTests,
     ObjectDepthSingleHitAndMissPreserveNoopRevisions) {
  for (const auto operation :
       std::array{quader_editor_core::SelectionEditOperation::Add,
                  quader_editor_core::SelectionEditOperation::Remove}) {
    SCOPED_TRACE(static_cast<int>(operation));
    quader_editor_core::EditorSession session(
        {cube_object(7), cube_object(10, {8.0F, 0.0F, 0.0F})});
    const bool adding =
        operation == quader_editor_core::SelectionEditOperation::Add;
    const std::vector<int> selected =
        adding ? std::vector<int>{7, 10} : std::vector<int>{10};
    const int active = adding ? 7 : 10;
    session.set_selected_object_ids(selected, active);
    const auto revision = session.state().selection_revision;
    quader_editor_core::PickCycleState cycle;
    quader_editor_core::ApplyPickSelectionRequest request{
        .operation = operation,
        .ray = front_center_ray(),
        .pick_options = quader_editor_core::selection_pick_options_from_radii(
            0.05, 0.05, false),
        .click_count = 2,
        .cycle_depth = true,
        .depth_cycle = &cycle,
    };
    const auto single_hit = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    EXPECT_TRUE(single_hit.hit);
    EXPECT_FALSE(single_hit.changed);
    request.ray = miss_ray();
    const auto missed = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    EXPECT_FALSE(missed.hit);
    EXPECT_FALSE(missed.applied);
    EXPECT_FALSE(missed.changed);
    EXPECT_EQ(session.selected_object_ids(), selected);
    EXPECT_EQ(session.active_object_id(), active);
    EXPECT_EQ(session.state().selection_revision, revision);
  }
}

TEST(QdrModelingRuntimePickingTests,
     OrdinaryObjectRemovalReassignsPrimaryAndPreservesOtherTargets) {
  quader_editor_core::EditorSession session(
      {cube_object(7), cube_object(8, {4.0F, 0.0F, 0.0F}),
       cube_object(9, {8.0F, 0.0F, 0.0F})});
  session.set_selected_object_ids({7, 8}, 7);
  quader_editor_core::ApplyPickSelectionRequest request{
      .operation = quader_editor_core::SelectionEditOperation::Remove,
      .ray = front_center_ray(),
  };
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .changed);
  EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{8}));
  EXPECT_EQ(session.active_object_id(), 8);
  const auto revision = session.state().selection_revision;
  const auto repeated = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  EXPECT_TRUE(repeated.hit);
  EXPECT_FALSE(repeated.changed);
  EXPECT_EQ(session.active_object_id(), 8);
  EXPECT_EQ(session.state().selection_revision, revision);

  request.ray.origin.x = 4.0F;
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .changed);
  EXPECT_TRUE(session.selected_object_ids().empty());
  EXPECT_EQ(session.active_object_id(), -1);
}

TEST(QdrModelingRuntimePickingTests,
     ComponentDoubleClickSelectsOnlyClickedLoosePartAndKeepsSeedActive) {
  const auto fixture = loose_part_fixture();
  for (const auto mode : std::array{quader_editor_core::SelectionMode::Vertex,
                                    quader_editor_core::SelectionMode::Edge,
                                    quader_editor_core::SelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader_editor_core::EditorSession session({fixture.object});
    session.set_selection_mode(mode);
    quader_editor_core::ApplyPickSelectionRequest request{
        .ray = loose_part_ray(mode),
        .pick_options = quader_editor_core::selection_pick_options_from_radii(
            0.05, 0.05, false),
    };
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).hit);
    const auto seed_selection = session.objects().front().selection;
    request.click_count = 2;
    request.preserve_existing_component_selection_on_hit = true;
    request.preserve_selection_on_selected_hit = true;
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).changed);
    const auto &selection = session.objects().front().selection;
    expect_loose_part_selection(selection, mode, fixture, 0);
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, seed_selection.active_kind);
    if (mode == quader_editor_core::SelectionMode::Vertex) {
      EXPECT_EQ(selection.active_vertex, seed_selection.active_vertex);
    } else if (mode == quader_editor_core::SelectionMode::Edge) {
      EXPECT_EQ(selection.active_edge, seed_selection.active_edge);
    } else {
      EXPECT_EQ(selection.active_face, seed_selection.active_face);
    }
    EXPECT_TRUE(session.objects().front().component_edit_target);
    EXPECT_EQ(session.active_object_id(), fixture.object.id);

    const auto revision = session.state().selection_revision;
    const auto repeated = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    EXPECT_TRUE(repeated.hit);
    EXPECT_FALSE(repeated.changed);
    EXPECT_EQ(session.state().selection_revision, revision);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ComponentDoubleClickAddsAndRemovesOnlyClickedLoosePart) {
  const auto fixture = loose_part_fixture();
  for (const auto mode : std::array{quader_editor_core::SelectionMode::Vertex,
                                    quader_editor_core::SelectionMode::Edge,
                                    quader_editor_core::SelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader_editor_core::EditorSession session({fixture.object});
    session.set_selection_mode(mode);
    quader_editor_core::ApplyPickSelectionRequest request{
        .ray = loose_part_ray(mode),
        .pick_options = quader_editor_core::selection_pick_options_from_radii(
            0.05, 0.05, false),
        .click_count = 2,
    };
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).hit);
    request.operation = quader_editor_core::SelectionEditOperation::Add;
    request.ray = loose_part_ray(mode, 1);
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).changed);
    const auto &selection = session.objects().front().selection;
    if (mode == quader_editor_core::SelectionMode::Vertex) {
      EXPECT_EQ(selection.vertices.size(), 12U);
    } else if (mode == quader_editor_core::SelectionMode::Edge) {
      EXPECT_EQ(selection.edges.size(), 4U);
    } else {
      EXPECT_EQ(selection.faces.size(), 4U);
    }
    request.operation = quader_editor_core::SelectionEditOperation::Remove;
    request.ray = loose_part_ray(mode);
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).changed);
    expect_loose_part_selection(session.objects().front().selection, mode, fixture, 1);
    request.ray = loose_part_ray(mode, 1);
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).changed);
    EXPECT_TRUE(session.objects().front().selection.empty());
    EXPECT_FALSE(session.objects().front().selection.has_active);
    EXPECT_TRUE(session.objects().front().component_edit_target);
    EXPECT_EQ(session.active_object_id(), fixture.object.id);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ComponentDoubleClickPreservesHiddenMaterialFiltering) {
  for (const auto mode : std::array{quader_editor_core::SelectionMode::Vertex,
                                    quader_editor_core::SelectionMode::Edge,
                                    quader_editor_core::SelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader_editor_core::EditorSession session({adjacent_face_object()});
    session.set_selection_mode(mode);
    auto options = hidden_slot_pick_options(quader_poly::SelectionMode::Vertex);
    options.vertex_radius = 0.05F;
    options.edge_radius = 0.05F;
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(),
                    {.ray = loose_part_ray(mode),
                     .pick_options = options,
                     .click_count = 2}).hit);
    const auto &selection = session.objects().front().selection;
    if (mode == quader_editor_core::SelectionMode::Vertex) {
      EXPECT_EQ(selection.vertices.size(), 4U);
    } else if (mode == quader_editor_core::SelectionMode::Edge) {
      EXPECT_EQ(selection.edges.size(), 1U);
    } else {
      EXPECT_EQ(selection.faces.size(), 1U);
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     EdgeDoubleClickBoundsSeparatedSeedsAndKeepsUnrelatedEdges) {
  const auto fixture = component_traversal_grid();
  const auto edge = [&](std::size_t column) {
    return quader_poly::make_edge(fixture.vertices[2][column],
                                  fixture.vertices[2][column + 1u]);
  };
  const auto unrelated = quader_poly::make_edge(fixture.vertices[0][0], fixture.vertices[1][0]);
  for (const auto operation : {quader_editor_core::SelectionEditOperation::Replace,
                               quader_editor_core::SelectionEditOperation::Add,
                               quader_editor_core::SelectionEditOperation::Remove}) {
    SCOPED_TRACE(static_cast<int>(operation));
    quader_editor_core::EditorSession session({fixture.object});
    std::vector prior{unrelated, edge(0)};
    if (operation == quader_editor_core::SelectionEditOperation::Remove) prior.push_back(edge(2));
    session.set_component_selection(fixture.object.id,
        {.mode = quader_poly::SelectionMode::Edge, .edges = prior,
         .has_active = true, .active_kind = quader_poly::ElementKind::Edge,
         .active_edge = edge(0)}, quader_editor_core::SelectionMode::Edge);
    const auto content_revision = session.state().content_revision;
    const auto selected = quader_editor_core::apply_pick_selection(
        session, session.objects(),
        {.operation = operation, .ray = component_traversal_ray(2.5F, 2.0F),
         .pick_options = quader_editor_core::selection_pick_options_from_radii(0.03, 0.03, false),
         .click_count = 2});
    ASSERT_TRUE(selected.hit);
    EXPECT_TRUE(selected.changed);
    std::vector<quader_poly::Edge> expected;
    if (operation != quader_editor_core::SelectionEditOperation::Replace) expected.push_back(unrelated);
    if (operation != quader_editor_core::SelectionEditOperation::Remove) {
      for (std::size_t column = 0;
           column < (operation == quader_editor_core::SelectionEditOperation::Replace ? 4u : 3u);
           ++column) expected.push_back(edge(column));
    }
    const auto &selection = session.objects().front().selection;
    EXPECT_TRUE(std::is_permutation(selection.edges.begin(), selection.edges.end(),
                                    expected.begin(), expected.end()));
    EXPECT_TRUE(selection.vertices.empty());
    EXPECT_TRUE(selection.faces.empty());
    EXPECT_TRUE(session.objects().front().component_edit_target);
    EXPECT_EQ(session.active_object_id(), fixture.object.id);
    EXPECT_EQ(session.state().content_revision, content_revision);
  }
}

TEST(QdrModelingRuntimePickingTests,
     FaceDoubleClickUsesAdjacentActiveFaceOrFallsBackToContiguousFaces) {
  const auto fixture = component_traversal_grid();
  for (const int direction : {-1, 0, 1}) {
    SCOPED_TRACE(direction);
    quader_editor_core::EditorSession session({fixture.object});
    session.set_selection_mode(quader_editor_core::SelectionMode::Face);
    if (direction >= 0) {
      const auto active = direction == 0 ? fixture.faces[2][0] : fixture.faces[1][1];
      session.set_component_selection(fixture.object.id,
          {.mode = quader_poly::SelectionMode::Face, .faces = {active},
           .has_active = true, .active_kind = quader_poly::ElementKind::Face,
           .active_face = active}, quader_editor_core::SelectionMode::Face);
    }
    const auto selected = quader_editor_core::apply_pick_selection(
        session, session.objects(),
        {.ray = component_traversal_ray(1.5F, 2.5F),
         .pick_options = quader_editor_core::selection_pick_options_from_radii(0.03, 0.03, false),
         .click_count = 2});
    ASSERT_TRUE(selected.hit);
    EXPECT_TRUE(selected.changed);
    std::vector<quader_poly::ElementId> expected;
    if (direction < 0) {
      for (const auto &row : fixture.faces) expected.insert(expected.end(), row.begin(), row.end());
    } else {
      for (std::size_t index = 0; index < fixture.faces.size(); ++index)
        expected.push_back(direction == 0 ? fixture.faces[2][index] : fixture.faces[index][1]);
    }
    const auto &selection = session.objects().front().selection;
    EXPECT_TRUE(std::is_permutation(selection.faces.begin(), selection.faces.end(),
                                    expected.begin(), expected.end()));
    EXPECT_EQ(selection.active_face, fixture.faces[2][1]);
    EXPECT_TRUE(session.objects().front().component_edit_target);
    EXPECT_EQ(session.active_object_id(), fixture.object.id);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ComponentDoubleClickMissRespectsPreservationAndModifierPolicy) {
  const auto fixture = loose_part_fixture();
  for (const auto mode : std::array{quader_editor_core::SelectionMode::Vertex,
                                    quader_editor_core::SelectionMode::Edge,
                                    quader_editor_core::SelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader_editor_core::EditorSession session({fixture.object});
    session.set_selection_mode(mode);
    quader_editor_core::ApplyPickSelectionRequest request{
        .ray = loose_part_ray(mode),
        .click_count = 2,
    };
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request).hit);
    request.ray = miss_ray();
    request.preserve_selection_on_miss = true;
    const auto revision = session.state().selection_revision;
    const auto preserved = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    EXPECT_FALSE(preserved.hit);
    EXPECT_FALSE(preserved.changed);
    expect_loose_part_selection(session.objects().front().selection, mode, fixture, 0);
    request.preserve_selection_on_miss = false;
    for (const auto operation : std::array{quader_editor_core::SelectionEditOperation::Add,
                                          quader_editor_core::SelectionEditOperation::Remove}) {
      request.operation = operation;
      const auto missed = quader_editor_core::apply_pick_selection(
          session, session.objects(), request);
      EXPECT_FALSE(missed.hit);
      EXPECT_FALSE(missed.changed);
    }
    EXPECT_EQ(session.state().selection_revision, revision);
  }
}

TEST(QdrModelingRuntimePickingTests,
     DepthFlagDoesNotReplaceOrdinaryFaceDoubleClick) {
  quader_editor_core::EditorSession session({cube_object(7)});
  session.set_selection_mode(quader_editor_core::SelectionMode::Face);
  const auto result = quader_editor_core::apply_pick_selection(
      session, session.objects(),
      {.ray = front_center_ray(),
       .pick_options =
           quader_editor_core::selection_pick_options_from_radii(0.05, 0.05, false),
       .click_count = 2,
       .cycle_depth = true});
  ASSERT_TRUE(result.hit);
  EXPECT_EQ(session.objects().front().selection.faces.size(), 6U);
}

TEST(QdrModelingRuntimePickingTests,
     EdgeRingTraversalSelectsFourParallelCubeEdges) {
  quader_editor_core::EditorSession session({cube_object(7)});
  session.set_selection_mode(quader_editor_core::SelectionMode::Edge);
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  options.mode = quader_poly::SelectionMode::Edge;

  const auto selected = quader_editor_core::apply_pick_selection(
      session, session.objects(),
      {.ray = {{0.0F, -1.0F, 5.0F}, {0.0F, 0.0F, -1.0F}},
       .pick_options = options,
       .click_count = 2,
       .edge_traversal = quader_editor_core::EdgeSelectionTraversal::Ring});

  ASSERT_TRUE(selected.hit);
  EXPECT_TRUE(selected.changed);
  ASSERT_EQ(session.objects().front().selection.edges.size(), 4U);
  for (const quader_poly::Edge edge :
       session.objects().front().selection.edges) {
    const quader_poly::Vertex *a =
        quader_poly::find_vertex(session.objects().front().document, edge.a);
    const quader_poly::Vertex *b =
        quader_poly::find_vertex(session.objects().front().document, edge.b);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_GT(std::abs(a->position.x - b->position.x), 1.9F);
    EXPECT_FLOAT_EQ(a->position.y, b->position.y);
    EXPECT_FLOAT_EQ(a->position.z, b->position.z);
  }
}

TEST(QdrModelingRuntimePickingTests,
     EdgeRingToggleUsesClickedSeedForWholeRing) {
  quader_editor_core::EditorSession session({cube_object(7)});
  session.set_selection_mode(quader_editor_core::SelectionMode::Edge);
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  options.mode = quader_poly::SelectionMode::Edge;
  quader_editor_core::ApplyPickSelectionRequest request{
      .ray = {{0.0F, -1.0F, 5.0F}, {0.0F, 0.0F, -1.0F}},
      .pick_options = options,
      .edge_traversal = quader_editor_core::EdgeSelectionTraversal::Ring,
  };
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .hit);
  const std::vector<quader_poly::Edge> expected_ring =
      session.objects().front().selection.edges;
  ASSERT_EQ(expected_ring.size(), 4U);

  request.operation = quader_editor_core::SelectionEditOperation::Toggle;
  const auto removed = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  ASSERT_TRUE(removed.hit);
  EXPECT_TRUE(removed.changed);
  EXPECT_TRUE(session.objects().front().selection.edges.empty());

  const auto added = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  ASSERT_TRUE(added.hit);
  EXPECT_TRUE(added.changed);
  const auto &selected_edges = session.objects().front().selection.edges;
  ASSERT_EQ(selected_edges.size(), expected_ring.size());
  EXPECT_TRUE(std::is_permutation(selected_edges.begin(), selected_edges.end(),
                                  expected_ring.begin(), expected_ring.end()));
}

TEST(QdrModelingRuntimePickingTests,
     EdgeRingTraversalMissPreservesExistingSelection) {
  quader_editor_core::EditorSession session({cube_object(7)});
  session.set_selection_mode(quader_editor_core::SelectionMode::Edge);
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  options.mode = quader_poly::SelectionMode::Edge;
  quader_editor_core::ApplyPickSelectionRequest request{
      .ray = {{0.0F, -1.0F, 5.0F}, {0.0F, 0.0F, -1.0F}},
      .pick_options = options,
      .edge_traversal = quader_editor_core::EdgeSelectionTraversal::Ring,
  };
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request)
                  .hit);
  const std::vector<quader_poly::Edge> expected_ring =
      session.objects().front().selection.edges;
  ASSERT_EQ(expected_ring.size(), 4U);
  const std::uint64_t selection_revision =
      session.state().selection_revision;

  request.ray = miss_ray();
  const auto missed = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  EXPECT_FALSE(missed.hit);
  EXPECT_FALSE(missed.changed);
  EXPECT_EQ(session.objects().front().selection.edges, expected_ring);
  EXPECT_EQ(session.state().selection_revision, selection_revision);
}

TEST(QdrModelingRuntimePickingTests,
     TypedVertexLoopAndRingRetainClickedVertexAndIncidentDirection) {
  const auto fixture = component_traversal_grid();
  const auto ray = component_traversal_ray(2.025F, 2.0F);
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  options.mode = quader_poly::SelectionMode::Vertex;
  const auto hint = quader_editor_core::pick_nearest(
      {fixture.object}, {.ray = ray, .options = options,
                         .resolve_component_loop_hint = true});
  ASSERT_TRUE(hint.has_value());
  EXPECT_EQ(hint->kind, quader_poly::ElementKind::Vertex);
  EXPECT_EQ(hint->vertex_id, fixture.vertices[2][2]);
  EXPECT_EQ(hint->edge, quader_poly::make_edge(fixture.vertices[2][2],
                                             fixture.vertices[2][3]));

  for (const auto traversal : std::array{
           quader_editor_core::ComponentSelectionTraversal::VertexLoop,
           quader_editor_core::ComponentSelectionTraversal::VertexRing}) {
    SCOPED_TRACE(static_cast<int>(traversal));
    quader_editor_core::EditorSession session({fixture.object});
    session.set_selection_mode(quader_editor_core::SelectionMode::Vertex);
    const auto content_revision = session.state().content_revision;
    const quader_editor_core::ApplyPickSelectionRequest request{
        .ray = ray, .pick_options = options, .click_count = 2,
        .component_traversal = traversal};
    const auto selected = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    ASSERT_TRUE(selected.hit);
    EXPECT_TRUE(selected.changed);
    std::vector<quader_poly::ElementId> expected;
    if (traversal == quader_editor_core::ComponentSelectionTraversal::VertexLoop) {
      expected.assign(fixture.vertices[2].begin(), fixture.vertices[2].end());
    } else {
      for (const auto &row : fixture.vertices) {
        expected.push_back(row[2]);
        expected.push_back(row[3]);
      }
    }
    const auto &selection = session.objects().front().selection;
    EXPECT_TRUE(std::is_permutation(selection.vertices.begin(), selection.vertices.end(),
                                    expected.begin(), expected.end()));
    EXPECT_TRUE(selection.edges.empty());
    EXPECT_TRUE(selection.faces.empty());
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, quader_poly::ElementKind::Vertex);
    EXPECT_EQ(selection.active_vertex, fixture.vertices[2][2]);
    EXPECT_EQ(session.active_object_id(), fixture.object.id);
    EXPECT_EQ(session.state().content_revision, content_revision);
    const auto selection_revision = session.state().selection_revision;
    const auto repeated = quader_editor_core::apply_pick_selection(
        session, session.objects(), request);
    EXPECT_TRUE(repeated.hit);
    EXPECT_FALSE(repeated.changed);
    EXPECT_EQ(session.state().selection_revision, selection_revision);
  }
}

TEST(QdrModelingRuntimePickingTests,
     TypedVertexLoopUsesAdjacentActiveVertexBeforePointerDirection) {
  const auto fixture = component_traversal_grid();
  quader_editor_core::EditorSession session({fixture.object});
  session.set_component_selection(
      fixture.object.id,
      {.mode = quader_poly::SelectionMode::Vertex,
       .vertices = {fixture.vertices[1][2]}, .has_active = true,
       .active_kind = quader_poly::ElementKind::Vertex,
       .active_vertex = fixture.vertices[1][2]},
      quader_editor_core::SelectionMode::Vertex);
  const auto selected = quader_editor_core::apply_pick_selection(
      session, session.objects(),
      {.ray = component_traversal_ray(2.025F, 2.0F),
       .pick_options = quader_editor_core::selection_pick_options_from_radii(
           0.05, 0.05, false),
       .component_traversal =
           quader_editor_core::ComponentSelectionTraversal::VertexLoop});
  ASSERT_TRUE(selected.hit);
  EXPECT_TRUE(selected.changed);
  const std::array expected{
      fixture.vertices[0][2], fixture.vertices[1][2], fixture.vertices[2][2],
      fixture.vertices[3][2], fixture.vertices[4][2]};
  const auto &selection = session.objects().front().selection;
  EXPECT_TRUE(std::is_permutation(selection.vertices.begin(), selection.vertices.end(),
                                  expected.begin(), expected.end()));
  EXPECT_EQ(selection.active_vertex, fixture.vertices[2][2]);
}

TEST(QdrModelingRuntimePickingTests,
     TypedVertexRingToggleUsesClickedVertexWithOnlyOneEndpointSelected) {
  const auto fixture = component_traversal_grid();
  quader_editor_core::EditorSession session({fixture.object});
  session.set_component_selection(
      fixture.object.id,
      {.mode = quader_poly::SelectionMode::Vertex,
       .vertices = {fixture.vertices[2][2], fixture.vertices[0][0]},
       .has_active = true, .active_kind = quader_poly::ElementKind::Vertex,
       .active_vertex = fixture.vertices[0][0]},
      quader_editor_core::SelectionMode::Vertex);
  quader_editor_core::ApplyPickSelectionRequest request{
      .operation = quader_editor_core::SelectionEditOperation::Toggle,
      .ray = component_traversal_ray(2.025F, 2.0F),
      .pick_options = quader_editor_core::selection_pick_options_from_radii(
          0.05, 0.05, false),
      .component_traversal =
          quader_editor_core::ComponentSelectionTraversal::VertexRing};
  const auto removed = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  ASSERT_TRUE(removed.hit);
  EXPECT_TRUE(removed.changed);
  EXPECT_EQ(session.objects().front().selection.vertices,
            (std::vector{fixture.vertices[0][0]}));
  EXPECT_EQ(session.objects().front().selection.active_vertex, fixture.vertices[0][0]);

  const auto added = quader_editor_core::apply_pick_selection(
      session, session.objects(), request);
  ASSERT_TRUE(added.hit);
  EXPECT_TRUE(added.changed);
  std::vector expected{fixture.vertices[0][0]};
  for (const auto &row : fixture.vertices) {
    expected.push_back(row[2]);
    expected.push_back(row[3]);
  }
  const auto &selection = session.objects().front().selection;
  EXPECT_TRUE(std::is_permutation(selection.vertices.begin(), selection.vertices.end(),
                                  expected.begin(), expected.end()));
  EXPECT_EQ(selection.active_vertex, fixture.vertices[2][2]);

  request.operation = quader_editor_core::SelectionEditOperation::Remove;
  ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                  session, session.objects(), request).changed);
  EXPECT_EQ(session.objects().front().selection.vertices,
            (std::vector{fixture.vertices[0][0]}));
}

TEST(QdrModelingRuntimePickingTests,
     TypedFaceLoopAtFaceCenterUsesAdjacentSelectedFaceDirection) {
  const auto fixture = component_traversal_grid();
  for (const bool horizontal : std::array{false, true}) {
    SCOPED_TRACE(horizontal);
    quader_editor_core::EditorSession session({fixture.object});
    const auto active = horizontal ? fixture.faces[2][0] : fixture.faces[1][1];
    session.set_component_selection(
        fixture.object.id,
        {.mode = quader_poly::SelectionMode::Face, .faces = {active},
         .has_active = true, .active_kind = quader_poly::ElementKind::Face,
         .active_face = active},
        quader_editor_core::SelectionMode::Face);
    auto options = quader_editor_core::selection_pick_options_from_radii(
        0.01, 0.01, false);
    options.mode = quader_poly::SelectionMode::Face;
    const auto ray = component_traversal_ray(1.5F, 2.5F);
    const auto hint = session.pick_nearest(
        {.ray = ray, .options = options, .resolve_component_loop_hint = true});
    ASSERT_TRUE(hint.has_value());
    EXPECT_EQ(hint->kind, quader_poly::ElementKind::Face);
    EXPECT_EQ(hint->face_id, fixture.faces[2][1]);
    const auto selected = quader_editor_core::apply_pick_selection(
        session, session.objects(),
        {.ray = ray, .pick_options = options, .click_count = 2,
         .component_traversal =
             quader_editor_core::ComponentSelectionTraversal::FaceLoop});
    ASSERT_TRUE(selected.hit);
    EXPECT_TRUE(selected.changed);
    std::array<quader_poly::ElementId, 4> expected{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
      expected[index] = horizontal ? fixture.faces[2][index] : fixture.faces[index][1];
    }
    const auto &selection = session.objects().front().selection;
    EXPECT_TRUE(std::is_permutation(selection.faces.begin(), selection.faces.end(),
                                    expected.begin(), expected.end()));
    EXPECT_TRUE(selection.vertices.empty());
    EXPECT_TRUE(selection.edges.empty());
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, quader_poly::ElementKind::Face);
    EXPECT_EQ(selection.active_face, fixture.faces[2][1]);
  }
}

TEST(QdrModelingRuntimePickingTests,
     TypedComponentTraversalMissAndInvalidTopologyPreserveState) {
  auto fixture = component_traversal_grid();
  const auto isolated_vertex = quader_poly::add_vertex(
      fixture.object.document, {8.0F, 0.0F, 0.0F});
  const std::array triangle_vertices{
      quader_poly::add_vertex(fixture.object.document, {6.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(fixture.object.document, {7.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(fixture.object.document, {6.0F, 1.0F, 0.0F})};
  ASSERT_NE(quader_poly::add_face(fixture.object.document, triangle_vertices),
            quader_poly::kInvalidElementId);
  ASSERT_NE(isolated_vertex, quader_poly::kInvalidElementId);
  for (const auto traversal : std::array{
           quader_editor_core::ComponentSelectionTraversal::VertexLoop,
           quader_editor_core::ComponentSelectionTraversal::VertexRing,
           quader_editor_core::ComponentSelectionTraversal::FaceLoop}) {
    SCOPED_TRACE(static_cast<int>(traversal));
    const bool face = traversal == quader_editor_core::ComponentSelectionTraversal::FaceLoop;
    quader_editor_core::EditorSession session({fixture.object});
    quader_poly::Selection baseline;
    baseline.mode = face ? quader_poly::SelectionMode::Face : quader_poly::SelectionMode::Vertex;
    baseline.has_active = true;
    if (face) {
      baseline.faces = {fixture.faces[1][1]};
      baseline.active_kind = quader_poly::ElementKind::Face;
      baseline.active_face = fixture.faces[1][1];
    } else {
      baseline.vertices = {fixture.vertices[1][1]};
      baseline.active_kind = quader_poly::ElementKind::Vertex;
      baseline.active_vertex = fixture.vertices[1][1];
    }
    session.set_component_selection(
        fixture.object.id, baseline,
        face ? quader_editor_core::SelectionMode::Face : quader_editor_core::SelectionMode::Vertex);
    const auto selection_revision = session.state().selection_revision;
    const auto content_revision = session.state().content_revision;
    for (const auto operation : std::array{
             quader_editor_core::SelectionEditOperation::Replace,
             quader_editor_core::SelectionEditOperation::Add,
             quader_editor_core::SelectionEditOperation::Remove,
             quader_editor_core::SelectionEditOperation::Toggle}) {
      SCOPED_TRACE(static_cast<int>(operation));
      for (const auto ray : std::array{
               miss_ray(), face ? component_traversal_ray(6.25F, 0.25F)
                                : component_traversal_ray(8.0F, 0.0F)}) {
        const auto rejected = quader_editor_core::apply_pick_selection(
            session, session.objects(),
            {.operation = operation, .ray = ray,
             .pick_options = quader_editor_core::selection_pick_options_from_radii(
                 0.05, 0.05, false),
             .component_traversal = traversal});
        EXPECT_FALSE(rejected.hit);
        EXPECT_FALSE(rejected.changed);
        EXPECT_TRUE(quader_editor_core::polygon_selection_matches(
            session.objects().front().selection, baseline));
        EXPECT_EQ(session.active_object_id(), fixture.object.id);
        EXPECT_TRUE(session.objects().front().selected);
        EXPECT_TRUE(session.objects().front().component_edit_target);
        EXPECT_EQ(session.state().selection_revision, selection_revision);
        EXPECT_EQ(session.state().content_revision, content_revision);
      }
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     MeshDepthCycleIncludesEdgeOnStairAndUsesOrdinaryFaceAnchor) {
  auto room = cube_object(7);
  for (auto &vertex : room.document.edit().vertices) {
    vertex.position = vertex.position * 5.0F;
  }
  for (auto &face : room.document.edit().faces) {
    std::reverse(face.vertices.begin(), face.vertices.end());
  }
  const std::vector objects{room, side_on_stair_object(8)};
  const std::array bounds{pick_bounds_for_object(objects[0]),
                          pick_bounds_for_object(objects[1])};
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  options.mode = quader_poly::SelectionMode::Face;
  const auto face_geometry =
      quader_editor_core::build_pick_face_geometry(objects, options);
  const quader_poly::Ray ray{{-64.0F, -0.125F, 0.1875F},
                            {1.0F, 0.0F, 0.0F}};
  const auto hits = quader_editor_core::pick_mesh_depth_hits(
      objects, {.ray = ray, .options = options}, bounds, face_geometry);
  ASSERT_EQ(hits.size(), 2U);
  EXPECT_EQ(hits[0].object_id, 8);
  EXPECT_EQ(hits[0].kind, quader_poly::ElementKind::Edge);
  EXPECT_FLOAT_EQ(hits[0].ray_distance, 62.0F);
  EXPECT_EQ(hits[1].object_id, 7);
  EXPECT_EQ(hits[1].kind, quader_poly::ElementKind::Face);
  EXPECT_FLOAT_EQ(hits[1].ray_distance, 69.0F);

  for (const bool ordinary_first_click : std::array{false, true}) {
    SCOPED_TRACE(ordinary_first_click);
    quader_editor_core::EditorSession session(objects);
    ASSERT_TRUE(session.selected_object_ids().empty());
    quader_editor_core::PickCycleState cycle;
    quader_editor_core::ApplyPickSelectionRequest request{
        .ray = ray,
        .pick_options = options,
        .click_count = 2,
        .object_bounds = bounds,
        .face_geometry = face_geometry,
        .cycle_depth = true,
        .depth_cycle = &cycle,
    };
    if (ordinary_first_click) {
      request.click_count = 1;
      request.cycle_depth = false;
      ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                      session, session.objects(), request)
                      .hit);
      ASSERT_EQ(session.active_object_id(), 7);
      request.click_count = 2;
      request.cycle_depth = true;
    } else {
      // Retaining a prior stair selection while the first click is deferred
      // must not anchor a fresh cycle to the edge hit and select the room.
      session.set_selected_object_ids({8}, 8);
    }
    // A coalesced double click may arrive without a separately applied first
    // click. Its ordinary Face target is the room, after the stair in depth.
    ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                    session, session.objects(), request)
                    .hit);
    EXPECT_EQ(session.active_object_id(), 8);
    for (const int expected : std::array{7, 8}) {
      request.click_count = 1;
      request.cycle_depth = false;
      ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                      session, session.objects(), request)
                      .hit);
      ASSERT_EQ(session.active_object_id(), 7);
      request.click_count = 2;
      request.cycle_depth = true;
      ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                      session, session.objects(), request)
                      .hit);
      EXPECT_EQ(session.active_object_id(), expected);
      EXPECT_EQ(session.selected_object_ids().size(), 1U);
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     FreshEdgeOnlyDepthCycleStartsAtFirstBoundaryAndThenWraps) {
  auto farther_stair = side_on_stair_object(8);
  for (auto &vertex : farther_stair.document.edit().vertices) {
    vertex.position.x += 8.0F;
  }
  const std::vector objects{side_on_stair_object(7), farther_stair};
  for (const int previous_primary : std::array{7, 8}) {
    SCOPED_TRACE(previous_primary);
    quader_editor_core::EditorSession session(objects);
    session.set_selected_object_ids({previous_primary}, previous_primary);
    quader_editor_core::PickCycleState cycle;
    const quader_editor_core::ApplyPickSelectionRequest request{
        .ray = {{-64.0F, -0.125F, 0.1875F}, {1.0F, 0.0F, 0.0F}},
        .pick_options = quader_editor_core::selection_pick_options_from_radii(
            0.05, 0.05, false),
        .click_count = 2,
        .cycle_depth = true,
        .depth_cycle = &cycle,
    };
    for (const int expected : std::array{7, 8, 7}) {
      ASSERT_TRUE(quader_editor_core::apply_pick_selection(
                      session, session.objects(), request)
                      .hit);
      EXPECT_EQ(session.selected_object_ids(), (std::vector<int>{expected}));
      EXPECT_EQ(session.active_object_id(), expected);
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     MeshDepthFallbackRetainsBackfaceAndHiddenMaterialFilters) {
  const std::vector backfacing{single_quad_object(7, true)};
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  options.mode = quader_poly::SelectionMode::Face;
  // Outside the face, but within the authored boundary's world-space radius.
  const quader_poly::Ray boundary_ray{{0.0F, -1.02F, 5.0F},
                                     {0.0F, 0.0F, -1.0F}};
  EXPECT_TRUE(quader_editor_core::pick_mesh_depth_hits(
                  backfacing, {.ray = boundary_ray, .options = options})
                  .empty());
  options.pick_backfaces = true;
  const auto two_sided = quader_editor_core::pick_mesh_depth_hits(
      backfacing, {.ray = boundary_ray, .options = options});
  ASSERT_EQ(two_sided.size(), 1U);
  EXPECT_EQ(two_sided[0].kind, quader_poly::ElementKind::Edge);
  EXPECT_EQ(two_sided[0].face_id,
            backfacing[0].document.faces.front().id);
  options.pick_backfaces = false;
  const std::array reflected{scaled_pick_transform(7, -1.0F, 1.0F, 1.0F)};
  ASSERT_EQ(quader_editor_core::pick_mesh_depth_hits(
                backfacing, {.ray = boundary_ray, .options = options}, {}, {},
                reflected)
                .size(),
            1U);

  auto hidden_stair = side_on_stair_object(8);
  for (auto &face : hidden_stair.document.edit().faces) {
    face.material_slot = kHiddenSlot;
  }
  const std::vector hidden_objects{hidden_stair};
  const quader_poly::Ray stair_ray{{-64.0F, -0.125F, 0.1875F},
                                  {1.0F, 0.0F, 0.0F}};
  options.has_hidden_material_slot = true;
  options.hidden_material_slot = kHiddenSlot;
  const auto hidden_geometry =
      quader_editor_core::build_pick_face_geometry(hidden_objects, options);
  for (const bool backfaces : std::array{false, true}) {
    options.pick_backfaces = backfaces;
    EXPECT_TRUE(quader_editor_core::pick_mesh_depth_hits(
                    hidden_objects, {.ray = stair_ray, .options = options}, {},
                    hidden_geometry)
                    .empty());
  }
  options.has_hidden_material_slot = false;
  options.pick_backfaces = false;
  ASSERT_EQ(quader_editor_core::pick_mesh_depth_hits(
                hidden_objects, {.ray = stair_ray, .options = options})
                .size(),
            1U);

  // Including grazing boundaries in the Mesh stack must not loosen ordinary
  // Edge or Vertex eligibility, including their existing two-sided opt-in.
  const quader_poly::Ray corner_ray{{-64.0F, 0.0F, 0.1875F},
                                   {1.0F, 0.0F, 0.0F}};
  for (const auto mode : std::array{quader_poly::SelectionMode::Edge,
                                   quader_poly::SelectionMode::Vertex}) {
    options.mode = mode;
    options.pick_backfaces = false;
    EXPECT_FALSE(quader_editor_core::pick_nearest(
                     hidden_objects, {.ray = corner_ray, .options = options})
                     .has_value());
    options.pick_backfaces = true;
    EXPECT_TRUE(quader_editor_core::pick_nearest(
                    hidden_objects, {.ray = corner_ray, .options = options})
                    .has_value());
  }
}

TEST(QdrModelingRuntimePickingTests,
     MeshDepthFallbackDoesNotPickTriangulationDiagonals) {
  const std::vector objects{single_quad_object(7, false)};
  const auto &vertices = objects[0].document.faces.front().vertices;
  const auto boundary = quader_poly::make_edge(vertices[1], vertices[2]);
  const auto face_geometry = quader_editor_core::build_pick_face_geometry(objects);
  const auto options = quader_editor_core::selection_pick_options_from_radii(
      0.05, 0.05, false);
  // Start within the face plane: a triangulation diagonal crosses the ray
  // before the authored right boundary, but cannot be a mesh fallback target.
  const auto hits = quader_editor_core::pick_mesh_depth_hits(
      objects,
      {.ray = {{-0.5F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
       .options = options},
      {}, face_geometry);
  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].kind, quader_poly::ElementKind::Edge);
  EXPECT_EQ(hits[0].edge, boundary);
  EXPECT_FLOAT_EQ(hits[0].ray_distance, 1.5F);
}

TEST(QdrModelingRuntimePickingTests,
     TransformedVertexPickingRejectsCandidatesOutsideWorldRadius) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  const auto a = quader_poly::add_vertex(object.document, {0.0F, 0.0F, 5.0F});
  const auto b = quader_poly::add_vertex(object.document, {1.0F, 0.0F, 5.0F});
  const auto c = quader_poly::add_vertex(object.document, {0.0F, 1.0F, 5.0F});
  ASSERT_NE(quader_poly::add_face(object.document, std::array{a, c, b}),
            quader_poly::kInvalidElementId);
  const std::array transforms{scaled_pick_transform(7, 10.0F, 0.1F, 1.0F)};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = 7, .empty = false,
      .min = {0.0F, 0.0F, 5.0F}, .max = {10.0F, 0.1F, 5.0F}}};
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.3, 0.3, false);
  options.mode = quader_poly::SelectionMode::Vertex;
  // C is nearest locally, but every vertex is at least three world units away.
  expect_single_component_pick_queries(
      {object}, {.ray = {{3.0F, 0.06F, 0.0F}, {0.0F, 0.0F, 1.0F}},
                 .options = options},
      bounds, transforms, std::nullopt);
}

TEST(QdrModelingRuntimePickingTests,
     TransformedVertexPickingChoosesValidWorldCandidateBeforeLocalWinner) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  (void)quader_poly::add_vertex(object.document, {0.02F, 0.0F, 5.0F});
  const auto valid =
      quader_poly::add_vertex(object.document, {0.0F, 0.5F, 5.0F});
  const std::array transforms{scaled_pick_transform(7, 10.0F, 0.1F, 1.0F)};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = 7, .empty = false,
      .min = {0.0F, 0.0F, 5.0F}, .max = {0.2F, 0.05F, 5.0F}}};
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.1, 0.1, false);
  options.mode = quader_poly::SelectionMode::Vertex;
  // Local distances .02 and .5 become world distances .2 and .05.
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 7, .kind = quader_poly::ElementKind::Vertex,
      .vertex_id = valid, .local_position = {0.0F, 0.05F, 5.0F},
      .ray_distance = 5.0F, .pick_distance = 0.05F};
  expect_single_component_pick_queries(
      {object}, {.ray = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 2.0F}},
                 .options = options},
      bounds, transforms, expected);
}

TEST(QdrModelingRuntimePickingTests,
     TransformedEdgePickingChoosesValidWorldCandidateBeforeLocalWinner) {
  auto object = single_quad_object(7, false);
  for (auto &vertex : object.document.edit().vertices) {
    vertex.position = {vertex.position.x, 0.02F, vertex.position.y};
  }
  const std::array valid_vertices{
      quader_poly::add_vertex(object.document, {-1.0F, -1.0F, 0.5F}),
      quader_poly::add_vertex(object.document, {1.0F, -1.0F, 0.5F}),
      quader_poly::add_vertex(object.document, {1.0F, 1.0F, 0.5F}),
      quader_poly::add_vertex(object.document, {-1.0F, 1.0F, 0.5F}),
  };
  ASSERT_NE(quader_poly::add_face(object.document, valid_vertices),
            quader_poly::kInvalidElementId);
  const std::array transforms{scaled_pick_transform(7, -2.0F, 10.0F, 0.1F)};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = 7, .empty = false,
      .min = {-2.0F, -10.0F, -0.1F}, .max = {2.0F, 10.0F, 0.1F}}};
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.1, 0.1, true);
  options.mode = quader_poly::SelectionMode::Edge;
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 7, .kind = quader_poly::ElementKind::Edge,
      .edge = quader_poly::make_edge(valid_vertices[1], valid_vertices[2]),
      .local_position = {-2.0F, 0.0F, 0.05F},
      .ray_distance = 6.0F, .pick_distance = 0.05F};
  expect_single_component_pick_queries(
      {object}, {.ray = {{-8.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}},
                 .options = options},
      bounds, transforms, expected);
}

TEST(QdrModelingRuntimePickingTests,
     RotatedNonuniformVertexPickingUsesWorldRadiusAndBounds) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  const auto vertex =
      quader_poly::add_vertex(object.document, {1.9F, 0.0F, 5.0F});
  const float diagonal = std::sqrt(0.5F);
  auto transform = scaled_pick_transform(7, 0.1F, 1.0F, 1.0F);
  transform.world_from_object.values[0] = 0.1F * diagonal;
  transform.world_from_object.values[1] = 0.1F * diagonal;
  transform.world_from_object.values[4] = -diagonal;
  transform.world_from_object.values[5] = diagonal;
  transform.object_from_world.values[0] = 10.0F * diagonal;
  transform.object_from_world.values[1] = -diagonal;
  transform.object_from_world.values[4] = 10.0F * diagonal;
  transform.object_from_world.values[5] = diagonal;
  const std::array transforms{transform};
  const quader::QVec3 world_position{0.19F * diagonal, 0.19F * diagonal, 5.0F};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = 7, .empty = false,
      .min = world_position, .max = world_position}};
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.2, 0.2, false);
  options.mode = quader_poly::SelectionMode::Vertex;
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 7, .kind = quader_poly::ElementKind::Vertex,
      .vertex_id = vertex, .local_position = world_position,
      .ray_distance = 5.0F, .pick_distance = 0.19F};
  // The world radius reaches this vertex even though max inverse-column
  // scaling produces a local radius of only 1.4213 for a local distance of 1.9.
  expect_single_component_pick_queries(
      {object}, {.ray = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
                 .options = options},
      bounds, transforms, expected);
}

TEST(QdrModelingRuntimePickingTests,
     ShearedAndReflectedComponentPickingMatchesBakedWorldGeometry) {
  const auto object = single_quad_object(7, false);
  for (const float reflection : std::array{1.0F, -1.0F}) {
    SCOPED_TRACE(reflection);
    auto transform = scaled_pick_transform(7, reflection * 0.25F, 2.0F, 1.0F);
    transform.world_from_object.values[4] = reflection * 1.5F;
    transform.world_from_object.values[12] = 0.4F;
    transform.world_from_object.values[13] = -0.3F;
    transform.world_from_object.values[14] = 5.0F;
    transform.object_from_world.values[4] = -3.0F;
    transform.object_from_world.values[12] = -1.6F * reflection - 0.9F;
    transform.object_from_world.values[13] = 0.15F;
    transform.object_from_world.values[14] = -5.0F;
    const std::array transforms{transform};
    auto baked = object;
    for (auto &vertex : baked.document.edit().vertices) {
      const auto p = vertex.position;
      vertex.position = {reflection * (0.25F * p.x + 1.5F * p.y) + 0.4F,
                         2.0F * p.y - 0.3F, p.z + 5.0F};
    }
    const std::array bounds{pick_bounds_for_object(baked)};
    for (const auto mode : std::array{quader_poly::SelectionMode::Vertex,
                                      quader_poly::SelectionMode::Edge}) {
      SCOPED_TRACE(mode == quader_poly::SelectionMode::Vertex ? "vertex" : "edge");
      auto options = quader_editor_core::selection_pick_options_from_radii(
          0.1, 0.1, false);
      options.mode = mode;
      const bool vertex_mode = mode == quader_poly::SelectionMode::Vertex;
      const quader::QVec3 target{
          reflection * (vertex_mode ? -1.75F : -1.5F) + 0.4F, -2.3F, 5.0F};
      const quader_editor_core::PickRequest request{
          .ray = {{target.x, target.y - 0.08F, target.z + 5.0F * reflection},
                  {0.0F, 0.0F, -2.0F * reflection}},
          .options = options};
      const auto expected = quader_editor_core::pick_nearest({baked}, request);
      ASSERT_TRUE(expected.has_value());
      EXPECT_EQ(expected->kind, vertex_mode ? quader_poly::ElementKind::Vertex
                                           : quader_poly::ElementKind::Edge);
      EXPECT_NEAR(expected->local_position.x, target.x, 0.00001F);
      EXPECT_NEAR(expected->local_position.y, target.y, 0.00001F);
      EXPECT_NEAR(expected->pick_distance, 0.08F, 0.00001F);
      EXPECT_NEAR(expected->ray_distance, 5.0F, 0.00001F);
      expect_single_component_pick_queries({object}, request, bounds, transforms,
                                           expected);
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     CachedComponentPickingBreaksEqualDepthTiesByWorldDistance) {
  quader_editor_core::PolygonObject farther;
  farther.id = 7;
  (void)quader_poly::add_vertex(farther.document, {0.2F, 0.0F, 5.0F});
  quader_editor_core::PolygonObject closer;
  closer.id = 8;
  const auto vertex =
      quader_poly::add_vertex(closer.document, {0.05F, 0.0F, 5.0F});
  auto options = quader_editor_core::selection_pick_options_from_radii(
      0.3, 0.3, false);
  options.mode = quader_poly::SelectionMode::Vertex;
  const quader_editor_core::PickRequest request{
      .ray = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}, .options = options};
  const std::array priority{quader_editor_core::SelectionMode::Vertex};
  for (const bool reversed : std::array{false, true}) {
    SCOPED_TRACE(reversed);
    const std::vector objects = reversed ? std::vector{closer, farther}
                                         : std::vector{farther, closer};
    const std::array bounds{pick_bounds_for_object(objects[0]),
                            pick_bounds_for_object(objects[1])};
    const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
    const std::array nearest_hits{
        quader_editor_core::pick_nearest(objects, request),
        quader_editor_core::pick_nearest(objects, request, bounds, geometry),
        quader_editor_core::pick_nearest_priority(objects, request, priority,
                                                 bounds, geometry)};
    for (const auto &hit : nearest_hits) {
      ASSERT_TRUE(hit.has_value());
      EXPECT_EQ(hit->object_id, closer.id);
      EXPECT_EQ(hit->vertex_id, vertex);
      EXPECT_FLOAT_EQ(hit->pick_distance, 0.05F);
      EXPECT_FLOAT_EQ(hit->ray_distance, 5.0F);
    }
    for (const auto &hits :
         {quader_editor_core::pick_all(objects, request),
          quader_editor_core::pick_all(objects, request, bounds, geometry)}) {
      ASSERT_EQ(hits.size(), 2U);
      EXPECT_EQ(hits[0].object_id, closer.id);
      EXPECT_EQ(hits[1].object_id, farther.id);
    }
  }
}

TEST(QdrModelingRuntimePickingTests, LargeScreenComponentHoverBenchmark) {
  if (std::getenv("QUADER_TEST_LARGE_COMPONENT_HOVER") == nullptr) {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_COMPONENT_HOVER to measure dense component hover.";
  }
  constexpr std::size_t kObjectCount = 6274u;
  constexpr std::size_t kNearbyCount = 32u;
  constexpr int kQueryCount = 20;
  std::vector<quader_editor_core::PolygonObject> objects;
  std::vector<quader_editor_core::PickObjectBounds> bounds;
  objects.reserve(kObjectCount);
  bounds.reserve(kObjectCount);
  for (std::size_t index = 0; index < kObjectCount; ++index) {
    quader_editor_core::PolygonObject object;
    object.id = static_cast<int>(index + 1u);
    if (index < kNearbyCount) {
      quader_poly::add_vertex(object.document,
          {static_cast<float>(index) * 0.001F, 0.0F,
           -10.0F - static_cast<float>(index) * 0.01F});
    } else {
      object = cube_object(object.id,
          {1000.0F + static_cast<float>(index), 0.0F, -10.0F});
    }
    bounds.push_back(pick_bounds_for_object(object));
    objects.push_back(std::move(object));
  }
  const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
  const auto screen = component_pick_screen_context();
  quader_editor_core::PickScreenWorkCounters counters;
  quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = {.mode = quader_poly::SelectionMode::Vertex},
      .screen_context = screen,
      .screen_work_counters = &counters};
  const auto begin = std::chrono::steady_clock::now();
  for (int query = 0; query < kQueryCount; ++query) {
    const auto hit = quader_editor_core::pick_nearest(objects, request, bounds, geometry);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->object_id, objects.front().id);
  }
  const auto milliseconds = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - begin).count();
  std::cout << "COMPONENT_HOVER objects=" << objects.size()
            << " nearby=" << kNearbyCount << " queries=" << kQueryCount
            << " ms=" << milliseconds
            << " projected_bounds=" << counters.projected_object_bounds
            << " visibility_object_tests=" << counters.visibility_object_tests
            << " visibility_queries=" << counters.visibility_queries
            << " visibility_ray_queries=" << counters.visibility_ray_queries
            << " visibility_cache_hits=" << counters.visibility_cache_hits
            << " surface_geometry_queries=" << counters.surface_geometry_queries << std::endl;
}

TEST(QdrModelingRuntimePickingTests, DenseOverlappingScreenComponentHoverBenchmark) {
  if (std::getenv("QUADER_TEST_LARGE_COMPONENT_HOVER") == nullptr) {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_COMPONENT_HOVER to measure dense component hover.";
  }
  constexpr std::size_t kObjectCount = 64u;
  constexpr std::size_t kGridSide = 9u;
  constexpr int kQueryCount = 3;
  std::vector<quader_editor_core::PolygonObject> objects;
  std::vector<quader_editor_core::PickObjectBounds> bounds;
  objects.reserve(kObjectCount);
  bounds.reserve(kObjectCount);
  for (std::size_t index = 0; index < kObjectCount; ++index) {
    quader_editor_core::PolygonObject object;
    object.id = static_cast<int>(index + 1u);
    std::array<quader_poly::ElementId, kGridSide * kGridSide> vertices{};
    for (std::size_t row = 0; row < kGridSide; ++row) {
      for (std::size_t column = 0; column < kGridSide; ++column) {
        vertices[row * kGridSide + column] = quader_poly::add_vertex(
            object.document,
            {(static_cast<float>(column) - 4.0F) * 0.01F,
             (static_cast<float>(row) - 4.0F) * 0.01F,
             -10.0F - static_cast<float>(index) * 0.01F});
      }
    }
    for (std::size_t row = 0; row + 1u < kGridSide; ++row) {
      for (std::size_t column = 0; column + 1u < kGridSide; ++column) {
        const auto a = vertices[row * kGridSide + column];
        const auto b = vertices[row * kGridSide + column + 1u];
        const auto c = vertices[(row + 1u) * kGridSide + column + 1u];
        const auto d = vertices[(row + 1u) * kGridSide + column];
        ASSERT_NE(quader_poly::add_face(object.document, std::array{a, b, c}),
                  quader_poly::kInvalidElementId);
        ASSERT_NE(quader_poly::add_face(object.document, std::array{a, c, d}),
                  quader_poly::kInvalidElementId);
      }
    }
    bounds.push_back(pick_bounds_for_object(object));
    objects.push_back(std::move(object));
  }
  const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
  auto screen = component_pick_screen_context();
  screen.camera.projection = quader_editor_core::CameraProjection::Orthographic;
  screen.camera.orthographic_size = 10.0;
  for (const auto mode : std::array{quader_poly::SelectionMode::Vertex,
                                   quader_poly::SelectionMode::Edge}) {
    quader_editor_core::PickScreenWorkCounters counters;
    const quader_editor_core::PickRequest request{
        .ray = quader_editor_core::ray_from_viewport_screen_point(
            screen.camera, screen.screen_position),
        .options = {.mode = mode}, .screen_context = screen,
        .screen_work_counters = &counters};
    std::optional<quader_editor_core::PickHitRecord> hit;
    const auto begin = std::chrono::steady_clock::now();
    for (int query = 0; query < kQueryCount; ++query) {
      hit = quader_editor_core::pick_nearest(objects, request, bounds, geometry);
    }
    const auto milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->object_id, objects.front().id);
    EXPECT_FLOAT_EQ(hit->pick_distance, 0.0F);
    EXPECT_FLOAT_EQ(hit->ray_distance, 10.0F);
    std::cout << "DENSE_COMPONENT_HOVER mode="
              << (mode == quader_poly::SelectionMode::Vertex ? "vertex" : "edge")
              << " objects=" << objects.size()
              << " faces_per_object=" << objects.front().document.faces.size()
              << " vertices_per_object=" << objects.front().document.vertices.size()
              << " queries=" << kQueryCount << " ms=" << milliseconds
              << " hit_object=" << hit->object_id
              << " hit_distance=" << hit->pick_distance
              << " projected_bounds=" << counters.projected_object_bounds
              << " visibility_object_tests=" << counters.visibility_object_tests
              << " visibility_queries=" << counters.visibility_queries
              << " visibility_ray_queries=" << counters.visibility_ray_queries
              << " visibility_cache_hits=" << counters.visibility_cache_hits
              << " surface_geometry_queries=" << counters.surface_geometry_queries << std::endl;
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentRankingDoesNotRetainAnOccludedCandidate) {
  for (const auto mode : std::array{quader_poly::SelectionMode::Vertex,
                                   quader_poly::SelectionMode::Edge}) {
    SCOPED_TRACE(mode == quader_poly::SelectionMode::Vertex ? "vertex" : "edge");
    quader_editor_core::PolygonObject object;
    object.id = 7;
    const quader::QVec3 visible_position{0.0F, 0.025F, -4.0F};
    quader_editor_core::PickHitRecord expected{
        .hit = true, .object_id = object.id,
        .local_position = visible_position,
        .ray_distance = quader::length(visible_position), .pick_distance = 2.5F};
    if (mode == quader_poly::SelectionMode::Vertex) {
      (void)quader_poly::add_vertex(object.document, {0.0F, 0.0F, -10.0F});
      expected.kind = quader_poly::ElementKind::Vertex;
      expected.vertex_id = quader_poly::add_vertex(object.document, visible_position);
    } else {
      expected.kind = quader_poly::ElementKind::Edge;
      for (const auto position : std::array{quader::QVec3{0.0F, 0.0F, -10.0F},
                                           visible_position}) {
        const auto a = quader_poly::add_vertex(
            object.document, position + quader::QVec3{-1.0F, 0.0F, 0.0F});
        const auto b = quader_poly::add_vertex(
            object.document, position + quader::QVec3{1.0F, 0.0F, 0.0F});
        const auto c = quader_poly::add_vertex(
            object.document, position + quader::QVec3{0.0F, -1.0F, 0.0F});
        ASSERT_NE(quader_poly::add_face(object.document, std::array{a, c, b}),
                  quader_poly::kInvalidElementId);
        expected.edge = quader_poly::make_edge(a, b);
      }
    }
    auto occluder = single_quad_object(8, false);
    for (auto &vertex : occluder.document.edit().vertices) {
      vertex.position.z = -5.0F;
    }
    const auto screen = component_pick_screen_context();
    const quader_editor_core::PickRequest request{
        .ray = quader_editor_core::ray_from_viewport_screen_point(
            screen.camera, screen.screen_position),
        .options = {.mode = mode}, .screen_context = screen};
    const std::array bounds{pick_bounds_for_object(object),
                            pick_bounds_for_object(occluder)};
    expect_single_component_pick_queries({object, occluder}, request, bounds, {}, expected);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentRankingPreservesEpsilonFacingAndDepthPrecedence) {
  auto screen = component_pick_screen_context();
  screen.camera.projection = quader_editor_core::CameraProjection::Orthographic;
  screen.camera.viewport_size = {2.0F, 2.0F};
  screen.camera.orthographic_size = 2.0;
  screen.screen_position = {1.0F, 1.0F};
  screen.vertex_radius_pixels = 0.1;
  for (const float distance_delta : std::array{0.0000005F, 0.000004F}) {
    for (const bool prefer_front : std::array{false, true}) {
      SCOPED_TRACE(distance_delta);
      SCOPED_TRACE(prefer_front);
      quader_editor_core::PolygonObject object;
      object.id = 7;
      const auto back = quader_poly::add_vertex(object.document, {-0.05F, 0.0F, -2.0F});
      const auto back_a = quader_poly::add_vertex(object.document, {-0.5F, -0.5F, -2.0F});
      const auto back_b = quader_poly::add_vertex(object.document, {-0.5F, 0.5F, -2.0F});
      ASSERT_NE(quader_poly::add_face(object.document, std::array{back, back_a, back_b}),
                quader_poly::kInvalidElementId);
      const auto front = quader_poly::add_vertex(
          object.document, {0.05F + distance_delta, 0.0F, -4.0F});
      const auto front_a = quader_poly::add_vertex(object.document, {0.5F, -0.5F, -4.0F});
      const auto front_b = quader_poly::add_vertex(object.document, {0.5F, 0.5F, -4.0F});
      ASSERT_NE(quader_poly::add_face(object.document, std::array{front, front_a, front_b}),
                quader_poly::kInvalidElementId);
      const quader_editor_core::PickRequest request{
          .ray = quader_editor_core::ray_from_viewport_screen_point(
              screen.camera, screen.screen_position),
          .options = {.mode = quader_poly::SelectionMode::Vertex,
                      .pick_backfaces = true,
                      .prefer_front_facing_faces = prefer_front},
          .screen_context = screen};
      // The disjoint triangles cannot occlude each other's candidate. Within
      // the metric epsilon, facing wins over depth only when requested.
      const bool expect_front = prefer_front && distance_delta < 0.000001F;
      const auto hit = quader_editor_core::pick_nearest({object}, request);
      ASSERT_TRUE(hit.has_value());
      EXPECT_EQ(hit->vertex_id, expect_front ? front : back);
      EXPECT_FLOAT_EQ(hit->ray_distance, expect_front ? 4.0F : 2.0F);
    }
  }
  // Without incident faces, the nearer point wins a sub-epsilon distance tie
  // even though it appears later and is slightly farther from the cursor.
  quader_editor_core::PolygonObject points;
  points.id = 9;
  (void)quader_poly::add_vertex(points.document, {-0.05F, 0.0F, -4.0F});
  const auto nearer = quader_poly::add_vertex(points.document, {0.0500005F, 0.0F, -2.0F});
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = {.mode = quader_poly::SelectionMode::Vertex}, .screen_context = screen};
  const auto hit = quader_editor_core::pick_nearest({points}, request);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->vertex_id, nearer);
  EXPECT_FLOAT_EQ(hit->ray_distance, 2.0F);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentRejectsWorseCandidatesBeforeVisibilityQueries) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  const auto closest = quader_poly::add_vertex(object.document, {0.0F, 0.0F, -10.0F});
  for (int index = 1; index <= 32; ++index) {
    (void)quader_poly::add_vertex(
        object.document, {static_cast<float>(index) * 0.001F, 0.0F, -10.0F});
  }
  const auto screen = component_pick_screen_context();
  quader_editor_core::PickScreenWorkCounters counters;
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = {.mode = quader_poly::SelectionMode::Vertex},
      .screen_context = screen, .screen_work_counters = &counters};
  const auto hit = quader_editor_core::pick_nearest({object}, request);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->vertex_id, closest);
  EXPECT_EQ(counters.visibility_queries, 1u);
  EXPECT_EQ(counters.visibility_ray_queries, 1u);
  EXPECT_EQ(counters.visibility_cache_hits, 0u);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentVisibilitySharesExactRayHitsAndMissesWithinOneQuery) {
  constexpr std::size_t kObjectCount = 12u;
  for (const bool has_faces : std::array{false, true}) {
    SCOPED_TRACE(has_faces ? "surface hit" : "surface miss");
    std::vector<quader_editor_core::PolygonObject> objects;
    std::vector<quader_editor_core::PickObjectBounds> bounds;
    for (std::size_t index = 0; index < kObjectCount; ++index) {
      quader_editor_core::PolygonObject object;
      object.id = static_cast<int>(index + 1u);
      const auto center = quader_poly::add_vertex(object.document, {0.0F, 0.0F, -10.0F});
      if (has_faces) {
        const auto a = quader_poly::add_vertex(object.document, {-1.0F, -1.0F, -10.0F});
        const auto b = quader_poly::add_vertex(object.document, {1.0F, -1.0F, -10.0F});
        ASSERT_NE(quader_poly::add_face(object.document, std::array{center, a, b}),
                  quader_poly::kInvalidElementId);
      }
      bounds.push_back(pick_bounds_for_object(object));
      objects.push_back(std::move(object));
    }
    const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
    const auto screen = component_pick_screen_context();
    quader_editor_core::PickScreenWorkCounters counters;
    quader_editor_core::PickRequest request{
        .ray = quader_editor_core::ray_from_viewport_screen_point(
            screen.camera, screen.screen_position),
        .options = {.mode = quader_poly::SelectionMode::Vertex},
        .screen_context = screen, .screen_work_counters = &counters};
    for (int repeat = 0; repeat < 2; ++repeat) {
      counters = {};
      const auto hits = quader_editor_core::pick_all(objects, request, bounds, geometry);
      ASSERT_EQ(hits.size(), kObjectCount);
      for (std::size_t index = 0; index < hits.size(); ++index) {
        EXPECT_EQ(hits[index].object_id, static_cast<int>(index + 1u));
        EXPECT_FLOAT_EQ(hits[index].pick_distance, 0.0F);
        EXPECT_FLOAT_EQ(hits[index].ray_distance, 10.0F);
      }
      EXPECT_EQ(counters.visibility_queries, kObjectCount);
      EXPECT_EQ(counters.visibility_ray_queries, 1u);
      EXPECT_EQ(counters.visibility_cache_hits, kObjectCount - 1u);
      // pick_all also resolves its independent cursor surface after collecting
      // components. Count that face pass as well as the one shared visibility ray.
      EXPECT_EQ(counters.visibility_object_tests, 2u * kObjectCount);
      EXPECT_EQ(counters.surface_geometry_queries, 2u * kObjectCount);
    }
    counters = {};
    request.target_object_id = objects.back().id;
    const auto target = quader_editor_core::pick_all(objects, request, bounds, geometry);
    ASSERT_EQ(target.size(), 1u);
    EXPECT_EQ(target.front().object_id, objects.back().id);
    EXPECT_EQ(counters.visibility_queries, 1u);
    EXPECT_EQ(counters.visibility_ray_queries, 1u);
    EXPECT_EQ(counters.visibility_cache_hits, 0u);
    EXPECT_EQ(counters.surface_geometry_queries, 2u);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentVisibilityBoundsKeepDistinctRaysAndMissingBounds) {
  constexpr std::size_t kObjectCount = 16u;
  std::vector<quader_editor_core::PolygonObject> objects;
  std::vector<quader_editor_core::PickObjectBounds> bounds;
  for (std::size_t index = 0; index < kObjectCount; ++index) {
    quader_editor_core::PolygonObject object;
    object.id = static_cast<int>(index + 1u);
    (void)quader_poly::add_vertex(
        object.document, {static_cast<float>(index) * 0.004F, 0.0F, -10.0F});
    auto object_bounds = pick_bounds_for_object(object);
    object_bounds.min.x -= 0.00001F;
    object_bounds.max.x += 0.00001F;
    bounds.push_back(object_bounds);
    objects.push_back(std::move(object));
  }
  // One object deliberately has no cached bound and remains a conservative
  // visibility participant for every distinct candidate ray.
  bounds.pop_back();
  auto screen = component_pick_screen_context();
  screen.camera.projection = quader_editor_core::CameraProjection::Orthographic;
  screen.camera.orthographic_size = 10.0;
  const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
  quader_editor_core::PickScreenWorkCounters counters;
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = {.mode = quader_poly::SelectionMode::Vertex},
      .screen_context = screen, .screen_work_counters = &counters};
  const auto hits = quader_editor_core::pick_all(objects, request, bounds, geometry);
  ASSERT_EQ(hits.size(), kObjectCount);
  for (std::size_t index = 0; index < hits.size(); ++index) {
    EXPECT_EQ(hits[index].object_id, static_cast<int>(index + 1u));
    EXPECT_FLOAT_EQ(hits[index].ray_distance, 10.0F);
  }
  EXPECT_EQ(counters.visibility_queries, kObjectCount);
  EXPECT_EQ(counters.visibility_ray_queries, kObjectCount);
  EXPECT_EQ(counters.visibility_cache_hits, 0u);
  // Up to two bounds candidates per component ray, plus pick_all's cursor pass.
  EXPECT_LE(counters.visibility_object_tests, 3u * kObjectCount);
  EXPECT_LT(counters.visibility_object_tests, kObjectCount * kObjectCount / 2u);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentVisibilityBoundsExcludeOffCursorObjectsOnce) {
  constexpr std::size_t kNearbyCount = 3u;
  constexpr std::size_t kFarCount = 128u;
  for (const auto projection :
       std::array{quader_editor_core::CameraProjection::Orthographic,
                  quader_editor_core::CameraProjection::Perspective}) {
    SCOPED_TRACE(projection == quader_editor_core::CameraProjection::Orthographic
                     ? "orthographic" : "perspective");
    for (const auto mode : std::array{quader_poly::SelectionMode::Vertex,
                                     quader_poly::SelectionMode::Edge}) {
      SCOPED_TRACE(mode == quader_poly::SelectionMode::Vertex ? "vertex" : "edge");
      std::vector<quader_editor_core::PolygonObject> objects;
      objects.reserve(kNearbyCount + kFarCount);
      for (std::size_t index = 0; index < kNearbyCount; ++index) {
        quader_editor_core::PolygonObject object;
        object.id = static_cast<int>(index + 1u);
        const float depth = 5.0F * static_cast<float>(index + 1u);
        if (mode == quader_poly::SelectionMode::Vertex) {
          quader_poly::add_vertex(object.document, {0.0F, 0.0F, -depth});
        } else {
          const auto a = quader_poly::add_vertex(object.document, {-1.0F, 0.0F, -depth});
          const auto b = quader_poly::add_vertex(object.document, {1.0F, 0.0F, -depth});
          const auto c = quader_poly::add_vertex(object.document, {0.0F, -1.0F, -depth});
          ASSERT_NE(quader_poly::add_face(object.document, std::array{a, c, b}),
                    quader_poly::kInvalidElementId);
        }
        objects.push_back(std::move(object));
      }
      for (std::size_t index = 0; index < kFarCount; ++index) {
        objects.push_back(cube_object(static_cast<int>(index + kNearbyCount + 1u),
            {1000.0F + static_cast<float>(index), 0.0F, -10.0F}));
      }
      auto screen = component_pick_screen_context();
      screen.camera.projection = projection;
      screen.camera.orthographic_size = 10.0;
      quader_editor_core::PickRequest request{
          .ray = quader_editor_core::ray_from_viewport_screen_point(
              screen.camera, screen.screen_position),
          .options = {.mode = mode}, .screen_context = screen};
      for (const bool reversed : std::array{false, true}) {
        SCOPED_TRACE(reversed ? "reversed" : "forward");
        if (reversed) {
          std::ranges::reverse(objects);
        }
        std::vector<quader_editor_core::PickObjectBounds> bounds;
        bounds.reserve(objects.size());
        for (const auto &object : objects) {
          bounds.push_back(pick_bounds_for_object(object));
        }
        const auto geometry = quader_editor_core::build_pick_face_geometry(objects);
        request.screen_work_counters = nullptr;
        const auto reference = quader_editor_core::pick_all(objects, request);
        ASSERT_EQ(reference.size(),
                  mode == quader_poly::SelectionMode::Vertex ? kNearbyCount : 1u);
        EXPECT_EQ(reference.front().object_id, 1);
        const auto actual = quader_editor_core::pick_all(objects, request, bounds, geometry);
        ASSERT_EQ(actual.size(), reference.size());
        for (std::size_t index = 0; index < actual.size(); ++index) {
          expect_same_component_hit(actual[index], reference[index]);
        }
        quader_editor_core::PickScreenWorkCounters counters;
        request.screen_work_counters = &counters;
        const auto nearest = quader_editor_core::pick_nearest(
            objects, request, bounds, geometry);
        ASSERT_TRUE(nearest.has_value());
        expect_same_component_hit(*nearest, reference.front());
        EXPECT_EQ(counters.projected_object_bounds, objects.size());
        EXPECT_GT(counters.visibility_object_tests, 0u);
        EXPECT_LE(counters.visibility_object_tests, kNearbyCount * kNearbyCount);
      }
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentVisibilityKeepsOffsetOccludersAndMissingBounds) {
  for (const bool transformed : std::array{false, true}) {
    SCOPED_TRACE(transformed ? "reflected nonuniform transform" : "world geometry");
    auto front = single_quad_object(7, false);
    for (auto &vertex : front.document.edit().vertices) {
      vertex.position.x = 0.0625F + vertex.position.x * 0.0125F;
      vertex.position.z = -5.0F;
    }
    quader_editor_core::PolygonObject rear;
    rear.id = 8;
    quader_poly::add_vertex(rear.document, {0.125F, 0.0F, -10.0F});
    const std::array bounds{pick_bounds_for_object(front), pick_bounds_for_object(rear)};
    std::vector<quader_editor_core::PolygonObject> objects{front, rear};
    std::vector<quader_editor_core::PickObjectTransform> transforms;
    if (transformed) {
      for (auto &object : objects) {
        transforms.push_back(scaled_pick_transform(object.id, -2.0F, 3.0F, 0.5F));
        for (auto &vertex : object.document.edit().vertices) {
          vertex.position.x /= -2.0F;
          vertex.position.y /= 3.0F;
          vertex.position.z /= 0.5F;
        }
      }
    }
    const auto screen = component_pick_screen_context();
    const quader_editor_core::PickRequest request{
        .ray = quader_editor_core::ray_from_viewport_screen_point(
            screen.camera, screen.screen_position),
        .options = {.mode = quader_poly::SelectionMode::Vertex},
        .screen_context = screen};
    const auto unobstructed = quader_editor_core::pick_nearest(
        {objects.back()}, request, std::span{bounds}.last(1), {}, transforms);
    ASSERT_TRUE(unobstructed.has_value());
    EXPECT_EQ(unobstructed->object_id, rear.id);
    EXPECT_NEAR(unobstructed->pick_distance, 5.0F, 0.0001F);
    // The front quad misses the cursor ray but covers the offset vertex ray.
    expect_single_component_pick_queries(objects, request, bounds, transforms,
                                          std::nullopt);
    expect_single_component_pick_queries(objects, request, std::span{bounds}.last(1),
                                          transforms, std::nullopt);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentVisibilityRetainsUnprojectableNearOccluders) {
  auto front = single_quad_object(7, false);
  for (auto &vertex : front.document.edit().vertices) {
    vertex.position.z = -0.00005F;
  }
  quader_editor_core::PolygonObject rear;
  rear.id = 8;
  quader_poly::add_vertex(rear.document, {0.0F, 0.0F, -1.0F});
  auto screen = component_pick_screen_context();
  screen.near_plane = 0.0;
  quader::QVec2 projected;
  EXPECT_FALSE(quader_editor_core::project_viewport_point(
      screen.camera, front.document.vertices.front().position, projected));
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = {.mode = quader_poly::SelectionMode::Vertex},
      .screen_context = screen};
  ASSERT_TRUE(quader_editor_core::pick_nearest({rear}, request).has_value());
  const std::array bounds{pick_bounds_for_object(front), pick_bounds_for_object(rear)};
  expect_single_component_pick_queries({front, rear}, request, bounds, {}, std::nullopt);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentRadiusStaysInPixelsAcrossZoomAndDepth) {
  for (const auto projection :
       std::array{quader_editor_core::CameraProjection::Orthographic,
                  quader_editor_core::CameraProjection::Perspective}) {
    SCOPED_TRACE(projection == quader_editor_core::CameraProjection::Orthographic
                     ? "orthographic" : "perspective");
    for (const float scale : std::array{1.0F, 1000.0F}) {
      SCOPED_TRACE(scale);
      auto screen = component_pick_screen_context();
      screen.camera.projection = projection;
      screen.camera.orthographic_size = scale == 1.0F ? 10.0 : 1000.0;
      const bool perspective =
          projection == quader_editor_core::CameraProjection::Perspective;
      const float depth = perspective ? scale : 5.0F;
      const float extent = perspective ? depth * 0.2F
          : static_cast<float>(screen.camera.orthographic_size) * 0.2F;
      for (const auto mode : std::array{quader_poly::SelectionMode::Vertex,
                                       quader_poly::SelectionMode::Edge}) {
        SCOPED_TRACE(mode == quader_poly::SelectionMode::Vertex ? "vertex" : "edge");
        quader_editor_core::PolygonObject object;
        object.id = 7;
        quader_editor_core::PickHitRecord expected{
            .hit = true, .object_id = 7, .local_position = {0.0F, 0.0F, -depth},
            .pick_distance = 5.0F};
        if (mode == quader_poly::SelectionMode::Vertex) {
          expected.kind = quader_poly::ElementKind::Vertex;
          expected.vertex_id = quader_poly::add_vertex(
              object.document, expected.local_position);
        } else {
          expected.kind = quader_poly::ElementKind::Edge;
          const auto a = quader_poly::add_vertex(object.document, {-extent, 0.0F, -depth});
          const auto b = quader_poly::add_vertex(object.document, {extent, 0.0F, -depth});
          const auto c = quader_poly::add_vertex(object.document, {0.0F, -extent, -depth});
          ASSERT_NE(quader_poly::add_face(object.document, std::array{a, c, b}),
                    quader_poly::kInvalidElementId);
          expected.edge = quader_poly::make_edge(a, b);
        }
        const std::array bounds{pick_bounds_for_object(object)};
        for (const float pixel_offset : std::array{5.0F, 7.0F}) {
          SCOPED_TRACE(pixel_offset);
          screen.screen_position = {400.0F, 400.0F - pixel_offset};
          quader_poly::PickOptions options;
          options.mode = mode;
          options.pick_backfaces = false;
          // Both acceptance and rejection must ignore camera-less world radii.
          options.vertex_radius = pixel_offset < 6.0F ? 0.00001F : 10000.0F;
          options.edge_radius = options.vertex_radius;
          const quader_editor_core::PickRequest request{
              .ray = quader_editor_core::ray_from_viewport_screen_point(
                  screen.camera, screen.screen_position),
              .options = options, .screen_context = screen};
          // This candidate projects to the center, so its own ray depth is
          // exactly depth even when the cursor ray is offset from it.
          expected.ray_distance = depth;
          expect_single_component_pick_queries(
              {object}, request, bounds, {},
              pixel_offset < 6.0F ? std::optional{expected} : std::nullopt);
        }
      }
    }
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenEdgeHitUsesPerspectiveCorrectWorldInterpolation) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  const auto a = quader_poly::add_vertex(object.document, {-0.5F, 0.0F, -1.0F});
  const auto b = quader_poly::add_vertex(object.document, {5.0F, 0.0F, -10.0F});
  const auto c = quader_poly::add_vertex(object.document, {0.0F, -3.0F, -5.0F});
  ASSERT_NE(quader_poly::add_face(object.document, std::array{a, c, b}),
            quader_poly::kInvalidElementId);
  auto screen = component_pick_screen_context();
  screen.screen_position = {400.0F, 395.0F};
  quader_poly::PickOptions options;
  options.mode = quader_poly::SelectionMode::Edge;
  options.pick_backfaces = false;
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = options, .screen_context = screen};
  // Screen endpoints are (200,400) and (600,400). Their projected midpoint
  // is 1/11 along the world segment, rather than its geometric midpoint.
  const quader::QVec3 position{0.0F, 0.0F, -20.0F / 11.0F};
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 7, .kind = quader_poly::ElementKind::Edge,
      .edge = quader_poly::make_edge(a, b), .local_position = position,
      .ray_distance = quader::length(position),
      .pick_distance = 5.0F};
  const std::array bounds{pick_bounds_for_object(object)};
  expect_single_component_pick_queries({object}, request, bounds, {}, expected);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenEdgeCrossingCameraClipsBeforeProjectingAndInterpolating) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  const auto a = quader_poly::add_vertex(object.document, {-0.1F, 0.0F, 1.0F});
  const auto b = quader_poly::add_vertex(object.document, {0.2F, 0.0F, -1.0F});
  const auto c = quader_poly::add_vertex(object.document, {0.0F, -1.0F, -1.0F});
  ASSERT_NE(quader_poly::add_face(object.document, std::array{a, b, c}),
            quader_poly::kInvalidElementId);
  auto screen = component_pick_screen_context();
  screen.near_plane = 0.1;
  screen.screen_position = {550.0F, 395.0F};
  quader_poly::PickOptions options;
  options.mode = quader_poly::SelectionMode::Edge;
  options.pick_backfaces = false;
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = options, .screen_context = screen};
  // The visible AB portion projects from (660,400) to (480,400). The hit
  // lies 11/18 along the original world edge, after the near-plane crossing.
  const quader::QVec3 position{1.0F / 12.0F, 0.0F, -2.0F / 9.0F};
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 7, .kind = quader_poly::ElementKind::Edge,
      .edge = quader_poly::make_edge(a, b), .local_position = position,
      .ray_distance = quader::length(position),
      .pick_distance = 5.0F};
  const std::array bounds{pick_bounds_for_object(object)};
  expect_single_component_pick_queries({object}, request, bounds, {}, expected);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentClippingExcludesInvisibleOccludersAndFarCandidates) {
  auto front = single_quad_object(7, false);
  for (auto &vertex : front.document.edit().vertices) {
    vertex.position.z = -0.5F;
  }
  quader_editor_core::PolygonObject rear;
  rear.id = 8;
  const auto vertex = quader_poly::add_vertex(rear.document, {0.0F, 0.0F, -2.0F});
  auto screen = component_pick_screen_context();
  screen.near_plane = 1.0;
  screen.far_plane = 10.0;
  quader_poly::PickOptions options;
  options.mode = quader_poly::SelectionMode::Vertex;
  options.pick_backfaces = false;
  quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = options, .screen_context = screen};
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 8, .kind = quader_poly::ElementKind::Vertex,
      .vertex_id = vertex, .local_position = {0.0F, 0.0F, -2.0F},
      .ray_distance = 2.0F, .pick_distance = 0.0F};
  const std::array bounds{pick_bounds_for_object(front), pick_bounds_for_object(rear)};
  expect_single_component_pick_queries({front, rear}, request, bounds, {}, expected);
  request.screen_context->near_plane = 0.1;
  expect_single_component_pick_queries({front, rear}, request, bounds, {}, std::nullopt);
  request.screen_context->near_plane = 1.0;
  rear.document.edit().vertices.front().position.z = -11.0F;
  const std::array far_bounds{pick_bounds_for_object(rear)};
  expect_single_component_pick_queries({rear}, request, far_bounds, {}, std::nullopt);
}

TEST(QdrModelingRuntimePickingTests,
     ScreenComponentPickingHonorsBackfacePolicy) {
  auto object = single_quad_object(7, true);
  for (auto &vertex : object.document.edit().vertices) {
    vertex.position.z = -5.0F;
  }
  const std::array bounds{pick_bounds_for_object(object)};
  for (const auto mode : std::array{quader_poly::SelectionMode::Vertex,
                                   quader_poly::SelectionMode::Edge}) {
    auto screen = component_pick_screen_context();
    const quader::QVec3 position{
        mode == quader_poly::SelectionMode::Vertex ? -1.0F : 0.0F, -1.0F, -5.0F};
    ASSERT_TRUE(quader_editor_core::project_viewport_point(
        screen.camera, position, screen.screen_position));
    quader_poly::PickOptions options;
    options.mode = mode;
    options.pick_backfaces = false;
    quader_editor_core::PickRequest request{
        .ray = quader_editor_core::ray_from_viewport_screen_point(
            screen.camera, screen.screen_position),
        .options = options, .screen_context = screen};
    expect_single_component_pick_queries({object}, request, bounds, {}, std::nullopt);
    request.options.pick_backfaces = true;
    quader_editor_core::PickHitRecord expected{
        .hit = true, .object_id = 7, .local_position = position,
        .ray_distance = quader::length(position),
        .pick_distance = 0.0F};
    if (mode == quader_poly::SelectionMode::Vertex) {
      expected.kind = quader_poly::ElementKind::Vertex;
      expected.vertex_id = object.document.vertices[0].id;
    } else {
      expected.kind = quader_poly::ElementKind::Edge;
      expected.edge = quader_poly::make_edge(object.document.vertices[0].id,
                                            object.document.vertices[1].id);
    }
    expect_single_component_pick_queries({object}, request, bounds, {}, expected);
  }
}

TEST(QdrModelingRuntimePickingTests,
     ScreenVertexPickingUsesCandidateRayAtWideFieldOfView) {
  quader_editor_core::PolygonObject object;
  object.id = 7;
  const quader::QVec3 position{-5.0F, 0.0F, -1.0F};
  const auto vertex = quader_poly::add_vertex(object.document, position);
  auto screen = component_pick_screen_context();
  screen.camera.viewport_size = {800.0F, 600.0F};
  screen.camera.fov_degrees = 179.0;
  screen.vertex_radius_pixels = 32.0;
  ASSERT_TRUE(quader_editor_core::project_viewport_point(
      screen.camera, {5.0F, 0.0F, -1.0F}, screen.screen_position));
  quader::QVec2 projected;
  ASSERT_TRUE(quader_editor_core::project_viewport_point(
      screen.camera, position, projected));
  quader_poly::PickOptions options;
  options.mode = quader_poly::SelectionMode::Vertex;
  const quader_editor_core::PickRequest request{
      .ray = quader_editor_core::ray_from_viewport_screen_point(
          screen.camera, screen.screen_position),
      .options = options, .screen_context = screen};
  // Both points are visible and about 26 pixels apart, even though this
  // vertex lies behind the plane perpendicular to the cursor ray.
  ASSERT_LT(quader::dot(position, request.ray.direction), 0.0F);
  const quader_editor_core::PickHitRecord expected{
      .hit = true, .object_id = 7, .kind = quader_poly::ElementKind::Vertex,
      .vertex_id = vertex, .local_position = position,
      .ray_distance = std::sqrt(26.0F),
      .pick_distance = std::abs(projected.x - screen.screen_position.x)};
  ASSERT_LT(expected.pick_distance, screen.vertex_radius_pixels);
  const std::array bounds{pick_bounds_for_object(object)};
  expect_single_component_pick_queries({object}, request, bounds, {}, expected);
}

TEST(QdrModelingRuntimePickingTests,
     MeshDepthFallbackUsesWorldRadiusBeforeChoosingTransformedEdge) {
  auto object = single_quad_object(7, false);
  for (auto &vertex : object.document.edit().vertices) {
    vertex.position = {vertex.position.x, 0.02F, vertex.position.y};
  }
  const std::array valid_vertices{
      quader_poly::add_vertex(object.document, {-1.0F, -1.0F, 0.5F}),
      quader_poly::add_vertex(object.document, {1.0F, -1.0F, 0.5F}),
      quader_poly::add_vertex(object.document, {1.0F, 1.0F, 0.5F}),
      quader_poly::add_vertex(object.document, {-1.0F, 1.0F, 0.5F}),
  };
  ASSERT_NE(quader_poly::add_face(object.document, valid_vertices),
            quader_poly::kInvalidElementId);
  const std::vector objects{object};
  const std::array transforms{scaled_pick_transform(7, -2.0F, 10.0F, 0.1F)};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = 7,
      .empty = false,
      .min = {-2.0F, -10.0F, -0.1F},
      .max = {2.0F, 10.0F, 0.1F},
  }};
  const auto face_geometry = quader_editor_core::build_pick_face_geometry(objects);
  quader_editor_core::PickRequest request{
      .ray = {{-8.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
      .options = quader_editor_core::selection_pick_options_from_radii(
          0.1, 0.1, false),
  };
  // The first quad is closer locally (.02 versus .5), but outside the world
  // radius (.2 versus .05). Rejecting only a single local winner loses the hit.
  const auto hits = quader_editor_core::pick_mesh_depth_hits(
      objects, request, bounds, face_geometry, transforms);
  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].kind, quader_poly::ElementKind::Edge);
  EXPECT_EQ(hits[0].edge,
            quader_poly::make_edge(valid_vertices[1], valid_vertices[2]));
  EXPECT_NEAR(hits[0].pick_distance, 0.05F, 0.000001F);
  EXPECT_FLOAT_EQ(hits[0].ray_distance, 6.0F);
  request.options.edge_radius = 0.01F;
  EXPECT_TRUE(quader_editor_core::pick_mesh_depth_hits(
                  objects, request, bounds, face_geometry, transforms)
                  .empty());
}

TEST(QdrModelingRuntimePickingTests,
     MeshDepthFallbackRejectsBoundariesBehindRayOrigin) {
  auto object = single_quad_object(7, false);
  for (auto &vertex : object.document.edit().vertices) {
    vertex.position = {vertex.position.x, 0.0F,
                       -0.125F + vertex.position.y * 0.025F};
  }
  const std::vector objects{object};
  const std::array bounds{pick_bounds_for_object(object)};
  const auto face_geometry = quader_editor_core::build_pick_face_geometry(objects);
  quader_editor_core::PickRequest request{
      .ray = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
      .options = quader_editor_core::selection_pick_options_from_radii(
          0.2, 0.2, false),
  };
  // The radius reaches this grazing quad behind the ray origin. A closest
  // ray point clamped to distance zero must not admit a negative-depth edge.
  EXPECT_TRUE(quader_editor_core::pick_mesh_depth_hits(objects, request).empty());
  EXPECT_TRUE(quader_editor_core::pick_mesh_depth_hits(
                  objects, request, bounds, face_geometry)
                  .empty());
  request.ray.direction = {0.0F, 0.0F, -1.0F};
  const auto forward_hits = quader_editor_core::pick_mesh_depth_hits(
      objects, request, bounds, face_geometry);
  ASSERT_EQ(forward_hits.size(), 1U);
  EXPECT_EQ(forward_hits[0].kind, quader_poly::ElementKind::Edge);
  EXPECT_NEAR(forward_hits[0].ray_distance, 0.1F, 0.000001F);
}

TEST(QdrModelingRuntimePickingTests,
     MeshDepthFallbackRotatedNonuniformBoundsMatchWorldSpacePicking) {
  auto object = single_quad_object(7, false);
  for (auto &vertex : object.document.edit().vertices) {
    vertex.position = {0.0F, vertex.position.x, vertex.position.y};
  }
  const std::vector objects{object};
  const auto face_geometry = quader_editor_core::build_pick_face_geometry(objects);
  const float diagonal = std::sqrt(0.5F);
  auto transform = scaled_pick_transform(7, 0.1F, 1.0F, 1.0F);
  // Rz(45 degrees) * Scale(.1, 1, 1), and its matching inverse.
  transform.world_from_object.values[0] = 0.1F * diagonal;
  transform.world_from_object.values[1] = 0.1F * diagonal;
  transform.world_from_object.values[4] = -diagonal;
  transform.world_from_object.values[5] = diagonal;
  transform.object_from_world.values[0] = 10.0F * diagonal;
  transform.object_from_world.values[1] = -diagonal;
  transform.object_from_world.values[4] = 10.0F * diagonal;
  transform.object_from_world.values[5] = diagonal;
  const std::array transforms{transform};
  const std::array bounds{quader_editor_core::PickObjectBounds{
      .object_id = 7,
      .empty = false,
      .min = {-diagonal, -diagonal, -1.0F},
      .max = {diagonal, diagonal, 1.0F},
  }};
  const quader_editor_core::PickRequest request{
      .ray = {{0.19F * diagonal, 0.19F * diagonal, -4.0F},
              {0.0F, 0.0F, 1.0F}},
      .options = quader_editor_core::selection_pick_options_from_radii(
          0.2, 0.2, false),
  };
  const auto cached_hits = quader_editor_core::pick_mesh_depth_hits(
      objects, request, bounds, face_geometry, transforms);
  ASSERT_EQ(cached_hits.size(), 1U);
  EXPECT_EQ(cached_hits[0].kind, quader_poly::ElementKind::Edge);
  EXPECT_NEAR(cached_hits[0].pick_distance, 0.19F, 0.000001F);
  EXPECT_FLOAT_EQ(cached_hits[0].ray_distance, 3.0F);
  // Local ray X is 1.9: max inverse-column length gives only 1.4213 of
  // padding, although the world-space boundary is inside the .2 radius.
  const auto uncached_hits = quader_editor_core::pick_mesh_depth_hits(
      objects, request, {}, face_geometry, transforms);
  ASSERT_EQ(uncached_hits.size(), 1U);
  EXPECT_EQ(uncached_hits[0].edge, cached_hits[0].edge);
  EXPECT_NEAR(uncached_hits[0].pick_distance, cached_hits[0].pick_distance,
              0.000001F);
  EXPECT_FLOAT_EQ(uncached_hits[0].ray_distance, cached_hits[0].ray_distance);
}

TEST(QdrModelingRuntimePickingTests,
     EdgeHoverPreservesThePickedContextFace) {
  const quader_editor_core::PolygonObject object =
      single_quad_object(7, false);
  ASSERT_EQ(object.document.faces.size(), 1U);
  quader_poly::PickOptions options;
  options.mode = quader_poly::SelectionMode::Edge;
  options.edge_radius = 0.1F;

  const quader_editor_core::SelectionHoverUpdateResult hover =
      quader_editor_core::update_selection_hover(
          {object}, quader_editor_core::SelectionMode::Edge, {},
          {{0.0F, 0.95F, 5.0F}, {0.0F, 0.0F, -1.0F}}, false, options);

  ASSERT_EQ(hover.hover.kind,
            quader_editor_core::SelectionHoverKind::Edge);
  EXPECT_EQ(hover.hover.object_id, object.id);
  EXPECT_EQ(hover.hover.face_id, object.document.faces.front().id);
}
