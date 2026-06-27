////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

#include <algorithm>

using namespace quader_poly;

namespace {

std::size_t count_vertices_at(const Document& document, quader::QVec3 position)
{
	return static_cast<std::size_t>(std::count_if(document.vertices.begin(), document.vertices.end(), [position](const Vertex& vertex) {
		return qdr_test::vec_nearly_equal(vertex.position, position);
	}));
}

Document make_triangle_document(ElementId& face_id)
{
	Document document;
	const ElementId a = add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const ElementId b = add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const ElementId c = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const std::array<ElementId, 3> vertices { a, b, c };
	face_id = add_face(document, vertices, 3U);
	return document;
}

ElementId find_face_with_normal(const Document& document, quader::QVec3 direction)
{
	const auto face = std::ranges::find_if(document.faces, [&document, direction](const Face& candidate) {
		return dot(face_normal(document, candidate), direction) > 0.5F;
	});
	return face == document.faces.end() ? invalid_element_id : face->id;
}

} // namespace

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadSubdividesASelectedQuadIntoAnXAndYGrid) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(11);
	Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	face->uvs = {
		{ 0.0F, 0.0F },
		{ 1.0F, 0.0F },
		{ 1.0F, 1.0F },
		{ 0.0F, 1.0F },
	};
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = slice_selected_quads(fixture.document, selection, 1, 1);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == 9U);
	EXPECT_TRUE(fixture.document.faces.size() == 4U);
	EXPECT_TRUE(count_vertices_at(fixture.document, { 0.0F, 0.0F, 0.0F }) == 1U);
	EXPECT_TRUE(selection.mode == SelectionMode::face);
	EXPECT_TRUE(selection.faces.size() == 4U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_kind == ElementKind::face);
	EXPECT_TRUE(selection.active_face == selection.faces.back());
	for (const Face& sliced_face : fixture.document.faces) {
		EXPECT_TRUE(sliced_face.vertices.size() == 4U);
		EXPECT_TRUE(sliced_face.material_slot == 11U);
		EXPECT_TRUE(sliced_face.uvs.empty());
		EXPECT_FALSE(qdr_test::face_has_repeated_vertices(sliced_face));
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadSupportsOneAxisStripsAndZeroCountNoOpPreviewsXSlicesCreateVerticalStrips) {
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

		const OperationResult result = slice_selected_quads(fixture.document, selection, 2, 0);

		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(fixture.document.faces.size() == 3U);
		EXPECT_TRUE(fixture.document.vertices.size() == 8U);
		EXPECT_TRUE(selection.faces.size() == 3U);
		EXPECT_TRUE(count_vertices_at(fixture.document, { -1.0F / 3.0F, -1.0F, 0.0F }) == 1U);
		EXPECT_TRUE(count_vertices_at(fixture.document, { 1.0F / 3.0F, 1.0F, 0.0F }) == 1U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	}

	

	
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadSupportsOneAxisStripsAndZeroCountNoOpPreviewsYSlicesCreateHorizontalStrips) {
	

	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

		const OperationResult result = slice_selected_quads(fixture.document, selection, 0, 2);

		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(fixture.document.faces.size() == 3U);
		EXPECT_TRUE(fixture.document.vertices.size() == 8U);
		EXPECT_TRUE(selection.faces.size() == 3U);
		EXPECT_TRUE(count_vertices_at(fixture.document, { -1.0F, -1.0F / 3.0F, 0.0F }) == 1U);
		EXPECT_TRUE(count_vertices_at(fixture.document, { 1.0F, 1.0F / 3.0F, 0.0F }) == 1U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	}

	
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadSupportsOneAxisStripsAndZeroCountNoOpPreviewsZeroOnBothAxesPreservesTopology) {
	

	

	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

		const OperationResult result = slice_selected_quads(fixture.document, selection, 0, 0);

		EXPECT_FALSE(result.changed);
		EXPECT_TRUE(fixture.document.faces.size() == 1U);
		EXPECT_TRUE(fixture.document.vertices.size() == 4U);
		EXPECT_TRUE(selection.faces.size() == 1U);
	}
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadUsesStableAxesForCoplanarQuadsWithDifferentLoopStarts) {
	Document document;
	const ElementId left_bottom_left = add_vertex(document, { -2.0F, 1.0F, -1.0F });
	const ElementId left_top_left = add_vertex(document, { -2.0F, 1.0F, 1.0F });
	const ElementId left_top_right = add_vertex(document, { 0.0F, 1.0F, 1.0F });
	const ElementId left_bottom_right = add_vertex(document, { 0.0F, 1.0F, -1.0F });
	const ElementId right_bottom_left = add_vertex(document, { 1.0F, 1.0F, -1.0F });
	const ElementId right_top_left = add_vertex(document, { 1.0F, 1.0F, 1.0F });
	const ElementId right_top_right = add_vertex(document, { 3.0F, 1.0F, 1.0F });
	const ElementId right_bottom_right = add_vertex(document, { 3.0F, 1.0F, -1.0F });

	const ElementId left_face = add_face(document, std::array<ElementId, 4> {
		left_bottom_left,
		left_top_left,
		left_top_right,
		left_bottom_right,
	});
	const ElementId right_face = add_face(document, std::array<ElementId, 4> {
		right_top_left,
		right_top_right,
		right_bottom_right,
		right_bottom_left,
	});
	EXPECT_TRUE(left_face != invalid_element_id);
	EXPECT_TRUE(right_face != invalid_element_id);
	EXPECT_TRUE(dot(face_normal(document, *find_face(document, left_face)), { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(dot(face_normal(document, *find_face(document, right_face)), { 0.0F, 1.0F, 0.0F }) > 0.5F);
	Selection selection = qdr_test::face_selection({ left_face, right_face }, right_face);

	const OperationResult result = slice_selected_quads(document, selection, 1, 2);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.faces.size() == 12U);
	EXPECT_TRUE(document.vertices.size() == 24U);
	EXPECT_TRUE(count_vertices_at(document, { -1.0F, 1.0F, 1.0F / 3.0F }) == 1U);
	EXPECT_TRUE(count_vertices_at(document, { -1.0F, 1.0F, -1.0F / 3.0F }) == 1U);
	EXPECT_TRUE(count_vertices_at(document, { 2.0F, 1.0F, 1.0F / 3.0F }) == 1U);
	EXPECT_TRUE(count_vertices_at(document, { 2.0F, 1.0F, -1.0F / 3.0F }) == 1U);
	EXPECT_TRUE(count_vertices_at(document, { -4.0F / 3.0F, 1.0F, 0.0F }) == 0U);
	EXPECT_TRUE(count_vertices_at(document, { 5.0F / 3.0F, 1.0F, 0.0F }) == 0U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadKeepsGeneratedRightSideWallCellsOrientedWithTheSourceFace) {
	Document document;
	const ElementId bottom_back = add_vertex(document, { 1.0F, -1.0F, -1.0F });
	const ElementId bottom_front = add_vertex(document, { 1.0F, -1.0F, 1.0F });
	const ElementId top_front = add_vertex(document, { 1.0F, 1.0F, 1.0F });
	const ElementId top_back = add_vertex(document, { 1.0F, 1.0F, -1.0F });
	const ElementId right_face = add_face(document, std::array<ElementId, 4> {
		bottom_back,
		bottom_front,
		top_front,
		top_back,
	});
	EXPECT_TRUE(right_face != invalid_element_id);
	const quader::QVec3 source_normal = face_normal(document, *find_face(document, right_face));
	EXPECT_TRUE(dot(source_normal, { -1.0F, 0.0F, 0.0F }) > 0.5F);
	Selection selection = qdr_test::face_selection({ right_face }, right_face);

	const OperationResult result = slice_selected_quads(document, selection, 1, 1);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.faces.size() == 4U);
	EXPECT_TRUE(selection.faces.size() == 4U);
	for (const Face& face : document.faces) {
		EXPECT_TRUE(dot(face_normal(document, face), source_normal) > 0.5F);
	}
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadPropagatesSelectedCubeFaceBoundarySplitsIntoNeighboringFaces) {
	Document document = make_cube({});
	const ElementId top_face = find_face_with_normal(document, { 0.0F, 1.0F, 0.0F });
	EXPECT_TRUE(top_face != invalid_element_id);
	Selection selection = qdr_test::face_selection({ top_face }, top_face);

	const OperationResult result = slice_selected_quads(document, selection, 3, 3);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(selection.faces.size() == 16U);
	EXPECT_TRUE(qdr_test::open_edge_count(document) == 0U);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationSliceQuadTests, SliceQuadRejectsNonQuadSelectedFacesWithoutPartialEdits) {
	ElementId triangle_face = invalid_element_id;
	Document document = make_triangle_document(triangle_face);
	Selection selection = qdr_test::face_selection({ triangle_face }, triangle_face);

	const OperationResult result = slice_selected_quads(document, selection, 1, 1);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(document.faces.size() == 1U);
	EXPECT_TRUE(document.vertices.size() == 3U);
	EXPECT_TRUE(selection.mode == SelectionMode::face);
	EXPECT_TRUE(selection.faces.size() == 1U);
	EXPECT_TRUE(selection.faces.front() == triangle_face);
}
