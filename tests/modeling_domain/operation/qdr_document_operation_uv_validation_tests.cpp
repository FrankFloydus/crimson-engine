////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationUvValidationTests, UVOperationsRejectUnknownCommandsAndEmptySelectionsWithoutMutation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult unknown_result = apply_uv_operation(fixture.document, selection, "not_a_uv_operation", 64.0F);
	EXPECT_FALSE(unknown_result.changed);
	EXPECT_TRUE(unknown_result.message == "Unknown UV operation.");

	Selection empty_selection;
        empty_selection.mode = SelectionMode::Face;
        const OperationResult empty_result = apply_uv_operation(fixture.document, empty_selection, "align_face", 64.0F);
	EXPECT_FALSE(empty_result.changed);
	EXPECT_TRUE(empty_result.message == "Select a face, edge, vertex, or object first.");
}
