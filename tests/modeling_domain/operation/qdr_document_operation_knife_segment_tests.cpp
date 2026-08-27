////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationKnifeSegmentTests, KnifeSegmentSplitsAQuadBetweenTwoBoundaryEdgeTargets) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* source_face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(source_face != nullptr);
	source_face->material_slot = 7;
	const FaceTextureTransform texture{{2.0F, 0.5F}, {-0.25F, 0.75F}, -30.0F};
	source_face->texture_transform = texture;
	ensure_face_uvs(fixture.document);
	const ElementId bottom_right = fixture.bottom_right;
        const KnifePointTarget previous{
            KnifePointTargetKind::ExistingEdge,
            kInvalidElementId,
            make_edge(fixture.bottom_left, fixture.bottom_right),
            0.25F,
            fixture.face,
            {},
        };
        const KnifePointTarget current{
            KnifePointTargetKind::ExistingEdge,
            kInvalidElementId,
            make_edge(fixture.top_right, fixture.top_left),
            0.75F,
            fixture.face,
            {},
        };
        const KnifeSegmentPreview preview = build_knife_segment_preview(fixture.document, previous, current);
	EXPECT_TRUE(preview.valid);
	EXPECT_TRUE(preview.overlay.lines.size() == 1U);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(preview.overlay.lines.front().start, { -0.5F, -1.0F, 0.0F }));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(preview.overlay.lines.front().end, { -0.5F, 1.0F, 0.0F }));
	Selection selection;
	const std::size_t vertex_count_before = fixture.document.vertices.size();

	const OperationResult result = knife_segment(fixture.document, selection, previous, current);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 2U);
	EXPECT_TRUE(selection.has_active);
        EXPECT_TRUE(selection.active_kind == ElementKind::Vertex);
        EXPECT_TRUE(selection.active_vertex == selection.vertices.back());
	EXPECT_TRUE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[0], selection.vertices[1])));
	EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, make_edge(selection.vertices[0], selection.vertices[1]), { -0.5F, -1.0F, 0.0F }, { -0.5F, 1.0F, 0.0F }));
	for (const Face& face : fixture.document.faces) {
		EXPECT_TRUE(face.material_slot == 7U);
		EXPECT_EQ(face.texture_transform, texture);
		EXPECT_TRUE(face.uvs.empty());
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));

        const KnifePointTarget inserted_previous{
            KnifePointTargetKind::InsertedVertex,
            selection.active_vertex,
            {},
            0.5F,
            kInvalidElementId,
            {},
        };
        const KnifePointTarget existing_vertex_current{
            KnifePointTargetKind::ExistingVertex,
            bottom_right,
            {},
            0.5F,
            kInvalidElementId,
            {},
        };
        const OperationResult continued_result = knife_segment(fixture.document, selection, inserted_previous, existing_vertex_current);
	EXPECT_TRUE(continued_result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, continued_result);
	EXPECT_TRUE(fixture.document.faces.size() == 3U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationKnifeSegmentTests, KnifeSegmentRejectsTwoTargetsOnTheSameSourceEdgeWithoutMutation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertices_before = fixture.document.vertices.size();
	const std::size_t faces_before = fixture.document.faces.size();
	const Edge edge = make_edge(fixture.bottom_left, fixture.bottom_right);
        const KnifePointTarget previous{KnifePointTargetKind::ExistingEdge,
                                        kInvalidElementId,
                                        edge,
                                        0.25F,
                                        fixture.face,
                                        {}};
        const KnifePointTarget current{KnifePointTargetKind::ExistingEdge,
                                       kInvalidElementId,
                                       edge,
                                       0.75F,
                                       fixture.face,
                                       {}};
        Selection selection;

	const OperationResult result = knife_segment(fixture.document, selection, previous, current);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == vertices_before);
	EXPECT_TRUE(fixture.document.faces.size() == faces_before);
}
