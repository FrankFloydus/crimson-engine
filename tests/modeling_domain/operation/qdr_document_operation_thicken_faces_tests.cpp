////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>

using namespace quader_poly;

TEST(QdrDocumentOperationThickenFacesTests, ThickenFacesTurnsOneOpenFaceIntoAClosedThinVolume) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, 0.5F, false);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.vertices.size() == 4U);
	EXPECT_TRUE(result.created.faces.size() == 5U);
	EXPECT_TRUE(result.created.edges.empty());
	EXPECT_TRUE(result.deleted.empty());
	EXPECT_TRUE(result.affected.vertices.empty());
	EXPECT_TRUE(result.affected.edges.empty());
	EXPECT_TRUE(result.affected.faces.size() == 1U);
	EXPECT_TRUE(result.affected.faces.front() == fixture.face);
	EXPECT_TRUE(fixture.document.faces.size() == 6U);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 6U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_face == fixture.face);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationThickenFacesTests, NegativeThickenGrowsFromTheFixedSourceCapAndWindsInward) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	const ElementId original_face_id = selection.active_face;
	const Face* source_before = find_face(fixture.document, original_face_id);
	EXPECT_TRUE(source_before != nullptr);
	const quader::QVec3 source_normal = face_normal(fixture.document, *source_before);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, -0.5F, false);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 6U);
	EXPECT_TRUE(selection.has_active);
        EXPECT_TRUE(selection.active_kind == ElementKind::Face);
        EXPECT_TRUE(selection.active_face == original_face_id);
	const Face* source_cap = find_face(fixture.document, original_face_id);
	const auto offset_cap = std::find_if(fixture.document.faces.begin(), fixture.document.faces.end(), [&fixture](const Face& face) {
		return qdr_test::face_centroid(fixture.document, face).z > 0.49F;
	});
	EXPECT_TRUE(source_cap != nullptr);
	EXPECT_TRUE(offset_cap != fixture.document.faces.end());
	EXPECT_TRUE(dot(face_normal(fixture.document, *source_cap), source_normal) > 0.5F);
	EXPECT_TRUE(dot(face_normal(fixture.document, *offset_cap), source_normal) < -0.5F);
	EXPECT_TRUE(qdr_test::face_centroid(fixture.document, *source_cap).z < 0.01F);
	EXPECT_TRUE(qdr_test::face_centroid(fixture.document, *offset_cap).z > 0.49F);
	const auto front_side_face = std::find_if(fixture.document.faces.begin(), fixture.document.faces.end(), [&fixture](const Face& face) {
		const quader::QVec3 center = qdr_test::face_centroid(fixture.document, face);
		return center.y < -0.9F && center.z > 0.01F && center.z < 0.49F;
	});
	EXPECT_TRUE(front_side_face != fixture.document.faces.end());
	EXPECT_TRUE(dot(face_normal(fixture.document, *front_side_face), { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationThickenFacesTests, NegativeThickenIgnoresLegacyFromCenterMovement) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, -0.5F, true);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	const auto minmax = std::minmax_element(fixture.document.vertices.begin(), fixture.document.vertices.end(), [](const Vertex& left, const Vertex& right) {
		return left.position.z < right.position.z;
	});
	EXPECT_TRUE(minmax.first != fixture.document.vertices.end());
	EXPECT_TRUE(minmax.first->position.z < 0.01F);
	EXPECT_TRUE(minmax.second->position.z > 0.49F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
}
