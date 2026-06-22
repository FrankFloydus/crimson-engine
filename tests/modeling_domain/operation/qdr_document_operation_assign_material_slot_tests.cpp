////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("assign material slot operation updates selected faces", "[operation][material_slot]")
{
	Document document = make_cube({});
	REQUIRE(document.faces.size() >= 2U);
	const ElementId selected_face_id = document.faces[0].id;
	const ElementId untouched_face_id = document.faces[1].id;
	for (Face& face : document.faces) {
		face.material_slot = 7U;
	}

	const OperationResult result =
		assign_selected_face_material_slot(document, qdr_test::face_selection({ selected_face_id }, selected_face_id), 3U);

	REQUIRE(result.changed);
	REQUIRE(find_face(document, selected_face_id)->material_slot == 3U);
	REQUIRE(find_face(document, untouched_face_id)->material_slot == 7U);
}

TEST_CASE("assign material slot operation treats slot zero as a valid default", "[operation][material_slot]")
{
	Document document = make_cube({});
	REQUIRE_FALSE(document.faces.empty());
	const ElementId face_id = document.faces.front().id;
	for (Face& face : document.faces) {
		face.material_slot = 5U;
	}

	const OperationResult result =
		assign_selected_face_material_slot(document, qdr_test::face_selection({ face_id }, face_id), 0U);

	REQUIRE(result.changed);
	REQUIRE(find_face(document, face_id)->material_slot == 0U);
}

TEST_CASE("assign material slot operation supports one explicit face target", "[operation][material_slot]")
{
	Document document = make_cube({});
	REQUIRE(document.faces.size() >= 2U);
	const ElementId target_face_id = document.faces[1].id;
	const ElementId untouched_face_id = document.faces[0].id;

	const OperationResult result = assign_face_material_slot(document, target_face_id, 11U);

	REQUIRE(result.changed);
	REQUIRE(find_face(document, target_face_id)->material_slot == 11U);
	REQUIRE(find_face(document, untouched_face_id)->material_slot == 0U);
}
