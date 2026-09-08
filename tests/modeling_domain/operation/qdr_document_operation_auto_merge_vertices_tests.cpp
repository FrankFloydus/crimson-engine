////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <limits>

using namespace quader_poly;

namespace {

/** Makes a disconnected duplicate triangle with independently authored corners. */
Document duplicate_triangles() {
  Document document;
  for (int triangle = 0; triangle < 2; ++triangle) {
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId c = add_vertex(document, {0.0F, 1.0F, 0.0F});
    EXPECT_NE(add_face(document, std::array{a, b, c},
                       static_cast<std::uint32_t>(triangle + 5)),
              kInvalidElementId);
  }
  return document;
}

} // namespace

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     DuplicateSelectedFaceTransfersSelectionAndActiveIdentityToSurvivor) {
  Document document = duplicate_triangles();
  const Face original = document.faces.front();
  const ElementId retired = document.faces.back().id;
  Selection selection;
  selection.mode = SelectionMode::Face;
  selection.faces = {retired};
  selection.has_active = true;
  selection.active_kind = ElementKind::Face;
  selection.active_face = retired;

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  ASSERT_EQ(document.faces.size(), 1U);
  EXPECT_EQ(document.vertices.size(), 3U);
  EXPECT_EQ(document.faces.front().id, original.id);
  EXPECT_EQ(document.faces.front().material_slot, original.material_slot);
  EXPECT_EQ(document.faces.front().vertices, original.vertices);
  EXPECT_EQ(result.deleted.faces, std::vector<ElementId>{retired});
  EXPECT_EQ(result.deleted.vertices.size(), 3U);
  EXPECT_EQ(selection.mode, SelectionMode::Face);
  EXPECT_EQ(selection.faces, std::vector<ElementId>{original.id});
  EXPECT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_face, original.id);
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     CollapsedSelectedEdgeIsRemovedWithoutPruningUnrelatedLooseVertices) {
  Document document;
  const ElementId target = add_vertex(document, {});
  const ElementId source = add_vertex(document, {});
  const ElementId third = add_vertex(document, {0.0F, 1.0F, 0.0F});
  const ElementId unrelated = add_vertex(document, {9.0F, 8.0F, 7.0F});
  // Authored finite collapsed geometry is retained until the explicit weld.
  document.faces.push_back({.id = document.next_face_id++,
                             .vertices = {source, target, third}});
  const Edge collapsing = make_edge(source, target);
  document.hard_normal_edges = {collapsing};
  Selection selection;
  selection.mode = SelectionMode::Edge;
  selection.edges = {collapsing};
  selection.has_active = true;
  selection.active_kind = ElementKind::Edge;
  selection.active_edge = collapsing;

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(document.vertices.size(), 3U);
  EXPECT_NE(find_vertex(document, target), nullptr);
  EXPECT_NE(find_vertex(document, third), nullptr);
  ASSERT_NE(find_vertex(document, unrelated), nullptr);
  EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(document, unrelated)->position,
                                        {9.0F, 8.0F, 7.0F}));
  EXPECT_TRUE(document.faces.empty());
  EXPECT_TRUE(document.hard_normal_edges.empty());
  EXPECT_TRUE(selection.edges.empty());
  EXPECT_EQ(selection.mode, SelectionMode::Edge);
  EXPECT_FALSE(selection.has_active);
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     EqualUnselectedTargetDistancesUseDocumentOrderInsteadOfElementId) {
  Document document;
  document.vertices = {{20U, {-0.0005F, 0.0F, 0.0F}},
                        {10U, {0.0005F, 0.0F, 0.0F}},
                        {30U, {0.0F, 0.0F, 0.0F}}};
  document.next_vertex_id = 31U;
  Selection selection = qdr_test::vertex_selection({30U});

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(selection.vertices, std::vector<ElementId>{20U});
  ASSERT_NE(find_vertex(document, 20U), nullptr);
  EXPECT_EQ(find_vertex(document, 20U)->position.x, -0.0005F);
  EXPECT_NE(find_vertex(document, 10U), nullptr);
  EXPECT_EQ(find_vertex(document, 30U), nullptr);
}

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     TouchingMeshPartsWeldWithoutIntroducingAManifoldOnlyRestriction) {
  Document document = make_cube({});
  Document second = make_cube({});
  Selection selection;
  selection.mode = SelectionMode::Vertex;
  for (Vertex &vertex : second.vertices) {
    vertex.id += 100U;
    vertex.position.x += 2.0F;
    if (vertex.position.x == 1.0F) {
      selection.vertices.push_back(vertex.id);
    }
  }
  for (Face &face : second.faces) {
    face.id += 100U;
    for (ElementId &vertex : face.vertices) {
      vertex += 100U;
    }
  }
  document.vertices.insert(document.vertices.end(), second.vertices.begin(), second.vertices.end());
  document.faces.insert(document.faces.end(), second.faces.begin(), second.faces.end());
  document.next_vertex_id = 200U;
  document.next_face_id = 200U;

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(document.vertices.size(), 12U);
  EXPECT_EQ(document.faces.size(), 11U);
  EXPECT_EQ(selection.vertices.size(), 4U);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
  qdr_test::expect_polygon_document_validation_issue_after_success(
      document, result, PolygonDocumentDiagnosticCode::NonmanifoldEdge);
}

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     SelectedNonadjacentWeldRetainsBothFaceDescendantsAndTheirShading) {
  Document document;
  const std::array<quader::QVec3, 6> positions{{
      {0.0F, 0.0F, 0.0F}, {-2.0F, 0.0F, 0.0F}, {-1.0F, 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {2.0F, 0.0F, 0.0F}}};
  std::vector<ElementId> vertices;
  for (const auto position : positions) {
    vertices.push_back(add_vertex(document, position));
  }
  const ElementId source_face = add_face(document, vertices, 7U);
  ASSERT_NE(source_face, kInvalidElementId);
  document.faces.front().normal_shading = SurfaceShading::Smooth;
  Selection selection;
  selection.mode = SelectionMode::Face;
  selection.faces = {source_face};
  selection.has_active = true;
  selection.active_kind = ElementKind::Face;
  selection.active_face = source_face;

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  ASSERT_EQ(document.faces.size(), 2U);
  EXPECT_EQ(selection.faces,
            (std::vector<ElementId>{document.faces[0].id, document.faces[1].id}));
  EXPECT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_face, source_face);
  EXPECT_EQ(result.created.faces, std::vector<ElementId>{document.faces[1].id});
  for (const Face &face : document.faces) {
    EXPECT_EQ(face.vertices.size(), 3U);
    EXPECT_EQ(face.material_slot, 7U);
    EXPECT_EQ(face.normal_shading, SurfaceShading::Smooth);
  }
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     PreservesDistantFiniteCollapsedAuthoredFaceWhileWeldingValidGeometry) {
  Document document = duplicate_triangles();
  const ElementId first = add_vertex(document, {20.0F, 0.0F, 0.0F});
  const ElementId middle = add_vertex(document, {21.0F, 0.0F, 0.0F});
  const ElementId last = add_vertex(document, {22.0F, 0.0F, 0.0F});
  const ElementId collapsed_id = document.next_face_id++;
  document.faces.push_back({.id = collapsed_id,
                             .vertices = {first, middle, last},
                             .uvs = {{0.0F, 0.0F}, {0.5F, 0.0F}, {1.0F, 0.0F}},
                             .material_slot = 9U,
                             .normal_shading = SurfaceShading::Smooth});
  const Face before = document.faces.back();
  Selection selection = qdr_test::vertex_selection({4U});

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(document.vertices.size(), 8U);
  EXPECT_EQ(selection.vertices, std::vector<ElementId>{1U});
  const Face *after = find_face(document, collapsed_id);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->vertices, before.vertices);
  EXPECT_EQ(after->material_slot, before.material_slot);
  EXPECT_EQ(after->normal_shading, before.normal_shading);
  ASSERT_EQ(after->uvs.size(), before.uvs.size());
  for (std::size_t corner = 0; corner < after->uvs.size(); ++corner) {
    EXPECT_EQ(after->uvs[corner].x, before.uvs[corner].x);
    EXPECT_EQ(after->uvs[corner].y, before.uvs[corner].y);
  }
  EXPECT_TRUE(validate_polygon_document(
      document, PolygonDocumentValidationPolicy::AuthoredTopology).ok());
}

TEST(QdrDocumentOperationAutoMergeVerticesTests,
     RejectedTopologyPreservesDocumentSelectionAttributesAndAllocators) {
  Document document = duplicate_triangles();
  // A pre-existing invalid unrelated face must never cause a partial weld.
  document.faces.push_back({.id = document.next_face_id++,
                             .vertices = {1U, 2U}});
  document.hard_normal_edges = {make_edge(1U, 2U)};
  const Document before = document;
  Selection selection = qdr_test::vertex_selection({4U});
  const Selection before_selection = selection;

  const OperationResult result = auto_merge_selected_vertices(document, selection);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.changed);
  EXPECT_FALSE(result.message.empty());
  EXPECT_EQ(document.next_vertex_id, before.next_vertex_id);
  EXPECT_EQ(document.next_face_id, before.next_face_id);
  EXPECT_EQ(document.hard_normal_edges, before.hard_normal_edges);
  ASSERT_EQ(document.vertices.size(), before.vertices.size());
  ASSERT_EQ(document.faces.size(), before.faces.size());
  for (std::size_t index = 0; index < document.vertices.size(); ++index) {
    EXPECT_EQ(document.vertices[index].id, before.vertices[index].id);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(document.vertices[index].position,
                                          before.vertices[index].position));
  }
  for (std::size_t index = 0; index < document.faces.size(); ++index) {
    EXPECT_EQ(document.faces[index].id, before.faces[index].id);
    EXPECT_EQ(document.faces[index].vertices, before.faces[index].vertices);
    EXPECT_EQ(document.faces[index].material_slot, before.faces[index].material_slot);
  }
  EXPECT_EQ(selection.vertices, before_selection.vertices);
  EXPECT_EQ(selection.active_vertex, before_selection.active_vertex);
  EXPECT_EQ(selection.has_active, before_selection.has_active);
}
