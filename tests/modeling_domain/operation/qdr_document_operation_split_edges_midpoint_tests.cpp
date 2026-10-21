////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>

namespace {

using namespace quader_poly;

void expect_unchanged(const Document &actual, const Document &source,
                      const Selection &selection, const Selection &before) {
  EXPECT_EQ(actual.next_vertex_id, source.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, source.next_face_id);
  EXPECT_EQ(actual.hard_normal_edges, source.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, source.soft_normal_edges);
  ASSERT_EQ(actual.vertices.size(), source.vertices.size());
  ASSERT_EQ(actual.faces.size(), source.faces.size());
  for (std::size_t index = 0u; index < source.vertices.size(); ++index) {
    EXPECT_EQ(actual.vertices[index].id, source.vertices[index].id);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x, source.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y, source.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z, source.vertices[index].position.z);
  }
  for (std::size_t index = 0u; index < source.faces.size(); ++index) {
    const auto &face = actual.faces[index];
    const auto &previous = source.faces[index];
    EXPECT_EQ(face.id, previous.id);
    EXPECT_EQ(face.vertices, previous.vertices);
    EXPECT_EQ(face.material_slot, previous.material_slot);
    EXPECT_EQ(face.normal_shading, previous.normal_shading);
    EXPECT_EQ(face.texture_transform, previous.texture_transform);
    EXPECT_EQ(face.circular_uv_interpolation, previous.circular_uv_interpolation);
    ASSERT_EQ(face.uvs.size(), previous.uvs.size());
    for (std::size_t corner = 0u; corner < face.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(face.uvs[corner].x, previous.uvs[corner].x);
      EXPECT_FLOAT_EQ(face.uvs[corner].y, previous.uvs[corner].y);
    }
  }
  EXPECT_EQ(selection.mode, before.mode);
  EXPECT_EQ(selection.vertices, before.vertices);
  EXPECT_EQ(selection.edges, before.edges);
  EXPECT_EQ(selection.faces, before.faces);
  EXPECT_EQ(selection.has_active, before.has_active);
  EXPECT_EQ(selection.active_kind, before.active_kind);
  EXPECT_EQ(selection.active_vertex, before.active_vertex);
  EXPECT_EQ(selection.active_edge, before.active_edge);
  EXPECT_EQ(selection.active_face, before.active_face);
}

TEST(QdrDocumentOperationSplitEdgesMidpointTests, BoundaryEdgeGetsOneMidpointAndTwoSelectedHalves) {
  auto fixture = qdr_test::make_quad_document(17u);
  const auto edge = make_edge(fixture.bottom_left, fixture.bottom_right);
  auto selection = qdr_test::edge_selection({edge}, edge);
  const auto result = split_selected_edges_midpoint(fixture.document, selection);
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  ASSERT_EQ(result.created.vertices.size(), 1u);
  const auto midpoint = result.created.vertices.front();
  const auto *vertex = find_vertex(fixture.document, midpoint);
  ASSERT_NE(vertex, nullptr);
  EXPECT_FLOAT_EQ(vertex->position.x, 0.0f);
  EXPECT_FLOAT_EQ(vertex->position.y, -1.0f);
  EXPECT_FLOAT_EQ(vertex->position.z, 0.0f);
  EXPECT_EQ(fixture.document.faces.size(), 1u);
  EXPECT_EQ(fixture.document.faces.front().vertices,
      (std::vector<ElementId>{fixture.bottom_left, midpoint, fixture.bottom_right,
                             fixture.top_right, fixture.top_left}));
  EXPECT_EQ(fixture.document.faces.front().material_slot, 17u);
  EXPECT_EQ(selection.mode, SelectionMode::Edge);
  EXPECT_EQ(selection.edges,
      (std::vector<Edge>{make_edge(edge.a, midpoint), make_edge(midpoint, edge.b)}));
  EXPECT_EQ(result.created.edges, selection.edges);
  EXPECT_EQ(result.deleted.edges, std::vector<Edge>{edge});
  EXPECT_TRUE(result.created.faces.empty());
  EXPECT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_edge, selection.edges.back());
  qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesMidpointTests, SharedMidpointKeepsSeparateCornerUvsAndNormalModes) {
  for (const bool hard : {false, true}) {
    SCOPED_TRACE(hard);
    auto fixture = qdr_test::make_quad_document(7u);
    const auto fifth = add_vertex(fixture.document, {3, -1, 0});
    const auto sixth = add_vertex(fixture.document, {3, 1, 0});
    const std::array loop{fixture.bottom_right, fifth, sixth, fixture.top_right};
    const auto second_id = add_face(fixture.document, loop, 11u);
    auto &left = fixture.document.faces.front();
    left.uvs = {{0, 0}, {2, 0}, {2, 4}, {0, 4}};
    left.normal_shading = SurfaceShading::Smooth;
    left.texture_transform = {.scale = {-2, 3}, .shift_uv = {4, 5}, .rotation_degrees = 17};
    auto &right = fixture.document.faces.back();
    right.uvs = {{10, 2}, {14, 2}, {14, 8}, {10, 8}};
    right.normal_shading = SurfaceShading::Flat;
    right.texture_transform = {.scale = {4, -5}, .shift_uv = {7, 8}, .rotation_degrees = 33};
    const auto edge = make_edge(fixture.bottom_right, fixture.top_right);
    auto &modes = hard ? fixture.document.hard_normal_edges : fixture.document.soft_normal_edges;
    modes.push_back(edge);
    const auto untouched = make_edge(fixture.top_left, fixture.bottom_left);
    fixture.document.hard_normal_edges.push_back(untouched);
    const auto source = fixture.document;
    auto selection = qdr_test::edge_selection({edge}, edge);
    const auto result = split_selected_edges_midpoint(fixture.document, selection);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.created.vertices.size(), 1u);
    const auto midpoint = result.created.vertices.front();
    EXPECT_EQ(qdr_test::edge_incidence_count(fixture.document, make_edge(edge.a, midpoint)), 2);
    EXPECT_EQ(qdr_test::edge_incidence_count(fixture.document, make_edge(midpoint, edge.b)), 2);
    EXPECT_EQ(fixture.document.faces.front().vertices,
        (std::vector<ElementId>{fixture.bottom_left, fixture.bottom_right, midpoint,
                               fixture.top_right, fixture.top_left}));
    EXPECT_EQ(fixture.document.faces.back().vertices,
        (std::vector<ElementId>{fixture.bottom_right, fifth, sixth, fixture.top_right, midpoint}));
    for (const auto &face : fixture.document.faces) {
      const auto *previous = find_face(source, face.id);
      ASSERT_NE(previous, nullptr);
      EXPECT_EQ(face.material_slot, previous->material_slot);
      EXPECT_EQ(face.normal_shading, previous->normal_shading);
      EXPECT_EQ(face.texture_transform, previous->texture_transform);
      ASSERT_EQ(face.uvs.size(), face.vertices.size());
      for (std::size_t corner = 0u; corner < face.vertices.size(); ++corner) {
        if (face.vertices[corner] == midpoint) {
          EXPECT_FLOAT_EQ(face.uvs[corner].x, face.id == second_id ? 10.0f : 2.0f);
          EXPECT_FLOAT_EQ(face.uvs[corner].y, face.id == second_id ? 5.0f : 2.0f);
        } else {
          const auto old_corner = std::ranges::find(previous->vertices, face.vertices[corner]);
          ASSERT_NE(old_corner, previous->vertices.end());
          const auto index = static_cast<std::size_t>(old_corner - previous->vertices.begin());
          EXPECT_FLOAT_EQ(face.uvs[corner].x, previous->uvs[index].x);
          EXPECT_FLOAT_EQ(face.uvs[corner].y, previous->uvs[index].y);
        }
      }
    }
    const auto &inherited = hard ? fixture.document.hard_normal_edges : fixture.document.soft_normal_edges;
    EXPECT_FALSE(qdr_test::contains_edge(inherited, edge));
    EXPECT_TRUE(qdr_test::contains_edge(inherited, make_edge(edge.a, midpoint)));
    EXPECT_TRUE(qdr_test::contains_edge(inherited, make_edge(midpoint, edge.b)));
    EXPECT_TRUE(qdr_test::contains_edge(fixture.document.hard_normal_edges, untouched));
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
  }
}

TEST(QdrDocumentOperationSplitEdgesMidpointTests, OddBranchingSelectionAndReversedDuplicatesSplitOncePerEdge) {
  auto document = make_cube({});
  const auto all_edges = document_edges(document);
  const auto common_vertex = document.vertices.front().id;
  std::vector<Edge> selected;
  std::ranges::copy_if(all_edges, std::back_inserter(selected), [common_vertex](const Edge edge) {
    return edge.a == common_vertex || edge.b == common_vertex;
  });
  ASSERT_EQ(selected.size(), 3u);
  Selection selection = qdr_test::edge_selection(
      {selected[0], selected[1], selected[2], Edge{selected[0].b, selected[0].a}, selected[1]}, selected[1]);
  const auto source = document;
  const auto result = split_selected_edges_midpoint(document, selection);
  ASSERT_TRUE(result.success) << result.message;
  EXPECT_EQ(result.created.vertices.size(), 3u);
  EXPECT_EQ(document.vertices.size(), source.vertices.size() + 3u);
  EXPECT_EQ(document.faces.size(), source.faces.size());
  EXPECT_EQ(selection.edges.size(), 6u);
  for (const auto edge : selected) {
    const auto *a = find_vertex(source, edge.a);
    const auto *b = find_vertex(source, edge.b);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    const auto midpoint = std::ranges::find_if(result.created.vertices, [&](ElementId id) {
      const auto point = find_vertex(document, id)->position;
      return point.x == std::midpoint(a->position.x, b->position.x) &&
             point.y == std::midpoint(a->position.y, b->position.y) &&
             point.z == std::midpoint(a->position.z, b->position.z);
    });
    ASSERT_NE(midpoint, result.created.vertices.end());
    EXPECT_TRUE(qdr_test::contains_edge(selection.edges, make_edge(edge.a, *midpoint)));
    EXPECT_TRUE(qdr_test::contains_edge(selection.edges, make_edge(*midpoint, edge.b)));
  }
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationSplitEdgesMidpointTests, InvalidMixedSelectionRejectsWithoutPartialMutation) {
  auto fixture = qdr_test::make_quad_document();
  const auto edge = make_edge(fixture.bottom_left, fixture.bottom_right);
  auto selection = qdr_test::edge_selection({edge, make_edge(fixture.bottom_left, fixture.top_right)}, edge);
  const auto source = fixture.document;
  const auto before = selection;
  const auto result = split_selected_edges_midpoint(fixture.document, selection);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(result.created.empty());
  expect_unchanged(fixture.document, source, selection, before);
}

TEST(QdrDocumentOperationSplitEdgesMidpointTests, InvalidChangedFaceRollsBackEarlierValidMidpoints) {
  auto fixture = qdr_test::make_quad_document();
  const auto first = add_vertex(fixture.document, {10, 0, 0});
  const auto second = add_vertex(fixture.document, {12, 0, 0});
  const auto third = add_vertex(fixture.document, {14, 0, 0});
  const std::array loop{first, second, third};
  const auto collapsed = add_face(fixture.document, loop);
  ASSERT_NE(collapsed, kInvalidElementId);
  const auto edge = make_edge(fixture.bottom_left, fixture.bottom_right);
  auto selection = qdr_test::edge_selection({edge, make_edge(first, second)}, edge);
  const auto source = fixture.document;
  const auto before = selection;
  const auto result = split_selected_edges_midpoint(fixture.document, selection);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.changed);
  expect_unchanged(fixture.document, source, selection, before);
}

} // namespace
