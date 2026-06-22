////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("recalculate normals orients selected closed mesh faces outside or inside", "[operation][recalculate_normals]")
{
	Document document = make_cube({});
	Selection selection = qdr_test::all_face_selection(document);
	REQUIRE(flip_selected_face_normals(document, selection).changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(document, false));

	const OperationResult outside = recalculate_selected_face_normals(document, selection, true);

	REQUIRE(outside.changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(document, true));

	const OperationResult inside = recalculate_selected_face_normals(document, selection, false);

	REQUIRE(inside.changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(document, false));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}
