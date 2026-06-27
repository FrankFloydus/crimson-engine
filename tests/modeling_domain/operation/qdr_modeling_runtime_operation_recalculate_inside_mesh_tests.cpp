////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationRecalculateInsideMeshTests, RecalculateInsideMeshOperationOrientsSelectedObjectNormalsInward) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	EXPECT_TRUE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, true));

	const quader_editor_core::CommandResult result = session.recalculate_selected_face_normals(false);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, false));
}
