////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationRadialAlignSelectionTests, RadialAlignSelectionMovesAnUnevenVertexRingOntoASharedRadius) {
	Document document;
	const ElementId a = add_vertex(document, { -2.0F, 0.0F, 0.0F });
	const ElementId b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId c = add_vertex(document, { 3.0F, 0.0F, 0.0F });
	const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	Selection selection = qdr_test::vertex_selection({ a, b, c, d });

	const OperationResult result = radial_align_selection(document, selection);

	EXPECT_TRUE(result.changed);
	const quader::QVec3 center = qdr_test::document_vertex_centroid(document);
	std::optional<float> radius;
	for (ElementId vertex_id : selection.vertices) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		EXPECT_TRUE(vertex != nullptr);
		const float distance = length(vertex->position - center);
		if (!radius.has_value()) {
			radius = distance;
		}
		EXPECT_NEAR(distance, *radius, 0.001F);
	}
}

TEST(QdrDocumentOperationRadialAlignSelectionTests, RadialAlignSelectionPreservesEdgeAndFaceSelectionOwnershipEdgeModeAlignsAllVerticesReferencedBySelectedEdges) {
	{
		Document document;
		const ElementId a = add_vertex(document, { -2.0F, 0.0F, 0.0F });
		const ElementId b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
		const ElementId c = add_vertex(document, { 3.0F, 0.0F, 0.0F });
		const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
                EXPECT_TRUE(
                    add_face(document, std::array<ElementId, 4>{a, b, c, d}) !=
                    kInvalidElementId);
                Selection selection = qdr_test::edge_selection({ make_edge(a, b), make_edge(b, c), make_edge(c, d), make_edge(d, a) });

		const OperationResult result = radial_align_selection(document, selection);

		EXPECT_TRUE(result.changed);
                EXPECT_TRUE(selection.mode == SelectionMode::Edge);
                EXPECT_TRUE(selection.edges.size() == 4U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}

	
}

TEST(QdrDocumentOperationRadialAlignSelectionTests, RadialAlignSelectionPreservesEdgeAndFaceSelectionOwnershipFaceModeAlignsAllVerticesFromTheSelectedFaceLoop) {
	

	{
		Document document;
		const ElementId a = add_vertex(document, { -2.0F, 0.0F, 0.0F });
		const ElementId b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
		const ElementId c = add_vertex(document, { 3.0F, 0.0F, 0.0F });
		const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
		const ElementId face = add_face(document, std::array<ElementId, 4> { a, b, c, d });
                EXPECT_TRUE(face != kInvalidElementId);
                Selection selection = qdr_test::face_selection({ face }, face);

		const OperationResult result = radial_align_selection(document, selection);

		EXPECT_TRUE(result.changed);
                EXPECT_TRUE(selection.mode == SelectionMode::Face);
                EXPECT_TRUE(selection.faces.size() == 1U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}
}

TEST(QdrDocumentOperationRadialAlignSelectionTests, RadialAlignSelectionKeepsInteriorPatchVerticesOffThePerimeter) {
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const ElementId bottom = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const ElementId left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId center = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId top = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           bottom_left, bottom, center,
                                           left}) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           bottom, bottom_right, right,
                                           center}) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           left, center, top,
                                           top_left}) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           center, right, top_right,
                                           top}) != kInvalidElementId);
        Selection selection = qdr_test::vertex_selection({
		bottom_left,
		bottom,
		bottom_right,
		left,
		center,
		right,
		top_left,
		top,
		top_right,
	});

	const OperationResult result = radial_align_selection(document, selection);

	EXPECT_TRUE(result.changed);
	const Vertex* center_vertex = find_vertex(document, center);
	EXPECT_TRUE(center_vertex != nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(center_vertex->position, { 0.0F, 0.0F, 0.0F }));

	std::optional<float> radius;
	for (const ElementId vertex_id : { bottom_left, bottom, bottom_right, left, right, top_left, top, top_right }) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		EXPECT_TRUE(vertex != nullptr);
		const float distance = length(vertex->position - center_vertex->position);
		EXPECT_TRUE(distance > 0.1F);
		if (!radius.has_value()) {
			radius = distance;
		}
		EXPECT_NEAR(distance, *radius, 0.001F);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationRadialAlignSelectionTests, RadialAlignSelectionKeepsASquareFaceIslandCardinallyPhased) {
	Document document;
	std::array<std::array<ElementId, 4>, 4> vertices {};
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			vertices[y][x] = add_vertex(document, {
				static_cast<float>(x) - 1.5F,
				static_cast<float>(y) - 1.5F,
				0.0F,
			});
		}
	}

	Selection selection;
        selection.mode = SelectionMode::Face;
        for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {
			const ElementId face = add_face(document, std::array<ElementId, 4> {
				vertices[y][x],
				vertices[y][x + 1],
				vertices[y + 1][x + 1],
				vertices[y + 1][x],
			});
                        EXPECT_TRUE(face != kInvalidElementId);
                        selection.faces.push_back(face);
		}
	}
	selection.has_active = true;
        selection.active_kind = ElementKind::Face;
        selection.active_face = selection.faces.back();

	const OperationResult result = radial_align_selection(document, selection);

	EXPECT_TRUE(result.changed);
        constexpr float kExpectedStepDegrees = 30.0F;
        constexpr float kExpectedPhaseDegrees = 15.0F;
        for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			if (x > 0 && x < 3 && y > 0 && y < 3) {
				continue;
			}
			const Vertex* vertex = find_vertex(document, vertices[y][x]);
			EXPECT_TRUE(vertex != nullptr);
			const float angle_degrees = std::atan2(vertex->position.y, vertex->position.x) * 180.0F / static_cast<float>(std::acos(-1.0));
                        const float nearest_step =
                            kExpectedPhaseDegrees +
                            (std::round(
                                 (angle_degrees - kExpectedPhaseDegrees) /
                                 kExpectedStepDegrees) *
                             kExpectedStepDegrees);
                        EXPECT_NEAR(angle_degrees, nearest_step, 0.01F);
		}
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}
