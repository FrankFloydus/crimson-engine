////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

namespace {

using quader_poly::Document;
using quader_poly::Edge;
using quader_poly::ElementId;

/// Authored tube whose component sets are explicit fixture data.
struct TraversalTubeFixture final {
  Document document;
  std::array<std::array<ElementId, 6>, 4> vertices{};
  std::array<std::array<ElementId, 6>, 3> faces{};

  TraversalTubeFixture() {
    for (std::size_t row = 0; row < vertices.size(); ++row) {
      for (std::size_t column = 0; column < vertices[row].size(); ++column) {
        const float angle = static_cast<float>(column) * std::numbers::pi_v<float> / 3.0f;
        vertices[row][column] = quader_poly::add_vertex(
            document, {std::cos(angle), std::sin(angle), static_cast<float>(row)});
      }
    }
    for (std::size_t row = 0; row < faces.size(); ++row) {
      for (std::size_t column = 0; column < faces[row].size(); ++column) {
        const auto next = (column + 1) % faces[row].size();
        const std::array corners{vertices[row][column], vertices[row][next],
                                 vertices[row + 1][next], vertices[row + 1][column]};
        faces[row][column] = quader_poly::add_face(document, corners);
      }
    }
  }
};

std::vector<Edge> sorted_edges(std::vector<Edge> edges) {
  for (auto &edge : edges) edge = quader_poly::make_edge(edge.a, edge.b);
  std::ranges::sort(edges, {}, [](Edge edge) { return std::pair{edge.a, edge.b}; });
  return edges;
}

std::vector<ElementId> sorted_ids(std::vector<ElementId> ids) {
  std::ranges::sort(ids);
  return ids;
}

void expect_tube_components(const TraversalTubeFixture &fixture) {
  std::vector<Edge> circumference;
  std::vector<Edge> ring;
  std::vector<Edge> boundary;
  for (std::size_t side = 0; side < 6; ++side) {
    const auto next = (side + 1) % 6;
    circumference.push_back(quader_poly::make_edge(fixture.vertices[1][side], fixture.vertices[1][next]));
    ring.push_back(quader_poly::make_edge(fixture.vertices[1][side], fixture.vertices[2][side]));
    boundary.push_back(quader_poly::make_edge(fixture.vertices[0][side], fixture.vertices[0][next]));
  }
  for (const auto seed : circumference) {
    EXPECT_EQ(sorted_edges(quader_poly::edge_loop_edges(fixture.document, seed)), sorted_edges(circumference));
    EXPECT_EQ(sorted_ids(quader_poly::vertex_loop_vertices(fixture.document, seed)),
              sorted_ids({fixture.vertices[1].begin(), fixture.vertices[1].end()}));
  }
  for (const auto seed : ring) {
    EXPECT_EQ(sorted_edges(quader_poly::edge_ring_edges(fixture.document, seed)), sorted_edges(ring));
  }
  EXPECT_EQ(sorted_edges(quader_poly::edge_boundary_component_edges(fixture.document, boundary.front())),
            sorted_edges(boundary));
  EXPECT_EQ(sorted_ids(quader_poly::face_loop_faces(fixture.document, ring.front(), fixture.faces[1][0])),
            sorted_ids({fixture.faces[1].begin(), fixture.faces[1].end()}));

  quader_poly::Selection prior;
  prior.mode = quader_poly::SelectionMode::Face;
  prior.faces = {fixture.faces[1][0]};
  prior.has_active = true;
  prior.active_kind = quader_poly::ElementKind::Face;
  prior.active_face = fixture.faces[1][0];
  const quader_poly::PickResult clicked{
      .hit = true, .kind = quader_poly::ElementKind::Face, .face_id = fixture.faces[1][1]};
  EXPECT_EQ(quader_poly::component_loop_seed(fixture.document, clicked, prior), ring[1]);
}

} // namespace

TEST(QdrDocumentOperationComponentTraversalTests, AuthoredConnectivityDeterminesSelectionRegardlessOfEmbedding) {
  for (const bool collapsed : {false, true}) {
    SCOPED_TRACE(collapsed);
    TraversalTubeFixture fixture;
    for (std::size_t index = 0; index < fixture.document.vertices.size(); ++index) {
      const float coordinate = static_cast<float>(index);
      fixture.document.vertices[index].position = collapsed
          ? quader::QVec3{0.0f, 0.0f, 0.0f}
          : quader::QVec3{coordinate * coordinate - 23.0f,
                          static_cast<float>(index % 3) * -7.0f,
                          std::sin(coordinate) * 13.0f};
    }
    expect_tube_components(fixture);
  }
}

TEST(QdrDocumentOperationComponentTraversalTests, SparseReversedStableIdsPreserveTheAuthoredComponentSets) {
  TraversalTubeFixture fixture;
  const auto remap_vertex = [](ElementId id) { return static_cast<ElementId>(1009 - 17 * id); };
  const auto remap_face = [](ElementId id) { return static_cast<ElementId>(3001 - 19 * id); };
  for (auto &vertex : fixture.document.vertices) vertex.id = remap_vertex(vertex.id);
  for (auto &face : fixture.document.faces) {
    face.id = remap_face(face.id);
    for (auto &id : face.vertices) id = remap_vertex(id);
  }
  for (auto &row : fixture.vertices) for (auto &id : row) id = remap_vertex(id);
  for (auto &row : fixture.faces) for (auto &id : row) id = remap_face(id);
  fixture.document.next_vertex_id = 1010;
  fixture.document.next_face_id = 3002;
  expect_tube_components(fixture);
}

TEST(QdrDocumentOperationComponentTraversalTests, StorageOrderAndFaceCornerOriginsDoNotChooseTraversalBranches) {
  TraversalTubeFixture fixture;
  std::ranges::reverse(fixture.document.vertices);
  std::ranges::reverse(fixture.document.faces);
  for (std::size_t index = 0; index < fixture.document.faces.size(); ++index) {
    auto &corners = fixture.document.faces[index].vertices;
    std::ranges::rotate(corners, corners.begin() + static_cast<std::ptrdiff_t>(index % corners.size()));
    if (index % 2 == 0) std::ranges::reverse(corners);
  }
  expect_tube_components(fixture);
}

TEST(QdrDocumentOperationComponentTraversalTests, UnrelatedSelectedComponentsDoNotEraseRepeatedClickDirection) {
  const TraversalTubeFixture fixture;
  quader_poly::Selection prior;
  prior.mode = quader_poly::SelectionMode::Face;
  prior.faces.assign(fixture.faces[1].begin(), fixture.faces[1].end());
  prior.faces.push_back(fixture.faces[0][3]);
  prior.has_active = true;
  prior.active_kind = quader_poly::ElementKind::Face;
  prior.active_face = fixture.faces[1][1];
  const quader_poly::PickResult clicked_face{
      .hit = true, .kind = quader_poly::ElementKind::Face, .face_id = fixture.faces[1][1]};
  const auto face_seed = quader_poly::component_loop_seed(
      fixture.document, clicked_face, prior,
      quader_poly::make_edge(fixture.vertices[1][1], fixture.vertices[1][2]));
  ASSERT_TRUE(face_seed.has_value());
  EXPECT_EQ(sorted_ids(quader_poly::face_loop_faces(fixture.document, *face_seed, clicked_face.face_id)),
            sorted_ids({fixture.faces[1].begin(), fixture.faces[1].end()}));

  prior = {};
  prior.mode = quader_poly::SelectionMode::Vertex;
  prior.vertices.assign(fixture.vertices[1].begin(), fixture.vertices[1].end());
  prior.vertices.push_back(fixture.vertices[3][4]);
  prior.has_active = true;
  prior.active_kind = quader_poly::ElementKind::Vertex;
  prior.active_vertex = fixture.vertices[1][1];
  const quader_poly::PickResult clicked_vertex{
      .hit = true, .kind = quader_poly::ElementKind::Vertex, .vertex_id = fixture.vertices[1][1]};
  const auto vertex_seed = quader_poly::component_loop_seed(
      fixture.document, clicked_vertex, prior,
      quader_poly::make_edge(fixture.vertices[1][1], fixture.vertices[2][1]));
  ASSERT_TRUE(vertex_seed.has_value());
  EXPECT_EQ(sorted_ids(quader_poly::vertex_loop_vertices(fixture.document, *vertex_seed)),
            sorted_ids({fixture.vertices[1].begin(), fixture.vertices[1].end()}));
}
