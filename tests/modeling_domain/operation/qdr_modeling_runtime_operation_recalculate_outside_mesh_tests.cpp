////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

TEST_CASE("recalculate outside mesh operation restores outward normals on selected objects", "[operation][recalculate_outside_mesh]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	REQUIRE(session.flip_selected_face_normals().changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, false));

	const quader_editor_core::CommandResult result = session.recalculate_selected_face_normals(true);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, true));
}
