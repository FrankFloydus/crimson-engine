////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

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
