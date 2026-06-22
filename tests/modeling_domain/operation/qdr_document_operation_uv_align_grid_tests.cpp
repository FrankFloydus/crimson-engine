////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("UV align grid clears explicit selected loop UVs", "[operation][uv_align_grid]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	qdr_test::assign_offset_uvs(*face);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "align_grid", 64.0F);

	REQUIRE(result.changed);
	face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.empty());
}

TEST_CASE("UV align grid clears UV loops created by fit and align face", "[operation][uv_align_grid]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	REQUIRE(apply_uv_operation(fixture.document, selection, "fit_both", 64.0F).changed);
	const Face* face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.size() == face->vertices.size());
	REQUIRE(apply_uv_operation(fixture.document, selection, "align_grid", 64.0F).changed);
	face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.empty());

	REQUIRE(apply_uv_operation(fixture.document, selection, "align_face", 64.0F).changed);
	face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.size() == face->vertices.size());
	REQUIRE(apply_uv_operation(fixture.document, selection, "align_grid", 64.0F).changed);
	face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.empty());
}
