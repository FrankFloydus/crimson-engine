////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("UV align face bakes selected face-local loop UVs", "[operation][uv_align_face]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(3);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "align_face", 64.0F);

	REQUIRE(result.changed);
	const Face* face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.size() == face->vertices.size());
	const CompiledMesh mesh = compile_document(fixture.document, SurfaceShading::smooth);
	REQUIRE(mesh.indices.size() == 6U);
	REQUIRE(mesh.primitives.size() == 1U);
	REQUIRE(mesh.primitives.front().material_slot == 3U);
}

TEST_CASE("UV align face resolves target faces from component selections", "[operation][uv_align_face]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection vertex_selection = qdr_test::vertex_selection({ fixture.bottom_left }, fixture.bottom_left);

	const OperationResult result = apply_uv_operation(fixture.document, vertex_selection, "align_face", 64.0F);

	REQUIRE(result.changed);
	const Face* face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	REQUIRE(face->uvs.size() == face->vertices.size());
}
