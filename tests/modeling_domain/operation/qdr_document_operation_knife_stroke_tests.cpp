////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

namespace {

qdr_test::QuadFixture attributed_knife_quad(bool reverse_winding) {
  auto fixture = qdr_test::make_quad_document(7U);
  Face &face = fixture.document.faces.front();
  face.normal_shading = SurfaceShading::Smooth;
  face.texture_transform = {{2.0F, 0.5F}, {-0.25F, 0.75F}, -30.0F};
  face.uvs = {{2.0F, 3.0F}, {6.0F, 3.0F}, {6.0F, 9.0F}, {2.0F, 9.0F}};
  if (reverse_winding) {
    std::ranges::reverse(face.vertices);
    std::ranges::reverse(face.uvs);
  }
  return fixture;
}

void expect_knife_partition_attributes(const Document &document,
                                      const Face &source_face,
                                      quader::QVec3 source_normal,
                                      std::size_t face_count) {
  ASSERT_EQ(document.faces.size(), face_count);
  qdr_test::expect_valid_polygon_document(document, "Knife partition");
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
  EXPECT_NE(find_face(document, source_face.id), nullptr);
  double total_area = 0.0;
  for (const Face &face : document.faces) {
    EXPECT_GT(quader::dot(face_normal(document, face), source_normal), 0.99F);
    EXPECT_EQ(face.material_slot, source_face.material_slot);
    EXPECT_EQ(face.normal_shading, source_face.normal_shading);
    EXPECT_EQ(face.texture_transform, source_face.texture_transform);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const Vertex *vertex = find_vertex(document, face.vertices[corner]);
      const Vertex *next = find_vertex(
          document, face.vertices[(corner + 1U) % face.vertices.size()]);
      ASSERT_NE(vertex, nullptr);
      ASSERT_NE(next, nullptr);
      total_area += 0.5 * quader::dot(
          quader::cross(vertex->position, next->position), source_normal);
      EXPECT_NEAR(face.uvs[corner].x, vertex->position.x * 2.0F + 4.0F, 0.00001F);
      EXPECT_NEAR(face.uvs[corner].y, vertex->position.y * 3.0F + 6.0F, 0.00001F);
    }
  }
  EXPECT_NEAR(total_area, 4.0, 0.00001);
}

void expect_knife_edge_has_two_faces(const Document &document,
                                    quader::QVec3 from, quader::QVec3 to) {
  const ElementId first = qdr_test::vertex_at_position(document, from);
  const ElementId second = qdr_test::vertex_at_position(document, to);
  ASSERT_NE(first, kInvalidElementId);
  ASSERT_NE(second, kInvalidElementId);
  const Edge expected = make_edge(first, second);
  std::size_t incidence = 0;
  for (const Face &face : document.faces) {
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const Edge edge = make_edge(
          face.vertices[corner],
          face.vertices[(corner + 1U) % face.vertices.size()]);
      incidence += edge == expected ? 1U : 0U;
    }
  }
  EXPECT_EQ(incidence, 2U);
}

} // namespace

TEST(QdrDocumentOperationKnifeStrokeTests,
     KnifeStrokePartitionsRepeatedBoundaryZigzagsForBothWindings) {
  for (const bool reverse_winding : {false, true}) {
    SCOPED_TRACE(reverse_winding);
    auto fixture = attributed_knife_quad(reverse_winding);
    const Face source = fixture.document.faces.front();
    const auto normal = face_normal(fixture.document, source);
    const Edge top{fixture.top_left, fixture.top_right};
    const Edge bottom{fixture.bottom_left, fixture.bottom_right};
    const std::array points{
        KnifePointTarget{KnifePointTargetKind::ExistingEdge, kInvalidElementId,
                         top, 0.125F, fixture.face, {}},
        KnifePointTarget{KnifePointTargetKind::ExistingEdge, kInvalidElementId,
                         bottom, 0.375F, fixture.face, {}},
        KnifePointTarget{KnifePointTargetKind::ExistingEdge, kInvalidElementId,
                         top, 0.625F, fixture.face, {}},
        KnifePointTarget{KnifePointTargetKind::ExistingEdge, kInvalidElementId,
                         bottom, 0.875F, fixture.face, {}},
    };
    const std::array<KnifeStrokeSegment, 3> segments{{{0U, 1U}, {1U, 2U}, {2U, 3U}}};
    Selection selection;
    const OperationResult result = knife_stroke(fixture.document, selection,
                                               points, segments);
    ASSERT_TRUE(result.changed) << result.message;
    expect_knife_partition_attributes(fixture.document, source, normal, 4U);
    ASSERT_EQ(selection.vertices.size(), 4U);
    EXPECT_EQ(selection.active_vertex, selection.vertices.back());
    const std::array positions{
        quader::QVec3{-0.75F, 1.0F, 0.0F}, quader::QVec3{-0.25F, -1.0F, 0.0F},
        quader::QVec3{0.25F, 1.0F, 0.0F}, quader::QVec3{0.75F, -1.0F, 0.0F}};
    for (std::size_t index = 1; index < positions.size(); ++index) {
      expect_knife_edge_has_two_faces(fixture.document, positions[index - 1U],
                                      positions[index]);
    }
  }
}

TEST(QdrDocumentOperationKnifeStrokeTests,
     KnifeStrokeCanRevisitAnEarlierBoundaryPoint) {
  for (const bool reverse_winding : {false, true}) {
    SCOPED_TRACE(reverse_winding);
    auto fixture = attributed_knife_quad(reverse_winding);
    const Face source = fixture.document.faces.front();
    const auto normal = face_normal(fixture.document, source);
    const std::array positions{
        quader::QVec3{-0.75F, 1.0F, 0.0F}, quader::QVec3{0.0F, -1.0F, 0.0F},
        quader::QVec3{0.0F, 1.0F, 0.0F}, quader::QVec3{0.75F, 1.0F, 0.0F}};
    std::vector<KnifePointTarget> points;
    for (const auto position : positions) {
      points.push_back({KnifePointTargetKind::FacePoint, kInvalidElementId, {},
                        0.5F, fixture.face, position});
    }
    const std::array<KnifeStrokeSegment, 4> segments{{
        {0U, 1U}, {1U, 2U}, {2U, 1U}, {1U, 3U}}};
    Selection selection;
    const OperationResult result = knife_stroke(fixture.document, selection,
                                               points, segments);
    ASSERT_TRUE(result.changed) << result.message;
    expect_knife_partition_attributes(fixture.document, source, normal, 4U);
    EXPECT_EQ(selection.vertices.size(), 4U);
    for (const std::size_t other : {0U, 2U, 3U}) {
      expect_knife_edge_has_two_faces(fixture.document, positions[1], positions[other]);
    }
  }
}

TEST(QdrDocumentOperationKnifeStrokeTests,
     KnifeStrokeSplitsCrossingBoundaryCutsForBothWindings) {
  for (const bool reverse_winding : {false, true}) {
    SCOPED_TRACE(reverse_winding);
    auto fixture = attributed_knife_quad(reverse_winding);
    const Face source = fixture.document.faces.front();
    const auto normal = face_normal(fixture.document, source);
    const std::array positions{
        quader::QVec3{-0.5F, 1.0F, 0.0F}, quader::QVec3{0.5F, -1.0F, 0.0F},
        quader::QVec3{0.5F, 1.0F, 0.0F}, quader::QVec3{-0.5F, -1.0F, 0.0F}};
    std::vector<KnifePointTarget> points;
    for (const auto position : positions) {
      points.push_back({KnifePointTargetKind::FacePoint, kInvalidElementId, {},
                        0.5F, fixture.face, position});
    }
    const std::array<KnifeStrokeSegment, 3> segments{{{0U, 1U}, {1U, 2U}, {2U, 3U}}};
    Selection selection;
    const OperationResult result = knife_stroke(fixture.document, selection,
                                               points, segments);
    ASSERT_TRUE(result.changed) << result.message;
    expect_knife_partition_attributes(fixture.document, source, normal, 5U);
    EXPECT_EQ(selection.vertices.size(), 4U);
    for (const auto position : positions) {
      expect_knife_edge_has_two_faces(fixture.document, position, {});
    }
    expect_knife_edge_has_two_faces(fixture.document, positions[1], positions[2]);
  }
}

TEST(QdrDocumentOperationKnifeStrokeTests,
     KnifeStrokeRetainsOpenAndClosedInteriorPartitionsForBothWindings) {
  for (const bool closed : {false, true}) {
    for (const bool reverse_winding : {false, true}) {
      SCOPED_TRACE(closed);
      SCOPED_TRACE(reverse_winding);
      auto fixture = attributed_knife_quad(reverse_winding);
      const Face source = fixture.document.faces.front();
      const auto normal = face_normal(fixture.document, source);
      const std::vector<quader::QVec3> positions = closed
          ? std::vector<quader::QVec3>{{-0.5F, -0.5F, 0.0F}, {0.5F, -0.5F, 0.0F},
                                       {0.5F, 0.5F, 0.0F}, {-0.5F, 0.5F, 0.0F}}
          : std::vector<quader::QVec3>{{-1.0F, 0.75F, 0.0F}, {-0.5F, -0.75F, 0.0F},
                                       {0.0F, 0.5F, 0.0F}, {0.5F, -0.75F, 0.0F},
                                       {1.0F, 0.75F, 0.0F}};
      std::vector<KnifePointTarget> points;
      std::vector<KnifeStrokeSegment> segments;
      for (std::size_t index = 0; index < positions.size(); ++index) {
        points.push_back({KnifePointTargetKind::FacePoint, kInvalidElementId, {},
                          0.5F, fixture.face, positions[index]});
        if (index > 0U) {
          segments.push_back({static_cast<std::uint32_t>(index - 1U),
                              static_cast<std::uint32_t>(index)});
        }
      }
      if (closed) {
        segments.push_back({static_cast<std::uint32_t>(positions.size() - 1U), 0U});
      }
      Selection selection;
      const OperationResult result = knife_stroke(fixture.document, selection,
                                                 points, segments);
      ASSERT_TRUE(result.changed) << result.message;
      expect_knife_partition_attributes(fixture.document, source, normal,
                                        closed ? 3U : 2U);
      EXPECT_EQ(selection.vertices.size(), positions.size());
      for (const auto segment : segments) {
        expect_knife_edge_has_two_faces(fixture.document, positions[segment.first_point],
                                        positions[segment.second_point]);
      }
    }
  }
}

TEST(QdrDocumentOperationKnifeStrokeTests, KnifeStrokeAppliesConnectedStrokeSegmentsInOneOperation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	fixture.document.faces.front().uvs = {
		{2.0F, 3.0F}, {6.0F, 3.0F}, {6.0F, 9.0F}, {2.0F, 9.0F}};
        const std::vector<KnifePointTarget> points{
            {
                KnifePointTargetKind::ExistingEdge,
                kInvalidElementId,
                make_edge(fixture.bottom_left, fixture.bottom_right),
                0.5F,
                fixture.face,
                {},
            },
            {
                KnifePointTargetKind::FacePoint,
                kInvalidElementId,
                {},
                0.5F,
                fixture.face,
                {0.0F, 0.0F, 0.0F},
            },
            {
                KnifePointTargetKind::ExistingEdge,
                kInvalidElementId,
                make_edge(fixture.top_right, fixture.top_left),
                0.5F,
                fixture.face,
                {},
            },
        };
        const std::vector<KnifeStrokeSegment> segments {
		{ 0U, 1U },
		{ 1U, 2U },
	};
	Selection selection;

	const OperationResult result = knife_stroke(fixture.document, selection, points, segments);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 3U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	for (const Face& face : fixture.document.faces) {
		ASSERT_EQ(face.uvs.size(), face.vertices.size());
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			const Vertex* vertex = find_vertex(fixture.document, face.vertices[corner]);
			ASSERT_NE(vertex, nullptr);
			EXPECT_FLOAT_EQ(face.uvs[corner].x, vertex->position.x * 2.0F + 4.0F);
			EXPECT_FLOAT_EQ(face.uvs[corner].y, vertex->position.y * 3.0F + 6.0F);
		}
	}
}

TEST(QdrDocumentOperationKnifeStrokeTests, KnifeStrokeSupportsTwoSplitPointsOnTheSameSourceEdge) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
        const std::vector<KnifePointTarget> points{
            {
                KnifePointTargetKind::ExistingEdge,
                kInvalidElementId,
                make_edge(fixture.bottom_left, fixture.bottom_right),
                0.25F,
                fixture.face,
                {-0.5F, -1.0F, 0.0F},
            },
            {
                KnifePointTargetKind::FacePoint,
                kInvalidElementId,
                {},
                0.5F,
                fixture.face,
                {0.0F, -0.25F, 0.0F},
            },
            {
                KnifePointTargetKind::ExistingEdge,
                kInvalidElementId,
                Edge{fixture.bottom_right, fixture.bottom_left},
                0.25F,
                fixture.face,
                {0.5F, -1.0F, 0.0F},
            },
        };
        const std::vector<KnifeStrokeSegment> segments {
		{ 0U, 1U },
		{ 1U, 2U },
	};
	Selection selection;

	const OperationResult result = knife_stroke(fixture.document, selection, points, segments);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 3U);
	EXPECT_TRUE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[0], selection.vertices[1])));
	EXPECT_TRUE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[1], selection.vertices[2])));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationKnifeStrokeTests, KnifeStrokeRejectsAOneClickCutAcrossAdjacentFaces) {
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const ElementId bottom_center = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId top_center = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId left_face = add_face(document, std::array<ElementId, 4> { bottom_left, bottom_center, top_center, top_left });
	const ElementId right_face = add_face(document, std::array<ElementId, 4> { bottom_center, bottom_right, top_right, top_center });
        EXPECT_TRUE(left_face != kInvalidElementId);
        EXPECT_TRUE(right_face != kInvalidElementId);

        const std::vector<KnifePointTarget> points{
            {
                KnifePointTargetKind::FacePoint,
                kInvalidElementId,
                {},
                0.5F,
                left_face,
                {-0.5F, 0.0F, 0.0F},
            },
            {
                KnifePointTargetKind::FacePoint,
                kInvalidElementId,
                {},
                0.5F,
                right_face,
                {0.5F, 0.0F, 0.0F},
            },
        };
        const std::vector<KnifeStrokeSegment> segments { { 0U, 1U } };
	Selection selection;
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = knife_stroke(document, selection, points, segments);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(result.message.find("one face") != std::string::npos);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before);
	EXPECT_TRUE(document.faces.size() == face_count_before);
	EXPECT_TRUE(selection.vertices.empty());
}

TEST(QdrDocumentOperationKnifeStrokeTests, KnifeStrokeRejectsIncompleteStrokesWithoutMutation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertex_count_before = fixture.document.vertices.size();
	const std::size_t face_count_before = fixture.document.faces.size();
	Selection selection;

	const OperationResult result = knife_stroke(fixture.document, selection, {}, {});

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before);
	EXPECT_TRUE(fixture.document.faces.size() == face_count_before);
}
