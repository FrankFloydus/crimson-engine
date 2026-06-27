////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_operation_test_helpers.hpp"

#include "test_support.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationUvFitYTests, UVFitYNormalizesOnlyTheSelectedVRange) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	qdr_test::assign_offset_uvs(*face);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "fit_y", 64.0F);

	EXPECT_TRUE(result.changed);
	face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	const qdr_test::UvBounds bounds = qdr_test::uv_bounds(*face);
	EXPECT_NEAR(bounds.min_u, 2.0F, 0.0001);
	EXPECT_NEAR(bounds.max_u, 4.0F, 0.0001);
	EXPECT_NEAR(bounds.min_v, 0.0F, 0.0001);
	EXPECT_NEAR(bounds.max_v, 1.0F, 0.0001);
}
