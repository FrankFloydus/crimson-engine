////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>
#include <cmath>
#include <limits>

using namespace quader_poly;

TEST(QdrDocumentOperationExtrudeFacesTests, ExtrudeFacesCreatesACapAndSideWallsWhileKeepingTheCapSelected) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(11);
	const FaceTextureTransform texture{{-2.0F, 0.5F}, {0.25F, 0.75F}, 25.0F};
	fixture.document.faces.front().texture_transform = texture;
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = extrude_selected_faces(fixture.document, selection, 0.5F);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.vertices.size() == 4U);
	EXPECT_TRUE(result.created.faces.size() == 4U);
	EXPECT_TRUE(result.created.edges.empty());
	EXPECT_TRUE(result.deleted.empty());
	EXPECT_TRUE(result.affected.vertices.empty());
	EXPECT_TRUE(result.affected.edges.empty());
	EXPECT_TRUE(result.affected.faces.size() == 1U);
	EXPECT_TRUE(result.affected.faces.front() == fixture.face);
	EXPECT_TRUE(fixture.document.vertices.size() == 8U);
	EXPECT_TRUE(fixture.document.faces.size() == 5U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	EXPECT_TRUE(selection.faces.front() == fixture.face);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationExtrudeFacesTests, ExtrudeCopiesExactModesToTheCapAndDefaultsNewConnectors) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face& source = fixture.document.faces.front();
	source.normal_shading = SurfaceShading::Smooth;
	const std::vector<ElementId> source_vertices = source.vertices;
	const Edge hard_source = make_edge(source_vertices[0], source_vertices[1]);
	const Edge soft_source = make_edge(source_vertices[2], source_vertices[3]);
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, hard_source, EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, soft_source, EdgeNormalMode::Soft));
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = extrude_selected_faces(fixture.document, selection, 0.5F);

	ASSERT_TRUE(result.changed) << result.message;
	const Face* cap = find_face(fixture.document, fixture.face);
	ASSERT_NE(cap, nullptr);
	ASSERT_EQ(cap->vertices.size(), source_vertices.size());
	EXPECT_EQ(cap->normal_shading, SurfaceShading::Smooth);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(fixture.document, make_edge(cap->vertices[0], cap->vertices[1])),
		EdgeNormalMode::Hard);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(fixture.document, make_edge(cap->vertices[2], cap->vertices[3])),
		EdgeNormalMode::Soft);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(fixture.document, make_edge(source_vertices[0], cap->vertices[0])),
		EdgeNormalMode::Default);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

namespace {

/** Checks cap movement against source corners while preserving their ordering. */
void expect_cap_corner_offset(const Document& before, const Document& after,
                             ElementId face_id, std::size_t corner,
                             quader::QVec3 expected_offset) {
  const Face* source = find_face(before, face_id);
  const Face* cap = find_face(after, face_id);
  ASSERT_NE(source, nullptr);
  ASSERT_NE(cap, nullptr);
  ASSERT_LT(corner, source->vertices.size());
  ASSERT_EQ(cap->vertices.size(), source->vertices.size());
  const Vertex* source_vertex = find_vertex(before, source->vertices[corner]);
  const Vertex* cap_vertex = find_vertex(after, cap->vertices[corner]);
  ASSERT_NE(source_vertex, nullptr);
  ASSERT_NE(cap_vertex, nullptr);
  const auto delta = cap_vertex->position - source_vertex->position;
  EXPECT_NEAR(delta.x, expected_offset.x, 0.00001F);
  EXPECT_NEAR(delta.y, expected_offset.y, 0.00001F);
  EXPECT_NEAR(delta.z, expected_offset.z, 0.00001F);
}

/** Counts shared cap identities, independently of coincident positions. */
std::size_t shared_cap_vertex_count(const Document& document,
                                  ElementId first, ElementId second) {
  const Face* first_cap = find_face(document, first);
  const Face* second_cap = find_face(document, second);
  if (first_cap == nullptr || second_cap == nullptr) {
    ADD_FAILURE() << "Extrusion lost a selected cap identity";
    return 0U;
  }
  return static_cast<std::size_t>(std::ranges::count_if(
      first_cap->vertices, [&](ElementId vertex) {
        return std::ranges::find(second_cap->vertices, vertex) !=
               second_cap->vertices.end();
      }));
}

} // namespace

TEST(QdrDocumentOperationExtrudeFacesTests, RegionMovesConnectedCapsTogetherUsingUnitFaceNormals) {
  auto fixture = qdr_test::make_manifold_corner_loop_document();
  const Document before = fixture.document;
  auto selection = qdr_test::face_selection(
      {fixture.top_face, fixture.side_face}, fixture.top_face);
  const FaceExtrudeOptions options{.mode = FaceExtrudeMode::Region, .amount = 1.0F};

  const auto result = extrude_selected_faces(fixture.document, selection, options);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  const float component = std::sqrt(0.5F);
  for (const auto face : {fixture.top_face, fixture.side_face}) {
    for (std::size_t corner = 0; corner < 4U; ++corner) {
      expect_cap_corner_offset(before, fixture.document, face, corner,
                               {0.0F, -component, component});
    }
  }
  EXPECT_EQ(shared_cap_vertex_count(fixture.document, fixture.top_face,
                                   fixture.side_face), 2U);
  EXPECT_EQ(result.created.vertices.size(), 6U);
  EXPECT_EQ(selection.faces, (std::vector<ElementId>{fixture.top_face, fixture.side_face}));
  EXPECT_EQ(selection.active_face, fixture.top_face);
  qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, AlongNormalsSharesCapsAndMovesEachVertexByTheRequestedLength) {
  auto fixture = qdr_test::make_manifold_corner_loop_document();
  const Document before = fixture.document;
  auto selection = qdr_test::face_selection(
      {fixture.top_face, fixture.side_face}, fixture.side_face);
  const FaceExtrudeOptions options{.mode = FaceExtrudeMode::AlongNormals, .amount = 2.0F};

  const auto result = extrude_selected_faces(fixture.document, selection, options);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  const float component = std::sqrt(2.0F);
  for (std::size_t corner = 0; corner < 2U; ++corner) {
    expect_cap_corner_offset(before, fixture.document, fixture.top_face, corner,
                             {0.0F, -component, component});
    expect_cap_corner_offset(before, fixture.document, fixture.side_face, corner,
                             {0.0F, -component, component});
  }
  for (std::size_t corner = 2; corner < 4U; ++corner) {
    expect_cap_corner_offset(before, fixture.document, fixture.top_face, corner,
                             {0.0F, 0.0F, 2.0F});
    expect_cap_corner_offset(before, fixture.document, fixture.side_face, corner,
                             {0.0F, -2.0F, 0.0F});
  }
  EXPECT_EQ(shared_cap_vertex_count(fixture.document, fixture.top_face,
                                   fixture.side_face), 2U);
  EXPECT_EQ(result.created.vertices.size(), 6U);
  qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, AlongNormalsWeightsUnequalSelectedCornerAngles) {
  Document document;
  const auto a = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const auto b = add_vertex(document, {2.0F, 0.0F, 0.0F});
  const auto c = add_vertex(document, {2.0F, 2.0F, 0.0F});
  const auto d = add_vertex(document, {0.0F, 2.0F, 0.0F});
  const auto e = add_vertex(document, {-2.0F, 0.0F, -2.0F});
  const auto top = add_face(document, std::array{a, b, c, d});
  const auto side = add_face(document, std::array{b, a, e});
  const Document before = document;
  auto selection = qdr_test::face_selection({top, side}, top);

  const auto result = extrude_selected_faces(
      document, selection,
      FaceExtrudeOptions{.mode = FaceExtrudeMode::AlongNormals, .amount = 1.0F});

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  // The shared source corner has 90 degrees on top and 135 degrees on the side.
  const float scale = 1.0F / std::sqrt(13.0F);
  expect_cap_corner_offset(before, document, top, 0U,
                           {0.0F, -3.0F * scale, 2.0F * scale});
  EXPECT_EQ(shared_cap_vertex_count(document, top, side), 2U);
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, IndividualSeparatesCapVerticesAndKeepsOriginalBases) {
  auto fixture = qdr_test::make_manifold_corner_loop_document();
  const Document before = fixture.document;
  auto selection = qdr_test::face_selection(
      {fixture.top_face, fixture.side_face}, fixture.side_face);

  const auto result = extrude_selected_faces(
      fixture.document, selection,
      FaceExtrudeOptions{.mode = FaceExtrudeMode::Individual, .amount = 1.0F});

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(shared_cap_vertex_count(fixture.document, fixture.top_face,
                                   fixture.side_face), 0U);
  EXPECT_EQ(result.created.vertices.size(), 8U);
  EXPECT_EQ(result.created.faces.size(), 8U);
  EXPECT_EQ(fixture.document.faces.size(), 10U);
  for (std::size_t corner = 0; corner < 4U; ++corner) {
    expect_cap_corner_offset(before, fixture.document, fixture.top_face, corner,
                             {0.0F, 0.0F, 1.0F});
    expect_cap_corner_offset(before, fixture.document, fixture.side_face, corner,
                             {0.0F, -1.0F, 0.0F});
  }
  for (const Vertex& vertex : before.vertices) {
    const Vertex* retained = find_vertex(fixture.document, vertex.id);
    ASSERT_NE(retained, nullptr);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(retained->position, vertex.position));
  }
  EXPECT_EQ(selection.faces, (std::vector<ElementId>{fixture.top_face, fixture.side_face}));
  EXPECT_EQ(selection.active_face, fixture.side_face);
  qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, AlongNormalsUsesTheSmallerAngleAtConcaveCorners) {
  Document document;
  const auto a = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const auto b = add_vertex(document, {2.0F, 0.0F, 0.0F});
  const auto c = add_vertex(document, {2.0F, 1.0F, 0.0F});
  const auto d = add_vertex(document, {1.0F, 1.0F, 0.0F});
  const auto e = add_vertex(document, {1.0F, 2.0F, 0.0F});
  const auto f = add_vertex(document, {0.0F, 2.0F, 0.0F});
  const auto g = add_vertex(document, {2.0F, 1.0F, -1.0F});
  const auto h = add_vertex(document, {1.0F, 1.0F, -1.0F});
  const auto top = add_face(document, std::array{a, b, c, d, e, f});
  const auto side = add_face(document, std::array{d, c, g, h});
  const Document before = document;
  auto selection = qdr_test::face_selection({top, side}, top);

  const auto result = extrude_selected_faces(document, selection,
      FaceExtrudeOptions{.mode = FaceExtrudeMode::AlongNormals, .amount = 0.25F});

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  const float component = 0.25F * std::sqrt(0.5F);
  expect_cap_corner_offset(before, document, top, 3U, {0.0F, component, component});
  EXPECT_EQ(shared_cap_vertex_count(document, top, side), 2U);
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, CoplanarCapsStayConnectedOnlyForRegionModes) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto fixture = qdr_test::make_quad_document();
    const auto bottom = add_vertex(fixture.document, {3.0F, -1.0F, 0.0F});
    const auto top = add_vertex(fixture.document, {3.0F, 1.0F, 0.0F});
    const auto second = add_face(fixture.document, std::array{
        fixture.bottom_right, bottom, top, fixture.top_right});
    auto selection = qdr_test::face_selection({fixture.face, second}, second);

    const auto result = extrude_selected_faces(
        fixture.document, selection, FaceExtrudeOptions{.mode = mode, .amount = 0.5F});

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(shared_cap_vertex_count(fixture.document, fixture.face, second),
              mode == FaceExtrudeMode::Individual ? 0U : 2U);
    EXPECT_EQ(result.created.vertices.size(),
              mode == FaceExtrudeMode::Individual ? 8U : 6U);
    for (const auto face : selection.faces) {
      for (const auto vertex : find_face(fixture.document, face)->vertices) {
        EXPECT_FLOAT_EQ(find_vertex(fixture.document, vertex)->position.z, 0.5F);
      }
    }
    if (mode != FaceExtrudeMode::Individual) {
      EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
      EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
    }
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
  }
}

TEST(QdrDocumentOperationExtrudeFacesTests, ClosedMeshExtrusionDoesNotRetainAnInternalSourceCap) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    Document document = make_cube({});
    const auto face = document.faces.front().id;
    auto selection = qdr_test::face_selection({face}, face);

    const auto result = extrude_selected_faces(
        document, selection, FaceExtrudeOptions{.mode = mode, .amount = 0.25F});

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(document.vertices.size(), 12U);
    EXPECT_EQ(document.faces.size(), 10U);
    EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
    EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
    qdr_test::expect_valid_polygon_document_after_success(document, result);
  }
}

TEST(QdrDocumentOperationExtrudeFacesTests, SignedDistanceUsesRotatedScaledAndMirroredOperationSpace) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    for (const float handedness : {-1.0F, 1.0F}) {
      for (const float amount : {-2.15F, 2.15F}) {
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(handedness);
        SCOPED_TRACE(amount);
        auto fixture = qdr_test::make_quad_document();
        const Document before = fixture.document;
        auto selection = qdr_test::face_selection({fixture.face}, fixture.face);
        FaceExtrudeOptions options{.mode = mode, .amount = amount};
        options.extrusion_from_local.x_axis = {0.0F, 2.0F * handedness, 0.0F};
        options.extrusion_from_local.y_axis = {0.0F, 0.0F, 3.0F};
        options.extrusion_from_local.z_axis = {4.0F, 0.0F, 0.0F};
        options.extrusion_from_local.origin = {7.0F, -13.0F, 21.0F};

        const auto result = extrude_selected_faces(fixture.document, selection, options);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        for (std::size_t corner = 0; corner < 4U; ++corner) {
          expect_cap_corner_offset(before, fixture.document, fixture.face, corner,
                                   {0.0F, 0.0F, handedness * amount / 4.0F});
        }
        qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
      }
    }
  }
}

TEST(QdrDocumentOperationExtrudeFacesTests, ExplicitRegionDirectionIsNormalizedInOperationSpace) {
  auto fixture = qdr_test::make_quad_document();
  const Document before = fixture.document;
  auto selection = qdr_test::face_selection({fixture.face}, fixture.face);
  FaceExtrudeOptions options{.mode = FaceExtrudeMode::Region, .amount = 2.0F};
  options.region_direction = quader::QVec3{0.0F, 3.0F, 4.0F};
  options.extrusion_from_local.y_axis = {0.0F, 2.0F, 0.0F};
  options.extrusion_from_local.z_axis = {0.0F, 0.0F, 4.0F};

  const auto result = extrude_selected_faces(fixture.document, selection, options);

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  for (std::size_t corner = 0; corner < 4U; ++corner) {
    expect_cap_corner_offset(before, fixture.document, fixture.face, corner,
                             {0.0F, 0.6F, 0.4F});
  }
  qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, CancelledRegionNormalSumUsesTheActiveDisconnectedFace) {
  Document document;
  const auto a = add_vertex(document, {-1.0F, -1.0F, 0.0F});
  const auto b = add_vertex(document, {1.0F, -1.0F, 0.0F});
  const auto c = add_vertex(document, {1.0F, 1.0F, 0.0F});
  const auto d = add_vertex(document, {-1.0F, 1.0F, 0.0F});
  const auto e = add_vertex(document, {4.0F, -1.0F, 0.0F});
  const auto f = add_vertex(document, {6.0F, -1.0F, 0.0F});
  const auto g = add_vertex(document, {6.0F, 1.0F, 0.0F});
  const auto h = add_vertex(document, {4.0F, 1.0F, 0.0F});
  const auto positive = add_face(document, std::array{a, b, c, d});
  const auto negative = add_face(document, std::array{h, g, f, e});
  const Document before = document;
  auto selection = qdr_test::face_selection({positive, negative}, negative);

  const auto result = extrude_selected_faces(
      document, selection,
      FaceExtrudeOptions{.mode = FaceExtrudeMode::Region, .amount = 1.0F});

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  for (const auto face : {positive, negative}) {
    for (std::size_t corner = 0; corner < 4U; ++corner) {
      expect_cap_corner_offset(before, document, face, corner, {0.0F, 0.0F, -1.0F});
    }
  }
  EXPECT_EQ(shared_cap_vertex_count(document, positive, negative), 0U);
  EXPECT_EQ(selection.active_face, negative);
  qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationExtrudeFacesTests, RegionRetainsFlatBoundaryWallsForOppositeSidesAndTop) {
  Document document = make_cube({});
  Selection selection;
  selection.mode = SelectionMode::Face;
  for (const Face& face : document.faces) {
    const auto normal = face_normal(document, face);
    if (std::abs(normal.x) > 0.9F || normal.z > 0.9F) {
      selection.faces.push_back(face.id);
      if (normal.z > 0.9F) {
        selection.has_active = true;
        selection.active_kind = ElementKind::Face;
        selection.active_face = face.id;
      }
    }
  }
  ASSERT_EQ(selection.faces.size(), 3U);
  const auto selected_faces = selection.faces;
  const Document before = document;

  const auto result = extrude_selected_faces(
      document, selection,
      FaceExtrudeOptions{.mode = FaceExtrudeMode::Region, .amount = 0.5F});

  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(document.vertices.size(), 16U);
  EXPECT_EQ(document.faces.size(), 14U);
  EXPECT_EQ(result.created.vertices.size(), 8U);
  EXPECT_EQ(result.created.faces.size(), 8U);
  EXPECT_EQ(selection.faces, selected_faces);
  for (const auto face : selected_faces) {
    for (std::size_t corner = 0; corner < 4U; ++corner) {
      expect_cap_corner_offset(before, document, face, corner, {0.0F, 0.0F, 0.5F});
    }
  }
  EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
  EXPECT_TRUE(validate_polygon_document(
      document, PolygonDocumentValidationPolicy::AuthoredTopology).ok());
  // Moving the two side caps along their planes intentionally authors flat walls.
  EXPECT_FALSE(validate_polygon_document(document).ok());
}

TEST(QdrDocumentOperationExtrudeFacesTests, AlongNormalsKeepsNonplanarWallsWithSkewBoundaryEdges) {
  for (const float amount : {-0.5F, 0.5F}) {
    SCOPED_TRACE(amount);
    Document document;
    const auto a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const auto b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const auto c = add_vertex(document, {2.0F, 2.0F, 0.0F});
    const auto d = add_vertex(document, {-0.5F, 2.0F, 0.0F});
    const auto e = add_vertex(document, {2.0F, 0.0F, -2.0F});
    const auto f = add_vertex(document, {2.0F, 2.0F, -2.0F});
    const auto g = add_vertex(document, {0.0F, 0.0F, -2.0F});
    const auto h = add_vertex(document, {-0.5F, 2.0F, -2.0F});
    const auto top = add_face(document, std::array{a, b, c, d});
    const auto right = add_face(document, std::array{b, e, f, c});
    const auto left = add_face(document, std::array{a, d, h, g});
    auto selection = qdr_test::face_selection({top, right, left}, top);

    const auto result = extrude_selected_faces(document, selection,
        FaceExtrudeOptions{.mode = FaceExtrudeMode::AlongNormals, .amount = amount});

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    const Face* front_wall = nullptr;
    for (const auto id : result.created.faces) {
      const Face* face = find_face(document, id);
      if (face != nullptr && face->vertices.size() == 4U &&
          face->vertices[0] == a && face->vertices[1] == b) {
        front_wall = face;
        break;
      }
    }
    ASSERT_NE(front_wall, nullptr);
    const auto origin = find_vertex(document, front_wall->vertices[0])->position;
    const auto edge = find_vertex(document, front_wall->vertices[1])->position - origin;
    const auto diagonal = find_vertex(document, front_wall->vertices[2])->position - origin;
    const auto connector = find_vertex(document, front_wall->vertices[3])->position - origin;
    // Different incident normals put the two connectors on skew lines.
    EXPECT_GT(std::abs(quader::dot(quader::cross(edge, diagonal), connector)), 0.01F);
    EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
    qdr_test::expect_valid_polygon_document_after_success(document, result);
  }
}

TEST(QdrDocumentOperationExtrudeFacesTests, StructuralAndNonfiniteSourceFailuresRejectAtomically) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    for (const int invalid_kind : {0, 1, 2}) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(invalid_kind);
      auto fixture = qdr_test::make_quad_document();
      const auto a = add_vertex(fixture.document, {10.0F, 0.0F, 0.0F});
      const auto b = add_vertex(fixture.document, {11.0F, 0.0F, 0.0F});
      const auto c = add_vertex(fixture.document, {11.0F, 1.0F, 0.0F});
      const auto d = add_vertex(fixture.document, {10.0F, 1.0F, 0.0F});
      const auto other = add_face(fixture.document, std::array{a, b, c, d});
      ASSERT_NE(other, kInvalidElementId);
      if (invalid_kind == 0) {
        find_face(fixture.document, other)->vertices.front() = 99999U;
      } else if (invalid_kind == 1) {
        find_face(fixture.document, other)->vertices.back() = a;
      } else {
        find_vertex(fixture.document, a)->position.x =
            std::numeric_limits<float>::infinity();
      }
      auto selection = qdr_test::face_selection({fixture.face}, fixture.face);
      const Document before = fixture.document;

      const auto result = extrude_selected_faces(
          fixture.document, selection,
          FaceExtrudeOptions{.mode = mode, .amount = 0.5F});

      EXPECT_FALSE(result.success);
      EXPECT_FALSE(result.changed);
      EXPECT_FALSE(result.message.empty());
      EXPECT_TRUE(result.created.empty());
      EXPECT_TRUE(result.deleted.empty());
      EXPECT_TRUE(result.affected.empty());
      EXPECT_EQ(selection.faces, (std::vector<ElementId>{fixture.face}));
      EXPECT_EQ(selection.active_face, fixture.face);
      EXPECT_EQ(fixture.document.next_vertex_id, before.next_vertex_id);
      EXPECT_EQ(fixture.document.next_face_id, before.next_face_id);
      ASSERT_EQ(fixture.document.faces.size(), before.faces.size());
      ASSERT_EQ(fixture.document.vertices.size(), before.vertices.size());
      for (std::size_t index = 0; index < before.faces.size(); ++index) {
        EXPECT_EQ(fixture.document.faces[index].id, before.faces[index].id);
        EXPECT_EQ(fixture.document.faces[index].vertices, before.faces[index].vertices);
      }
      for (std::size_t index = 0; index < before.vertices.size(); ++index) {
        EXPECT_EQ(fixture.document.vertices[index].id, before.vertices[index].id);
        EXPECT_FLOAT_EQ(fixture.document.vertices[index].position.x, before.vertices[index].position.x);
        EXPECT_FLOAT_EQ(fixture.document.vertices[index].position.y, before.vertices[index].position.y);
        EXPECT_FLOAT_EQ(fixture.document.vertices[index].position.z, before.vertices[index].position.z);
      }
    }
  }
}
