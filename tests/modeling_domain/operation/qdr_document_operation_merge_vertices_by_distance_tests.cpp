////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <cmath>

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

} // namespace

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
