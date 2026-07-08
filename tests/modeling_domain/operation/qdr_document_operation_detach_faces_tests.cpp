////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>

using namespace quader_poly;

namespace {

Document make_two_quad_strip_document() {
  Document document;
  const ElementId bottom_left = add_vertex(document, {-1.0F, -1.0F, 0.0F});
  const ElementId bottom_mid = add_vertex(document, {0.0F, -1.0F, 0.0F});
  const ElementId bottom_right = add_vertex(document, {1.0F, -1.0F, 0.0F});
  const ElementId top_left = add_vertex(document, {-1.0F, 1.0F, 0.0F});
  const ElementId top_mid = add_vertex(document, {0.0F, 1.0F, 0.0F});
  const ElementId top_right = add_vertex(document, {1.0F, 1.0F, 0.0F});
  EXPECT_TRUE(add_face(document,
                       std::array<ElementId, 4>{bottom_left, bottom_mid,
                                                top_mid, top_left}) !=
              kInvalidElementId);
  EXPECT_TRUE(add_face(document,
                       std::array<ElementId, 4>{bottom_mid, bottom_right,
                                                top_right, top_mid}) !=
              kInvalidElementId);
  return document;
}

} // namespace

TEST(QdrDocumentOperationDetachFacesTests, DetachFacesDuplicatesSharedBoundaryVerticesAndKeepsOneObjectDocument) {
	Document document = make_two_quad_strip_document();
	EXPECT_TRUE(document.faces.size() == 2U);
	Selection selection = qdr_test::face_selection({ document.faces.front().id }, document.faces.front().id);
	const std::size_t vertex_count_before = document.vertices.size();
	const ElementId detached_face_id = selection.active_face;

	const OperationResult result = detach_selected_faces(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	EXPECT_TRUE(selection.active_face == detached_face_id);
	EXPECT_TRUE(face_perimeter_info(document, detached_face_id).has_only_open_edges());
	EXPECT_TRUE(qdr_test::open_edge_count(document) == 8U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}
