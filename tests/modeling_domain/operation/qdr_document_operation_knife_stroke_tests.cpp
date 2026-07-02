////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationKnifeStrokeTests, KnifeStrokeAppliesConnectedStrokeSegmentsInOneOperation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
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
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 3U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
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
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 3U);
	EXPECT_TRUE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[0], selection.vertices[1])));
	EXPECT_TRUE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[1], selection.vertices[2])));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationKnifeStrokeTests, KnifeStrokeRoutesAdjacentFaceCutsThroughTheSharedBoundary) {
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

	const OperationResult result = knife_stroke(document, selection, points, segments);

	EXPECT_TRUE(result.changed);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 2U);
	const auto boundary = std::find_if(document.vertices.begin(), document.vertices.end(), [](const Vertex& vertex) {
		return qdr_test::vec_nearly_equal(vertex.position, { 0.0F, 0.0F, 0.0F });
	});
        const ElementId boundary_vertex = boundary != document.vertices.end()
                                              ? boundary->id
                                              : kInvalidElementId;
        EXPECT_TRUE(boundary_vertex != kInvalidElementId);
        EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(selection.vertices[0], boundary_vertex)));
	EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(boundary_vertex, selection.vertices[1])));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
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
