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

/// A broad grid gives both bounded open loops and unrelated intersecting loops.
struct TraversalGridFixture final {
  Document document;
  std::array<std::array<ElementId, 9>, 4> vertices{};
  std::array<std::array<ElementId, 8>, 3> faces{};

  TraversalGridFixture() {
    for (std::size_t row = 0; row < vertices.size(); ++row) {
      for (std::size_t column = 0; column < vertices[row].size(); ++column) {
        vertices[row][column] = quader_poly::add_vertex(
            document, {static_cast<float>(column), static_cast<float>(row), 0.0f});
      }
    }
    for (std::size_t row = 0; row < faces.size(); ++row) {
      for (std::size_t column = 0; column < faces[row].size(); ++column) {
        const std::array corners{vertices[row][column], vertices[row][column + 1],
                                 vertices[row + 1][column + 1], vertices[row + 1][column]};
        faces[row][column] = quader_poly::add_face(document, corners);
      }
    }
  }
};

/// Both strip axes close independently, with no boundary or cap shortcuts.
struct TraversalTorusFixture final {
  Document document;
  std::array<std::array<ElementId, 6>, 6> vertices{};
  std::array<std::array<ElementId, 6>, 6> faces{};

  explicit TraversalTorusFixture(bool twisted = false) {
    for (std::size_t row = 0; row < vertices.size(); ++row) {
      for (std::size_t column = 0; column < vertices[row].size(); ++column) {
        const float around = static_cast<float>(column) * std::numbers::pi_v<float> / 3.0f;
        const float profile = static_cast<float>(row) * std::numbers::pi_v<float> / 3.0f;
        vertices[row][column] = quader_poly::add_vertex(document,
            {(3.0f + std::cos(profile)) * std::cos(around),
             (3.0f + std::cos(profile)) * std::sin(around), std::sin(profile)});
      }
    }
    for (std::size_t row = 0; row < faces.size(); ++row) {
      for (std::size_t column = 0; column < faces[row].size(); ++column) {
        const auto next_row = (row + 1) % vertices.size();
        const auto next_column = (column + 1) % vertices[row].size();
        const std::size_t shift = twisted && next_row == 0 ? 1 : 0;
        const std::array corners{vertices[row][column], vertices[row][next_column],
                                 vertices[next_row][(next_column + shift) % vertices[row].size()],
                                 vertices[next_row][(column + shift) % vertices[row].size()]};
        faces[row][column] = quader_poly::add_face(document, corners);
      }
    }
  }
};

quader_poly::PickResult edge_pick(Edge edge) {
  return {.hit = true, .kind = quader_poly::ElementKind::Edge, .edge = edge};
}

quader_poly::PickResult face_pick(ElementId face) {
  return {.hit = true, .kind = quader_poly::ElementKind::Face, .face_id = face};
}

quader_poly::Selection edge_context(std::vector<Edge> edges) {
  quader_poly::Selection result;
  result.mode = quader_poly::SelectionMode::Edge;
  result.edges = std::move(edges);
  if (!result.edges.empty()) {
    result.has_active = true;
    result.active_kind = quader_poly::ElementKind::Edge;
    result.active_edge = result.edges.back();
  }
  return result;
}

quader_poly::Selection face_context(std::vector<ElementId> faces) {
  quader_poly::Selection result;
  result.mode = quader_poly::SelectionMode::Face;
  result.faces = std::move(faces);
  if (!result.faces.empty()) {
    result.has_active = true;
    result.active_kind = quader_poly::ElementKind::Face;
    result.active_face = result.faces.back();
  }
  return result;
}

std::vector<Edge> tube_edges(const TraversalTubeFixture& fixture, std::size_t row) {
  std::vector<Edge> result;
  for (std::size_t column = 0; column < fixture.vertices[row].size(); ++column) {
    result.push_back(quader_poly::make_edge(fixture.vertices[row][column],
        fixture.vertices[row][(column + 1) % fixture.vertices[row].size()]));
  }
  return result;
}

std::vector<Edge> grid_edges(const TraversalGridFixture& fixture, std::size_t row) {
  std::vector<Edge> result;
  for (std::size_t column = 0; column + 1 < fixture.vertices[row].size(); ++column) {
    result.push_back(quader_poly::make_edge(fixture.vertices[row][column], fixture.vertices[row][column + 1]));
  }
  return result;
}

void permute_storage(Document& document) {
  std::ranges::reverse(document.vertices);
  std::ranges::reverse(document.faces);
  for (std::size_t index = 0; index < document.faces.size(); ++index) {
    auto& corners = document.faces[index].vertices;
    std::ranges::rotate(corners, corners.begin() + static_cast<std::ptrdiff_t>(index % corners.size()));
    if (index % 2 == 0) std::ranges::reverse(corners);
  }
}

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

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickEdgesSelectsOneLoopInsteadOfConnectedShell) {
  for (const bool capped : {false, true}) {
    TraversalTubeFixture fixture;
    if (capped) {
      quader_poly::add_face(fixture.document, fixture.vertices.front());
      quader_poly::add_face(fixture.document, fixture.vertices.back());
    }
    for (std::size_t row = 0; row < fixture.vertices.size(); ++row) {
      const auto expected = tube_edges(fixture, row);
      for (const Edge edge : expected) {
        const auto result = quader_poly::component_double_click_selection(fixture.document, edge_pick(edge), {});
        EXPECT_EQ(result.mode, quader_poly::SelectionMode::Edge);
        EXPECT_EQ(sorted_edges(result.edges), sorted_edges(expected));
        EXPECT_TRUE(result.vertices.empty());
        EXPECT_TRUE(result.faces.empty());
        EXPECT_TRUE(result.has_active);
        EXPECT_EQ(result.active_kind, quader_poly::ElementKind::Edge);
        EXPECT_EQ(result.active_edge, edge);
      }
    }
  }
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickOpenLoopBoundsAllSeparatedSeedsAndIgnoresCrossingLoop) {
  const TraversalGridFixture fixture;
  const auto edges = grid_edges(fixture, 1);
  const Edge unrelated = quader_poly::make_edge(fixture.vertices[1][4], fixture.vertices[2][4]);
  const auto prior = edge_context({edges[1], edges[5], unrelated});
  const auto result = quader_poly::component_double_click_selection(fixture.document, edge_pick(edges[3]), prior);
  EXPECT_EQ(sorted_edges(result.edges), sorted_edges({edges[1], edges[2], edges[3], edges[4], edges[5]}));
  EXPECT_EQ(result.active_edge, edges[3]);
  EXPECT_EQ(prior.edges, (std::vector<Edge>{edges[1], edges[5], unrelated}));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickOpenLoopUsesEveryEndpointAndNeverWraps) {
  const TraversalGridFixture fixture;
  const auto edges = grid_edges(fixture, 1);
  for (std::size_t start = 0; start < edges.size(); ++start) {
    for (std::size_t finish = 0; finish < edges.size(); ++finish) {
      const auto result = quader_poly::component_double_click_selection(
          fixture.document, edge_pick(edges[finish]), edge_context({edges[start]}));
      const auto first = std::min(start, finish);
      const auto last = std::max(start, finish);
      const std::vector<Edge> expected = last - first < 2
          ? edges
          : std::vector<Edge>{edges.begin() + static_cast<std::ptrdiff_t>(first),
                              edges.begin() + static_cast<std::ptrdiff_t>(last + 1)};
      EXPECT_EQ(sorted_edges(result.edges), sorted_edges(expected)) << start << " -> " << finish;
    }
  }
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickConnectedSeedRunsExpandWholeOpenOrClosedLoop) {
  const TraversalGridFixture grid;
  const TraversalTubeFixture tube;
  const auto open_edges = grid_edges(grid, 1);
  const auto closed_edges = tube_edges(tube, 1);
  for (const auto& [document, edges] :
       {std::pair{&grid.document, open_edges}, std::pair{&tube.document, closed_edges}}) {
    for (const auto& selected : {std::vector<Edge>{edges[1]}, std::vector<Edge>{edges[0], edges[1]}, edges}) {
      const auto result = quader_poly::component_double_click_selection(*document, edge_pick(edges[2]),
                                                                       edge_context(selected));
      EXPECT_EQ(sorted_edges(result.edges), sorted_edges(edges));
    }
  }
  const auto wrap = quader_poly::component_double_click_selection(tube.document, edge_pick(closed_edges[0]),
      edge_context({closed_edges[4], closed_edges[5]}));
  EXPECT_EQ(sorted_edges(wrap.edges), sorted_edges(closed_edges));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickClosedLoopChoosesMinimumElementIntervalAndStableTie) {
  const TraversalTubeFixture fixture;
  const auto edges = tube_edges(fixture, 1);
  struct IntervalCase {
    std::vector<Edge> prior;
    Edge clicked;
    std::vector<Edge> expected;
  };
  const std::array cases{
      IntervalCase{{edges[0]}, edges[2], {edges[0], edges[1], edges[2]}},
      IntervalCase{{edges[5]}, edges[1], {edges[5], edges[0], edges[1]}},
      // Stable edge ordering walks 0,5,4,3,2,1. Equal gaps retain start 0.
      IntervalCase{{edges[0]}, edges[3], {edges[0], edges[5], edges[4], edges[3]}},
      IntervalCase{{edges[0], edges[2]}, edges[4], {edges[0], edges[5], edges[4], edges[3], edges[2]}},
      IntervalCase{{edges[0], edges[1], edges[2]}, edges[4],
                   {edges[0], edges[1], edges[2], edges[5], edges[4]}}};
  for (const auto& entry : cases) {
    const auto result = quader_poly::component_double_click_selection(
        fixture.document, edge_pick(entry.clicked), edge_context(entry.prior));
    EXPECT_EQ(sorted_edges(result.edges), sorted_edges(entry.expected));
    EXPECT_EQ(result.active_edge, entry.clicked);
  }
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickIgnoresDuplicateStaleReversedAndOtherModeEdgeSeeds) {
  const TraversalTubeFixture fixture;
  const auto edges = tube_edges(fixture, 1);
  const Edge reversed{edges[0].b, edges[0].a};
  auto prior = edge_context({reversed, edges[0], {9001, 9002}, tube_edges(fixture, 2)[3]});
  const auto result = quader_poly::component_double_click_selection(
      fixture.document, edge_pick({edges[2].b, edges[2].a}), prior);
  EXPECT_EQ(sorted_edges(result.edges), sorted_edges({edges[0], edges[1], edges[2]}));
  EXPECT_EQ(result.active_edge, edges[2]);
  prior.mode = quader_poly::SelectionMode::Face;
  EXPECT_EQ(sorted_edges(quader_poly::component_double_click_selection(
                fixture.document, edge_pick(edges[2]), prior).edges), sorted_edges(edges));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickFaceAdjacentActiveGuidesBothCylinderStripDirections) {
  const TraversalTubeFixture fixture;
  for (std::size_t column = 0; column < fixture.faces[1].size(); ++column) {
    const auto result = quader_poly::component_double_click_selection(fixture.document,
        face_pick(fixture.faces[1][column]),
        face_context({fixture.faces[1][(column + 1) % fixture.faces[1].size()]}));
    EXPECT_EQ(sorted_ids(result.faces), sorted_ids({fixture.faces[1].begin(), fixture.faces[1].end()}));
    EXPECT_EQ(result.active_face, fixture.faces[1][column]);
    EXPECT_EQ(result.active_kind, quader_poly::ElementKind::Face);
    EXPECT_TRUE(result.has_active);
    EXPECT_TRUE(result.vertices.empty());
    EXPECT_TRUE(result.edges.empty());
    const auto vertical = quader_poly::component_double_click_selection(fixture.document,
        face_pick(fixture.faces[1][column]), face_context({fixture.faces[0][column]}));
    EXPECT_EQ(sorted_ids(vertical.faces),
              sorted_ids({fixture.faces[0][column], fixture.faces[1][column], fixture.faces[2][column]}));
  }
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickFaceDistantActiveBoundsOpenAndClosedStrip) {
  const TraversalGridFixture grid;
  const auto open = quader_poly::component_double_click_selection(grid.document, face_pick(grid.faces[1][5]),
      face_context({grid.faces[1][1]}));
  EXPECT_EQ(sorted_ids(open.faces),
            sorted_ids({grid.faces[1][1], grid.faces[1][2], grid.faces[1][3], grid.faces[1][4], grid.faces[1][5]}));
  const TraversalTubeFixture tube;
  const auto closed = quader_poly::component_double_click_selection(tube.document, face_pick(tube.faces[1][2]),
      face_context({tube.faces[2][5], tube.faces[1][0]}));
  EXPECT_EQ(sorted_ids(closed.faces), sorted_ids({tube.faces[1][0], tube.faces[1][1], tube.faces[1][2]}));
  const auto wrapped = quader_poly::component_double_click_selection(tube.document, face_pick(tube.faces[1][1]),
      face_context({tube.faces[1][5]}));
  EXPECT_EQ(sorted_ids(wrapped.faces), sorted_ids({tube.faces[1][5], tube.faces[1][0], tube.faces[1][1]}));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickFaceUsesAllInStripSeedsAndLastDistinctSelection) {
  const TraversalGridFixture fixture;
  auto prior = face_context({fixture.faces[1][1], fixture.faces[0][4], fixture.faces[1][6], fixture.faces[1][3]});
  const auto result = quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][3]), prior);
  EXPECT_EQ(sorted_ids(result.faces), sorted_ids({fixture.faces[1][1], fixture.faces[1][2], fixture.faces[1][3],
                                                fixture.faces[1][4], fixture.faces[1][5], fixture.faces[1][6]}));
  prior = face_context({fixture.faces[1][0], fixture.faces[1][1]});
  const auto repeat = quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][1]), prior);
  EXPECT_EQ(sorted_ids(repeat.faces), sorted_ids({fixture.faces[1].begin(), fixture.faces[1].end()}));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickFaceAbsentOrInvalidGuidanceFallsBackToConnectedFaces) {
  const TraversalTubeFixture fixture;
  const auto picked = face_pick(fixture.faces[1][0]);
  const auto expected = quader_poly::connected_component_selection(fixture.document, picked);
  auto unselected_active = face_context({fixture.faces[1][1]});
  unselected_active.active_face = fixture.faces[1][2];
  auto wrong_kind = face_context({fixture.faces[1][1]});
  wrong_kind.active_kind = quader_poly::ElementKind::Edge;
  auto wrong_mode = face_context({fixture.faces[1][1]});
  wrong_mode.mode = quader_poly::SelectionMode::Edge;
  auto no_active = face_context({fixture.faces[1][1]});
  no_active.has_active = false;
  for (const auto& prior : {quader_poly::Selection{}, face_context({9001}),
                            face_context({fixture.faces[1][0]}), unselected_active,
                            wrong_kind, wrong_mode, no_active,
                            face_context({fixture.faces[0][2]})}) {
    const auto result = quader_poly::component_double_click_selection(fixture.document, picked, prior);
    EXPECT_EQ(sorted_ids(result.faces), sorted_ids(expected.faces));
    EXPECT_EQ(result.active_face, picked.face_id);
  }
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickFaceOppositeCubeFacesRejectAmbiguousStrip) {
  Document document;
  std::array<ElementId, 8> vertices{};
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(document,
        {static_cast<float>(index & 1U), static_cast<float>((index >> 1U) & 1U),
         static_cast<float>((index >> 2U) & 1U)});
  }
  const std::array<std::array<std::size_t, 4>, 6> corners{{
      {0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
      {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}};
  std::vector<ElementId> faces;
  for (const auto& indices : corners) {
    const std::array face_vertices{vertices[indices[0]], vertices[indices[1]], vertices[indices[2]], vertices[indices[3]]};
    faces.push_back(quader_poly::add_face(document, face_vertices));
  }
  const auto result = quader_poly::component_double_click_selection(document, face_pick(faces[1]), face_context({faces[0]}));
  EXPECT_EQ(sorted_ids(result.faces), sorted_ids(faces));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickTorusKeepsIndependentClosedAxesAndBoundsEach) {
  const TraversalTorusFixture fixture;
  std::vector<Edge> row_edges;
  std::vector<Edge> column_edges;
  std::vector<ElementId> column_faces;
  for (std::size_t index = 0; index < 6; ++index) {
    row_edges.push_back(quader_poly::make_edge(fixture.vertices[1][index], fixture.vertices[1][(index + 1) % 6]));
    column_edges.push_back(quader_poly::make_edge(fixture.vertices[index][1], fixture.vertices[(index + 1) % 6][1]));
    column_faces.push_back(fixture.faces[index][1]);
  }
  for (const auto& edges : {row_edges, column_edges}) {
    EXPECT_EQ(sorted_edges(quader_poly::component_double_click_selection(
                  fixture.document, edge_pick(edges[0]), {}).edges), sorted_edges(edges));
    EXPECT_EQ(sorted_edges(quader_poly::component_double_click_selection(
                  fixture.document, edge_pick(edges[2]), edge_context({edges[0]})).edges),
              sorted_edges({edges[0], edges[1], edges[2]}));
  }
  EXPECT_EQ(sorted_ids(quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][1]),
                face_context({fixture.faces[1][0]})).faces),
            sorted_ids({fixture.faces[1].begin(), fixture.faces[1].end()}));
  EXPECT_EQ(sorted_ids(quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][1]),
                face_context({fixture.faces[0][1]})).faces), sorted_ids(column_faces));
  EXPECT_EQ(sorted_ids(quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[2][1]),
                face_context({fixture.faces[0][1]})).faces),
            sorted_ids({fixture.faces[0][1], fixture.faces[1][1], fixture.faces[2][1]}));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickIntervalsIgnoreEmbeddingStorageCornersAndWinding) {
  TraversalTubeFixture fixture;
  const auto edges = tube_edges(fixture, 1);
  const auto edge_result = quader_poly::component_double_click_selection(fixture.document, edge_pick(edges[3]),
                                                                       edge_context({edges[0]}));
  const auto face_result = quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][3]),
                                                                       face_context({fixture.faces[1][0]}));
  EXPECT_EQ(sorted_ids(face_result.faces),
            sorted_ids({fixture.faces[1][0], fixture.faces[1][1], fixture.faces[1][2], fixture.faces[1][3]}));
  permute_storage(fixture.document);
  for (auto& vertex : fixture.document.vertices) vertex.position = {};
  EXPECT_EQ(quader_poly::component_double_click_selection(fixture.document, edge_pick(edges[3]),
                edge_context({edges[0]})).edges, edge_result.edges);
  EXPECT_EQ(quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][3]),
                face_context({fixture.faces[1][0]})).faces, face_result.faces);
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickAdjacentFaceDirectionWinsOverDistantStripIntersection) {
  const TraversalTorusFixture fixture(true);
  const auto result = quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][1]),
      face_context({fixture.faces[1][0]}));
  EXPECT_EQ(sorted_ids(result.faces), sorted_ids({fixture.faces[1].begin(), fixture.faces[1].end()}));
  const auto ambiguous = quader_poly::component_double_click_selection(fixture.document, face_pick(fixture.faces[1][2]),
      face_context({fixture.faces[1][0]}));
  std::vector<ElementId> all_faces;
  for (const auto& row : fixture.faces) all_faces.insert(all_faces.end(), row.begin(), row.end());
  EXPECT_EQ(sorted_ids(ambiguous.faces), sorted_ids(all_faces));
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickVertexUsesAuthoredConnectivityAndKeepsCoincidentShellsSeparate) {
  TraversalTubeFixture fixture;
  const auto detached = quader_poly::add_vertex(fixture.document, fixture.document.vertices.front().position);
  const auto isolated = quader_poly::component_double_click_selection(fixture.document,
      {.hit = true, .kind = quader_poly::ElementKind::Vertex, .vertex_id = detached}, {});
  EXPECT_EQ(isolated.vertices, (std::vector<ElementId>{detached}));
  for (auto& vertex : fixture.document.vertices) vertex.position = {};
  const auto result = quader_poly::component_double_click_selection(fixture.document,
      {.hit = true, .kind = quader_poly::ElementKind::Vertex, .vertex_id = fixture.vertices[1][2]}, {});
  std::vector<ElementId> expected;
  for (const auto& row : fixture.vertices) expected.insert(expected.end(), row.begin(), row.end());
  EXPECT_EQ(sorted_ids(result.vertices), sorted_ids(expected));
  EXPECT_EQ(result.active_kind, quader_poly::ElementKind::Vertex);
  EXPECT_EQ(result.active_vertex, fixture.vertices[1][2]);
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickInvalidTypedPicksReturnEmptyWithoutAnActiveComponent) {
  const TraversalTubeFixture fixture;
  for (const auto& picked : {quader_poly::PickResult{}, edge_pick({9001, 9002}),
                             edge_pick({fixture.vertices[0][0], fixture.vertices[0][2]}),
                             face_pick(9001),
                             quader_poly::PickResult{.hit = true, .kind = quader_poly::ElementKind::Vertex,
                                                     .vertex_id = 9001}}) {
    const auto result = quader_poly::component_double_click_selection(fixture.document, picked, {});
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(result.has_active);
  }
  auto duplicate_vertex = fixture.document;
  duplicate_vertex.vertices.push_back(duplicate_vertex.vertices.front());
  EXPECT_TRUE(quader_poly::component_double_click_selection(duplicate_vertex,
      {.hit = true, .kind = quader_poly::ElementKind::Vertex, .vertex_id = fixture.vertices[0][0]}, {}).empty());
  auto duplicate_face = fixture.document;
  duplicate_face.faces.push_back(duplicate_face.faces.front());
  EXPECT_TRUE(quader_poly::component_double_click_selection(duplicate_face,
      face_pick(fixture.faces[0][0]), {}).empty());
}

TEST(QdrDocumentOperationComponentTraversalTests, DoubleClickEdgeNonmanifoldIncidenceDoesNotManufactureAnotherRoute) {
  TraversalGridFixture fixture;
  const auto edges = grid_edges(fixture, 1);
  const auto extra = quader_poly::add_vertex(fixture.document, {3.5f, 1.0f, 1.0f});
  const std::array corners{edges[3].a, edges[3].b, extra};
  quader_poly::add_face(fixture.document, corners);
  const auto result = quader_poly::component_double_click_selection(fixture.document, edge_pick(edges[3]),
      edge_context({edges[0], edges[7]}));
  EXPECT_EQ(result.edges, (std::vector<Edge>{edges[3]}));
  EXPECT_EQ(result.active_edge, edges[3]);
}
