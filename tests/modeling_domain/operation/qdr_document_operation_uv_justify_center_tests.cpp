////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_operation_test_helpers.hpp"

#include "test_support.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationUvJustifyCenterTests, UVJustifyCenterCentersSelectedLoopBoundsOnHalfTile) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	qdr_test::assign_offset_uvs(*face);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "justify_center", 64.0F);

	EXPECT_TRUE(result.changed);
	const qdr_test::UvBounds bounds = qdr_test::uv_bounds(*face);
	EXPECT_NEAR(((bounds.min_u + bounds.max_u) * 0.5F), 0.5F, 0.0001);
	EXPECT_NEAR(((bounds.min_v + bounds.max_v) * 0.5F), 0.5F, 0.0001);
}
