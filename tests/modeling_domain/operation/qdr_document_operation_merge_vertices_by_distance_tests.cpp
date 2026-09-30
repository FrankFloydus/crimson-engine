////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"


#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

using namespace quader_poly;

namespace {

/**
 * Stores the Two Triangle Fixture data contract used by the automated test
 * suite.
 */
struct TwoTriangleFixture {
  Document document;
  ElementId a = kInvalidElementId;
  ElementId b = kInvalidElementId;
  ElementId c = kInvalidElementId;
  ElementId d = kInvalidElementId;
  ElementId e = kInvalidElementId;
  ElementId f = kInvalidElementId;
};

TwoTriangleFixture make_two_triangle_fixture(float duplicate_offset = 0.1F) {
  TwoTriangleFixture fixture;
  fixture.a = add_vertex(fixture.document, {0.0F, 0.0F, 0.0F});
  fixture.b = add_vertex(fixture.document, {1.0F, 0.0F, 0.0F});
  fixture.c = add_vertex(fixture.document, {0.0F, 1.0F, 0.0F});
  fixture.d =
      add_vertex(fixture.document, {0.0F, 1.0F + duplicate_offset, 0.0F});
  fixture.e = add_vertex(fixture.document, {-1.0F, 1.0F, 0.0F});
  fixture.f = add_vertex(fixture.document, {-1.0F, 0.0F, 0.0F});
  EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 3>{
                                             fixture.a, fixture.b,
                                             fixture.c}) != kInvalidElementId);
  EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 3>{
                                             fixture.d, fixture.e,
                                             fixture.f}) != kInvalidElementId);
  return fixture;
}

Document
make_touching_cube_pair_document(std::vector<ElementId> &first_interface,
                                 std::vector<ElementId> &second_interface) {
  Document first = make_cube({});
  Document second = make_cube({});
  for (Vertex &vertex : first.vertices) {
    if (qdr_test::nearly_equal(vertex.position.x, 1.0F)) {
      first_interface.push_back(vertex.id);
    }
  }
  for (Vertex &vertex : second.vertices) {
    vertex.id += 1000U;
    vertex.position.x += 2.0F;
    if (qdr_test::nearly_equal(vertex.position.x, 1.0F)) {
      second_interface.push_back(vertex.id);
    }
  }
  for (Face &face : second.faces) {
    face.id += 1000U;
    for (ElementId &vertex_id : face.vertices) {
      vertex_id += 1000U;
    }
  }
  first.vertices.insert(first.vertices.end(), second.vertices.begin(),
                        second.vertices.end());
  first.faces.insert(first.faces.end(), second.faces.begin(),
                     second.faces.end());
  first.next_vertex_id = 2000U;
  first.next_face_id = 2000U;
  return first;
}

std::size_t face_count_on_plane_x(const Document &document, float x) {
  return static_cast<std::size_t>(std::count_if(
      document.faces.begin(), document.faces.end(),
      [&document, x](const Face &face) {
        return std::all_of(
            face.vertices.begin(), face.vertices.end(),
            [&document, x](ElementId vertex_id) {
              const Vertex *vertex = find_vertex(document, vertex_id);
              return vertex != nullptr &&
                     qdr_test::nearly_equal(vertex->position.x, x);
            });
      }));
}

float compiled_primitive_area(const CompiledMesh &mesh,
                              std::size_t primitive_index) {
  if (primitive_index >= mesh.primitives.size()) {
    return 0.0F;
  }
  const CompiledPrimitive &primitive = mesh.primitives[primitive_index];
  float area = 0.0F;
  for (std::uint32_t offset = 0; offset + 2U < primitive.index_count;
       offset += 3U) {
    const std::uint32_t first_index =
        mesh.indices[primitive.index_offset + offset];
    const std::uint32_t second_index =
        mesh.indices[primitive.index_offset + offset + 1U];
    const std::uint32_t third_index =
        mesh.indices[primitive.index_offset + offset + 2U];
    const quader::QVec3 first = mesh.vertices[first_index].position;
    const quader::QVec3 second = mesh.vertices[second_index].position;
    const quader::QVec3 third = mesh.vertices[third_index].position;
    const quader::QVec3 cross_product = cross(second - first, third - first);
    area += 0.5F * std::sqrt(length_squared(cross_product));
  }
  return area;
}

// Keep the previous seed-radius scan independent of the production lookup.
// Loose vertices isolate clustering, float arithmetic and selection publication.
OperationResult reference_scan_distance_merge(Document& document,
                                              Selection& selection,
                                              float tolerance) {
  constexpr float kReferenceEpsilon = 0.000001F;
  std::vector<Vertex> selected;
  for (const ElementId id : selection.vertices) {
    if (id == kInvalidElementId ||
        std::any_of(selected.begin(), selected.end(),
                    [id](const Vertex& vertex) { return vertex.id == id; })) {
      continue;
    }
    const Vertex* vertex = find_vertex(document, id);
    if (vertex != nullptr) {
      selected.push_back(*vertex);
    }
  }
  if (selected.size() < 2U) {
    return OperationResult::failure("Reference scan needs two selected vertices.");
  }

  const float tolerance_squared = tolerance * tolerance;
  const float boundary_epsilon =
      std::max(kReferenceEpsilon, tolerance_squared * 0.00001F);
  std::vector<bool> assigned(selected.size(), false);
  std::vector<std::vector<std::size_t>> clusters;
  for (std::size_t seed = 0; seed < selected.size(); ++seed) {
    if (assigned[seed]) {
      continue;
    }
    std::vector<std::size_t> cluster{seed};
    for (std::size_t candidate = seed + 1U; candidate < selected.size(); ++candidate) {
      if (!assigned[candidate] &&
          length_squared(selected[candidate].position - selected[seed].position) <=
              tolerance_squared + boundary_epsilon) {
        cluster.push_back(candidate);
      }
    }
    assigned[seed] = true;
    if (cluster.size() >= 2U) {
      for (const std::size_t index : cluster) {
        assigned[index] = true;
      }
      clusters.push_back(std::move(cluster));
    }
  }
  if (clusters.empty()) {
    return {false, {}};
  }

  Document candidate = document;
  std::vector<ElementId> selected_after_merge;
  for (const Vertex& vertex : selected) {
    selected_after_merge.push_back(vertex.id);
  }
  ElementId first_survivor = kInvalidElementId;
  for (const auto& cluster : clusters) {
    quader::QVec3 centroid;
    for (const std::size_t index : cluster) {
      centroid += selected[index].position;
    }
    centroid = centroid / static_cast<float>(cluster.size());

    std::size_t survivor = cluster.front();
    if (cluster.size() != 2U) {
      float nearest = length_squared(selected[survivor].position - centroid);
      for (const std::size_t index : cluster) {
        const float distance = length_squared(selected[index].position - centroid);
        if (distance + kReferenceEpsilon < nearest ||
            (std::abs(distance - nearest) <= kReferenceEpsilon && index < survivor)) {
          survivor = index;
          nearest = distance;
        }
      }
    }
    if (!std::isfinite(centroid.x) || !std::isfinite(centroid.y) ||
        !std::isfinite(centroid.z)) {
      return OperationResult::failure("Reference scan produced a nonfinite centroid.");
    }
    const ElementId survivor_id = selected[survivor].id;
    find_vertex(candidate, survivor_id)->position = centroid;
    for (const std::size_t index : cluster) {
      const ElementId removed_id = selected[index].id;
      if (removed_id == survivor_id) {
        continue;
      }
      std::erase_if(candidate.vertices,
                    [removed_id](const Vertex& vertex) { return vertex.id == removed_id; });
      std::erase(selected_after_merge, removed_id);
    }
    if (first_survivor == kInvalidElementId) {
      first_survivor = survivor_id;
    }
  }

  document = std::move(candidate);
  selection.clear();
  selection.mode = SelectionMode::Vertex;
  selection.vertices = std::move(selected_after_merge);
  selection.has_active = true;
  selection.active_kind = ElementKind::Vertex;
  selection.active_vertex = first_survivor;
  return {true, {}};
}

void expect_distance_scan_parity(std::span<const quader::QVec3> positions,
                                 float tolerance,
                                 std::span<const std::size_t> selection_order = {},
                                 bool include_stale_ids = false) {
  Document document;
  std::vector<ElementId> ids;
  for (const auto position : positions) {
    ids.push_back(add_vertex(document, position));
  }
  // Coincident unselected geometry must remain outside the selected clusters.
  static_cast<void>(add_vertex(document, positions.front()));
  document.next_vertex_id += 7U;
  document.next_face_id = 13U;
  Selection selection;
  selection.mode = SelectionMode::Vertex;
  if (selection_order.empty()) {
    selection.vertices = ids;
  } else {
    for (const std::size_t index : selection_order) {
      selection.vertices.push_back(ids[index]);
    }
  }
  if (include_stale_ids) {
    selection.vertices.insert(selection.vertices.begin(), kInvalidElementId);
    selection.vertices.push_back(ids.front());
    selection.vertices.push_back(document.next_vertex_id + 3U);
  }
  selection.has_active = true;
  selection.active_kind = ElementKind::Vertex;
  selection.active_vertex = selection.vertices.back();
  Document expected = document;
  Selection expected_selection = selection;
  const auto expected_result =
      reference_scan_distance_merge(expected, expected_selection, tolerance);

  const auto result = merge_selected_vertices_by_distance(document, selection, tolerance);

  EXPECT_EQ(result.success, expected_result.success) << result.message;
  EXPECT_EQ(result.changed, expected_result.changed) << result.message;
  EXPECT_EQ(qdr_test::document_fingerprint(document), qdr_test::document_fingerprint(expected));
  EXPECT_EQ(qdr_test::selection_fingerprint(selection),
            qdr_test::selection_fingerprint(expected_selection));
}

} // namespace

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     SpatialLookupMatchesOriginalScanAtInclusiveAndZeroRadiusBoundaries) {
  const float infinity = std::numeric_limits<float>::infinity();
  const float radius_boundary = std::sqrt(1.0F + 0.00001F);
  const std::array boundary_positions{
      quader::QVec3{0.0F, 0.0F, 0.0F}, quader::QVec3{1.0F, 0.0F, 0.0F},
      quader::QVec3{std::nextafter(1.0F, infinity), 0.0F, 0.0F},
      quader::QVec3{radius_boundary, 0.0F, 0.0F},
      quader::QVec3{std::nextafter(radius_boundary, infinity), 0.0F, 0.0F},
      quader::QVec3{-2.0F, -3.0F, 1.0F}, quader::QVec3{-3.0F, -3.0F, 1.0F}};
  expect_distance_scan_parity(boundary_positions, 1.0F);
  expect_distance_scan_parity(boundary_positions, 1.0F,
                            std::array<std::size_t, 7>{6, 5, 4, 3, 2, 1, 0}, true);

  const float zero_boundary = std::sqrt(0.000001F);
  const std::array zero_positions{
      quader::QVec3{0.0F, 0.0F, 0.0F}, quader::QVec3{0.0F, -0.0F, 0.0F},
      quader::QVec3{std::nextafter(zero_boundary, 0.0F), 0.0F, 0.0F},
      quader::QVec3{zero_boundary, 0.0F, 0.0F},
      quader::QVec3{std::nextafter(zero_boundary, infinity), 0.0F, 0.0F},
      quader::QVec3{-2.0F, -2.0F, -2.0F}, quader::QVec3{-2.0F, -2.0F, -2.0F}};
  expect_distance_scan_parity(zero_positions, 0.0F, {}, true);

  const std::array original_position_chain{
      quader::QVec3{0.0F, 0.0F, 0.0F}, quader::QVec3{0.75F, 0.0F, 0.0F},
      quader::QVec3{1.5F, 0.0F, 0.0F}, quader::QVec3{2.25F, 0.0F, 0.0F}};
  expect_distance_scan_parity(original_position_chain, 1.0F);
  expect_distance_scan_parity(original_position_chain, 1.0F,
                            std::array<std::size_t, 4>{3, 2, 1, 0});
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     SpatialLookupMatchesOriginalScanForDenseSparseAndReorderedSelections) {
  constexpr std::size_t kPositionCount = 96U;
  std::vector<quader::QVec3> dense;
  std::vector<quader::QVec3> sparse;
  std::vector<std::size_t> order(kPositionCount);
  std::iota(order.begin(), order.end(), 0U);
  std::uint32_t state = 0x31415926U;
  for (std::size_t index = 0; index < kPositionCount; ++index) {
    const auto coordinate = [&state]() {
      state = state * 1664525U + 1013904223U;
      return static_cast<float>(static_cast<int>(state % 257U) - 128) * 0.001F;
    };
    dense.push_back({coordinate(), coordinate(), coordinate()});
    sparse.push_back({-static_cast<float>(index) * 4.0F,
                      static_cast<float>(index % 3U) * 6.0F, -1.0F});
  }
  for (const float tolerance : {0.0F, 0.01F, 0.1F, 1.0F}) {
    SCOPED_TRACE(tolerance);
    expect_distance_scan_parity(dense, tolerance, {}, true);
    expect_distance_scan_parity(sparse, tolerance, {}, true);
  }
  std::reverse(order.begin(), order.end());
  expect_distance_scan_parity(dense, 0.1F, order, true);
  std::rotate(order.begin(), order.begin() + 31U, order.end());
  expect_distance_scan_parity(dense, 0.1F, order, true);
  expect_distance_scan_parity(sparse, 0.1F, order, true);
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     SpatialLookupMatchesOriginalScanForFiniteExtremeArithmetic) {
  const float maximum = std::numeric_limits<float>::max();
  const float minimum = std::numeric_limits<float>::denorm_min();
  const std::array overflow_distance{
      quader::QVec3{maximum, 0.0F, 0.0F}, quader::QVec3{-maximum, 0.0F, 0.0F},
      quader::QVec3{0.0F, 0.0F, 0.0F}};
  expect_distance_scan_parity(overflow_distance, maximum);
  expect_distance_scan_parity(overflow_distance, 0.0F, {}, true);
  const std::array overflow_centroid{
      quader::QVec3{maximum, 0.0F, 0.0F}, quader::QVec3{maximum, 0.0F, 0.0F}};
  expect_distance_scan_parity(overflow_centroid, 0.0F);
  const std::array later_cluster_overflow{
      quader::QVec3{0.0F, 0.0F, 0.0F}, quader::QVec3{0.0F, 0.0F, 0.0F},
      quader::QVec3{maximum, 0.0F, 0.0F}, quader::QVec3{maximum, 0.0F, 0.0F}};
  expect_distance_scan_parity(later_cluster_overflow, 0.0F);
  const std::array tiny_positions{
      quader::QVec3{minimum, -minimum, 0.0F}, quader::QVec3{-minimum, minimum, 0.0F},
      quader::QVec3{0.0F, 0.0F, 0.0F}};
  expect_distance_scan_parity(tiny_positions, 0.0F);
  const std::array large_cell_positions{
      quader::QVec3{1.0e20F, -1.0e20F, 1.0e20F},
      quader::QVec3{1.0e20F, -1.0e20F, 1.0e20F},
      quader::QVec3{-1.0e20F, 1.0e20F, -1.0e20F}};
  expect_distance_scan_parity(large_cell_positions, 0.0F, {}, true);
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     SparseSelectionLookupFinishesWithinABoundedRuntime) {
  constexpr std::size_t kVertexCount = 100000U;
  Document document;
  document.vertices.reserve(kVertexCount);
  Selection selection;
  selection.mode = SelectionMode::Vertex;
  selection.vertices.reserve(kVertexCount);
  for (std::size_t index = 0; index < kVertexCount; ++index) {
    const ElementId id = document.next_vertex_id++;
    document.vertices.push_back({id, {-static_cast<float>(index) * 4.0F, 2.0F, -1.0F}});
    selection.vertices.push_back(id);
  }
  selection.has_active = true;
  selection.active_kind = ElementKind::Vertex;
  selection.active_vertex = selection.vertices.back();
  const auto source = qdr_test::document_fingerprint(document);
  const auto source_selection = qdr_test::selection_fingerprint(selection);

  const auto begin = std::chrono::steady_clock::now();
  const auto result = merge_selected_vertices_by_distance(document, selection, 0.1F);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

  ASSERT_TRUE(result.success) << result.message;
  EXPECT_FALSE(result.changed);
  EXPECT_EQ(qdr_test::document_fingerprint(document), source);
  EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
  std::cout << "DISTANCE_MERGE_SPARSE vertices=" << kVertexCount
            << " seconds=" << seconds << '\n';
  EXPECT_LT(seconds, 10.0);
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     AuthoredGridCollapseFinishesWithinABoundedRuntime) {
  constexpr std::size_t kSide = 32U;
  Document document;
  Selection selection;
  selection.mode = SelectionMode::Vertex;
  for (std::size_t row = 0; row < kSide; ++row) {
    for (std::size_t column = 0; column < kSide; ++column) {
      selection.vertices.push_back(add_vertex(document,
          {static_cast<float>(column) * 0.5F, 0.0F,
           static_cast<float>(row) * 0.5F}));
    }
  }
  for (std::size_t row = 0; row + 1U < kSide; ++row) {
    for (std::size_t column = 0; column + 1U < kSide; ++column) {
      const ElementId first = static_cast<ElementId>(row * kSide + column + 1U);
      ASSERT_NE(add_face(document, std::array<ElementId, 4>{
          first, first + 1U, first + static_cast<ElementId>(kSide) + 1U,
          first + static_cast<ElementId>(kSide)}), kInvalidElementId);
    }
  }
  const auto begin = std::chrono::steady_clock::now();
  const auto result = merge_selected_vertices_by_distance(document, selection, 0.5F);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(document.vertices.size(), 512U);
  EXPECT_EQ(selection.vertices.size(), 512U);
  EXPECT_TRUE(document.hard_normal_edges.empty());
  EXPECT_TRUE(document.soft_normal_edges.empty());
  EXPECT_TRUE(validate_polygon_document(document,
      PolygonDocumentValidationPolicy::AuthoredTopology).ok());
  std::cout << "DISTANCE_MERGE_AUTHORED_GRID vertices=1024 seconds=" << seconds << '\n';
  EXPECT_LT(seconds, 5.0);
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     PreservesUnrelatedCollapsedFacesAndLooseVertices) {
  auto fixture = make_two_triangle_fixture();
  const Document island =
      qdr_test::append_unrelated_authored_geometry(fixture.document);
  const auto vertex_count_before = fixture.document.vertices.size();
  const auto face_count_before = fixture.document.faces.size();
  const auto next_vertex_id_before = fixture.document.next_vertex_id;
  const auto next_face_id_before = fixture.document.next_face_id;
  Selection selection =
      qdr_test::vertex_selection({fixture.c, fixture.d, fixture.b}, fixture.d);

  const auto result =
      merge_selected_vertices_by_distance(fixture.document, selection, 0.1F);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(fixture.document.vertices.size(), vertex_count_before - 1U);
  EXPECT_EQ(fixture.document.faces.size(), face_count_before);
  EXPECT_EQ(find_vertex(fixture.document, fixture.d), nullptr);
  const Vertex* survivor = find_vertex(fixture.document, fixture.c);
  ASSERT_NE(survivor, nullptr);
  EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, {0.0F, 1.05F, 0.0F}));
  EXPECT_EQ(fixture.document.faces[0].vertices,
            (std::vector<ElementId>{fixture.a, fixture.b, fixture.c}));
  EXPECT_EQ(fixture.document.faces[1].vertices,
            (std::vector<ElementId>{fixture.c, fixture.e, fixture.f}));
  EXPECT_TRUE(qdr_test::face_compiles_to_triangles(
      fixture.document, fixture.document.faces[0]));
  EXPECT_TRUE(qdr_test::face_compiles_to_triangles(
      fixture.document, fixture.document.faces[1]));
  EXPECT_EQ(selection.mode, SelectionMode::Vertex);
  EXPECT_EQ(selection.vertices, (std::vector<ElementId>{fixture.c, fixture.b}));
  EXPECT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_kind, ElementKind::Vertex);
  EXPECT_EQ(selection.active_vertex, fixture.c);
  EXPECT_EQ(fixture.document.next_vertex_id, next_vertex_id_before);
  EXPECT_EQ(fixture.document.next_face_id, next_face_id_before);
  qdr_test::expect_unrelated_authored_geometry_preserved(fixture.document, island);
  EXPECT_TRUE(validate_polygon_document(
      fixture.document, PolygonDocumentValidationPolicy::AuthoredTopology).ok());
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     RejectsMalformedUnrelatedAuthoredDataAtomically) {
  for (const auto fault : qdr_test::kAuthoredInputFaults) {
    SCOPED_TRACE(static_cast<int>(fault));
    auto fixture = make_two_triangle_fixture();
    static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
    qdr_test::corrupt_unrelated_authored_geometry(fixture.document, fault);
    Selection selection = qdr_test::vertex_selection({fixture.c, fixture.d}, fixture.d);
    const auto source = qdr_test::document_fingerprint(fixture.document);
    const auto source_selection = qdr_test::selection_fingerprint(selection);

    const auto result =
        merge_selected_vertices_by_distance(fixture.document, selection, 0.1F);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
    EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
  }
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistanceWeldsSelectedVerticesInclusivelyToTheCentroid) {
  TwoTriangleFixture fixture = make_two_triangle_fixture();
  Selection selection = qdr_test::vertex_selection({fixture.c, fixture.d});

  const OperationResult result =
      merge_selected_vertices_by_distance(fixture.document, selection, 0.1F);

  EXPECT_TRUE(result.changed);

  qdr_test::expect_valid_polygon_document_after_success(fixture.document,
                                                        result);
  EXPECT_TRUE(fixture.document.vertices.size() == 5U);
  const Vertex *survivor = find_vertex(fixture.document, fixture.c);
  EXPECT_TRUE(survivor != nullptr);
  EXPECT_TRUE(find_vertex(fixture.document, fixture.d) == nullptr);
  EXPECT_TRUE(
      qdr_test::vec_nearly_equal(survivor->position, {0.0F, 1.05F, 0.0F}));
  EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
  EXPECT_TRUE(selection.vertices.size() == 1U);
  EXPECT_TRUE(selection.vertices.front() == fixture.c);
  EXPECT_TRUE(selection.has_active);
  EXPECT_TRUE(selection.active_vertex == fixture.c);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistanceKeepsSelectedVerticesWhenToleranceFindsNoCluster) {
  TwoTriangleFixture fixture = make_two_triangle_fixture(0.25F);
  Selection selection = qdr_test::vertex_selection({fixture.c, fixture.d});
  const std::size_t vertex_count_before = fixture.document.vertices.size();

  const OperationResult result =
      merge_selected_vertices_by_distance(fixture.document, selection, 0.1F);

  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before);
  EXPECT_TRUE(selection.vertices.size() == 2U);
  EXPECT_TRUE(std::find(selection.vertices.begin(), selection.vertices.end(),
                        fixture.c) != selection.vertices.end());
  EXPECT_TRUE(std::find(selection.vertices.begin(), selection.vertices.end(),
                        fixture.d) != selection.vertices.end());
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     RemoveDoubleVerticesWeldsExactCoordinateDuplicatesAndRemapsSelection) {
  TwoTriangleFixture fixture = make_two_triangle_fixture(0.0F);
  Selection selection;
  selection.mode = SelectionMode::Edge;
  selection.edges.push_back(make_edge(fixture.d, fixture.e));
  selection.has_active = true;
  selection.active_kind = ElementKind::Edge;
  selection.active_edge = make_edge(fixture.d, fixture.e);

  const OperationResult result =
      remove_double_vertices(fixture.document, selection);

  EXPECT_TRUE(result.changed);
  EXPECT_EQ(result.message, "Removed 1 vertex.");
  qdr_test::expect_valid_polygon_document_after_success(fixture.document,
                                                        result);
  EXPECT_TRUE(fixture.document.vertices.size() == 5U);
  EXPECT_TRUE(find_vertex(fixture.document, fixture.c) != nullptr);
  EXPECT_TRUE(find_vertex(fixture.document, fixture.d) == nullptr);
  EXPECT_TRUE(selection.mode == SelectionMode::Edge);
  ASSERT_EQ(selection.edges.size(), 1U);
  EXPECT_TRUE(selection.edges.front() == make_edge(fixture.c, fixture.e));
  EXPECT_TRUE(selection.has_active);
  EXPECT_TRUE(selection.active_kind == ElementKind::Edge);
  EXPECT_TRUE(selection.active_edge == make_edge(fixture.c, fixture.e));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     RemoveDoubleVerticesReportsNoExactCoordinateDuplicates) {
  TwoTriangleFixture fixture = make_two_triangle_fixture(0.25F);
  Selection selection;
  const std::size_t vertex_count_before = fixture.document.vertices.size();

  const OperationResult result =
      remove_double_vertices(fixture.document, selection);

  EXPECT_FALSE(result.changed);
  EXPECT_EQ(result.message, "No double vertices were found.");
  EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before);
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistancePicksTheMostCentralSurvivorForLargerClusters) {
  Document document;
  const ElementId a = add_vertex(document, {-1.0F, 0.0F, 0.0F});
  const ElementId b = add_vertex(document, {0.05F, 0.0F, 0.0F});
  const ElementId c = add_vertex(document, {0.1F, 0.0F, 0.0F});
  const ElementId a1 = add_vertex(document, {-1.5F, 1.0F, 0.0F});
  const ElementId a2 = add_vertex(document, {-0.5F, 1.0F, 0.0F});
  const ElementId b1 = add_vertex(document, {-0.5F, -1.0F, 0.0F});
  const ElementId b2 = add_vertex(document, {0.5F, -1.0F, 0.0F});
  const ElementId c1 = add_vertex(document, {0.5F, 1.0F, 0.0F});
  const ElementId c2 = add_vertex(document, {1.5F, 1.0F, 0.0F});
  EXPECT_TRUE(add_face(document, std::array<ElementId, 3>{a, a1, a2}) !=
              kInvalidElementId);
  EXPECT_TRUE(add_face(document, std::array<ElementId, 3>{b, b1, b2}) !=
              kInvalidElementId);
  EXPECT_TRUE(add_face(document, std::array<ElementId, 3>{c, c1, c2}) !=
              kInvalidElementId);
  Selection selection = qdr_test::vertex_selection({a, b, c});

  const OperationResult result =
      merge_selected_vertices_by_distance(document, selection, 1.1F);

  EXPECT_TRUE(result.changed);

  qdr_test::expect_valid_polygon_document_after_success(document, result);
  EXPECT_TRUE(find_vertex(document, a) == nullptr);
  EXPECT_TRUE(find_vertex(document, c) == nullptr);
  const Vertex *survivor = find_vertex(document, b);
  EXPECT_TRUE(survivor != nullptr);
  EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position,
                                         {-0.28333333F, 0.0F, 0.0F}));
  EXPECT_TRUE(selection.vertices.size() == 1U);
  EXPECT_TRUE(selection.vertices.front() == b);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistanceCollapsesDuplicateFacesCreatedByWeldedCubeSides) {
  std::vector<ElementId> first_interface;
  std::vector<ElementId> second_interface;
  Document document =
      make_touching_cube_pair_document(first_interface, second_interface);
  EXPECT_TRUE(first_interface.size() == 4U);
  EXPECT_TRUE(second_interface.size() == 4U);
  EXPECT_TRUE(face_count_on_plane_x(document, 1.0F) == 2U);
  const std::size_t vertex_count_before = document.vertices.size();

  Selection selection;
  selection.mode = SelectionMode::Vertex;
  selection.vertices.insert(selection.vertices.end(), first_interface.begin(),
                            first_interface.end());
  selection.vertices.insert(selection.vertices.end(), second_interface.begin(),
                            second_interface.end());

  const OperationResult result =
      merge_selected_vertices_by_distance(document, selection, 0.0F);

  EXPECT_TRUE(result.changed);

  qdr_test::expect_polygon_document_validation_issue_after_success(
      document, result, PolygonDocumentDiagnosticCode::NonmanifoldEdge);
  EXPECT_TRUE(document.vertices.size() == vertex_count_before - 4U);
  EXPECT_TRUE(document.faces.size() == 11U);
  EXPECT_TRUE(face_count_on_plane_x(document, 1.0F) == 1U);
  EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
  EXPECT_TRUE(selection.vertices.size() == 4U);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistanceAllowsNonManifoldEdgeClusters) {
  Document document;
  const ElementId a1 = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const ElementId b1 = add_vertex(document, {0.0F, 0.0F, 1.0F});
  const ElementId a2 = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const ElementId b2 = add_vertex(document, {0.0F, 0.0F, 1.0F});
  const ElementId a3 = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const ElementId b3 = add_vertex(document, {0.0F, 0.0F, 1.0F});
  const ElementId c1 = add_vertex(document, {1.0F, 0.0F, 1.0F});
  const ElementId d1 = add_vertex(document, {1.0F, 0.0F, 0.0F});
  const ElementId c2 = add_vertex(document, {0.0F, 1.0F, 1.0F});
  const ElementId d2 = add_vertex(document, {0.0F, 1.0F, 0.0F});
  const ElementId c3 = add_vertex(document, {-1.0F, 0.0F, 1.0F});
  const ElementId d3 = add_vertex(document, {-1.0F, 0.0F, 0.0F});
  EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{a1, b1, c1, d1}) !=
              kInvalidElementId);
  EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{a2, b2, c2, d2}) !=
              kInvalidElementId);
  EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{a3, b3, c3, d3}) !=
              kInvalidElementId);
  Selection selection = qdr_test::vertex_selection({a1, b1, a2, b2, a3, b3});

  const OperationResult result =
      merge_selected_vertices_by_distance(document, selection, 0.001F);

  EXPECT_TRUE(result.changed);

  qdr_test::expect_polygon_document_validation_issue_after_success(
      document, result, PolygonDocumentDiagnosticCode::NonmanifoldEdge);
  EXPECT_TRUE(result.message.empty());
  EXPECT_TRUE(document.vertices.size() == 8U);
  EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(a1, b1)) == 3);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistancePreservesIncidentFaceAcrossCoincidentBridgeVertices) {
  Document document;
  const ElementId top_left = add_vertex(document, {8.0F, 1.0F, -2.0F});
  const ElementId bridge_left = add_vertex(document, {8.0F, 0.5F, -2.0F});
  const ElementId back_left = add_vertex(document, {8.0F, 0.5F, 0.0F});
  const ElementId top_back_left = add_vertex(document, {8.0F, 1.0F, 0.0F});
  const ElementId disconnected_back_left =
      add_vertex(document, {8.0F, 0.5F, 0.0F});
  const ElementId back_right = add_vertex(document, {10.0F, 0.5F, 0.0F});
  const ElementId bridge_right = add_vertex(document, {10.0F, 0.5F, -2.0F});
  const ElementId top_right = add_vertex(document, {10.0F, 1.0F, -2.0F});
  const ElementId disconnected_bridge_right =
      add_vertex(document, {10.0F, 0.5F, -2.0F});
  const ElementId disconnected_bridge_left =
      add_vertex(document, {8.0F, 0.5F, -2.0F});

  const ElementId left_face =
      add_face(document, std::array<ElementId, 4>{
                             top_left, top_back_left, back_left, bridge_left});
  const ElementId right_face =
      add_face(document, std::array<ElementId, 4>{
                             top_right, top_left, disconnected_bridge_left,
                             disconnected_bridge_right});
  const ElementId back_face =
      add_face(document, std::array<ElementId, 3>{
                             disconnected_back_left, back_right,
                             bridge_right});
  const ElementId bridged_face = add_face(
      document,
      std::array<ElementId, 10>{
          top_left, bridge_left, back_left, top_back_left,
          disconnected_back_left, back_right, bridge_right, top_right,
          disconnected_bridge_right, disconnected_bridge_left});
  ASSERT_NE(left_face, kInvalidElementId);
  ASSERT_NE(right_face, kInvalidElementId);
  ASSERT_NE(back_face, kInvalidElementId);
  ASSERT_NE(bridged_face, kInvalidElementId);
  ASSERT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
  const std::size_t face_count_before = document.faces.size();
  const float bridged_face_area_before =
      compiled_primitive_area(compile_document(document),
                              face_count_before - 1U);
  // The three exact out-and-back boundary spurs enclose no surface area.
  EXPECT_NEAR(bridged_face_area_before, 4.0F, 0.0001F);

  Selection selection = qdr_test::vertex_selection(
      {bridge_left, disconnected_bridge_left});
  const OperationResult result =
      merge_selected_vertices_by_distance(document, selection, 0.125F);

  ASSERT_TRUE(result.changed) << result.message;
  qdr_test::expect_valid_polygon_document_after_success(document, result);
  EXPECT_EQ(document.faces.size(), face_count_before);
  EXPECT_NE(find_face(document, bridged_face), nullptr);
  EXPECT_EQ(find_vertex(document, disconnected_bridge_left), nullptr);
  EXPECT_NE(find_vertex(document, bridge_left), nullptr);
  ASSERT_EQ(selection.vertices.size(), 1U);
  EXPECT_EQ(selection.vertices.front(), bridge_left);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
  const auto repaired_face = std::ranges::find_if(
      document.faces,
      [bridged_face](const Face &face) { return face.id == bridged_face; });
  ASSERT_NE(repaired_face, document.faces.end());
  EXPECT_EQ(repaired_face->vertices,
            (std::vector<ElementId>{
                bridge_left, back_left, top_back_left,
                disconnected_back_left, back_right, bridge_right, top_right,
                disconnected_bridge_right}));
  EXPECT_EQ(qdr_test::edge_incidence_count(
                document,
                make_edge(top_left, disconnected_bridge_right)),
            0U);
  EXPECT_EQ(qdr_test::edge_incidence_count(
                document,
                make_edge(bridge_left, disconnected_bridge_right)),
            2U);
  const float bridged_face_area_after = compiled_primitive_area(
      compile_document(document),
      static_cast<std::size_t>(
          std::distance(document.faces.begin(), repaired_face)));
  EXPECT_NEAR(bridged_face_area_after, 4.0F, 0.0001F);
  EXPECT_NEAR(bridged_face_area_after, bridged_face_area_before, 0.0001F);
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests,
     MergeVerticesByDistanceKeepsBothValidBoundaryArcsWhenWeldingAcrossAFace) {
  Document document;
  const ElementId survivor = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const ElementId first_left = add_vertex(document, {-2.0F, 0.0F, 0.0F});
  const ElementId first_right = add_vertex(document, {-1.0F, 1.0F, 0.0F});
  const ElementId removed = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const ElementId second_left = add_vertex(document, {1.0F, 1.0F, 0.0F});
  const ElementId second_right = add_vertex(document, {2.0F, 0.0F, 0.0F});
  const ElementId source_face = add_face(
      document, std::array<ElementId, 6>{
                    survivor, first_left, first_right, removed, second_left,
                    second_right});
  ASSERT_NE(source_face, kInvalidElementId);

  Selection selection = qdr_test::vertex_selection({survivor, removed});
  const OperationResult result =
      merge_selected_vertices_by_distance(document, selection, 0.001F);

  ASSERT_TRUE(result.changed) << result.message;
  qdr_test::expect_valid_polygon_document_after_success(document, result);
  ASSERT_EQ(document.faces.size(), 2U);
  EXPECT_EQ(find_vertex(document, removed), nullptr);
  EXPECT_EQ(selection.vertices, (std::vector<ElementId>{survivor}));
  EXPECT_EQ(document.faces[0].id, source_face);
  EXPECT_EQ(document.faces[0].vertices,
            (std::vector<ElementId>{survivor, second_left, second_right}));
  EXPECT_EQ(document.faces[1].vertices,
            (std::vector<ElementId>{survivor, first_left, first_right}));
  EXPECT_EQ(qdr_test::edge_incidence_count(
                document, make_edge(first_right, second_left)),
            0U);
  EXPECT_EQ(qdr_test::edge_incidence_count(
                document, make_edge(second_right, first_left)),
            0U);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}
