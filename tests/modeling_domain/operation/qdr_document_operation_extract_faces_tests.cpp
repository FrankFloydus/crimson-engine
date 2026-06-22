////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("extract faces removes source faces and returns a selected extracted document", "[operation][extract_faces]")
{
	Document document = make_cube({});
	Selection selection = qdr_test::face_selection({ document.faces.front().id }, document.faces.front().id);

	const ExtractFacesResult result = extract_selected_faces(document, selection);

	REQUIRE(result.changed);
	REQUIRE(document.faces.size() == 5U);
	REQUIRE(result.extracted_document.faces.size() == 1U);
	REQUIRE(result.extracted_selection.mode == SelectionMode::face);
	REQUIRE(result.extracted_selection.faces.size() == 1U);
	REQUIRE(result.extracted_selection.active_face == result.extracted_selection.faces.front());
	REQUIRE(find_face(result.extracted_document, result.extracted_selection.active_face) != nullptr);
	REQUIRE(face_perimeter_info(result.extracted_document, result.extracted_selection.active_face).has_only_open_edges());
	REQUIRE(qdr_test::open_edge_count(document) == 4U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(result.extracted_document));
}
