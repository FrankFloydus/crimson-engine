////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationUvAlignGridTests, UVAlignGridClearsExplicitSelectedLoopUVs) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	qdr_test::assign_offset_uvs(*face);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "align_grid", 64.0F);

	EXPECT_TRUE(result.changed);
	face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	EXPECT_TRUE(face->uvs.empty());
}

TEST(QdrDocumentOperationUvAlignGridTests, UVAlignGridClearsUVLoopsCreatedByFitAndAlignFace) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	EXPECT_TRUE(apply_uv_operation(fixture.document, selection, "fit_both", 64.0F).changed);
	const Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	EXPECT_TRUE(face->uvs.size() == face->vertices.size());
	EXPECT_TRUE(apply_uv_operation(fixture.document, selection, "align_grid", 64.0F).changed);
	face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	EXPECT_TRUE(face->uvs.empty());

	EXPECT_TRUE(apply_uv_operation(fixture.document, selection, "align_face", 64.0F).changed);
	face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	EXPECT_TRUE(face->uvs.size() == face->vertices.size());
	EXPECT_TRUE(apply_uv_operation(fixture.document, selection, "align_grid", 64.0F).changed);
	face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	EXPECT_TRUE(face->uvs.empty());
}
