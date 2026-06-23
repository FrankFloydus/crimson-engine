////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("translate selection moves only vertices referenced by the active selection", "[operation][translate_selection]")
{
	SECTION("face selections translate every face vertex")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		const quader::QVec3 bottom_left_before = find_vertex(fixture.document, fixture.bottom_left)->position;
		Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

		const OperationResult result = translate_selection(fixture.document, selection, { 2.0F, -1.0F, 0.5F });

		REQUIRE(result.changed);
		const Vertex* bottom_left = find_vertex(fixture.document, fixture.bottom_left);
		REQUIRE(bottom_left != nullptr);
		REQUIRE(qdr_test::vec_nearly_equal(bottom_left->position, bottom_left_before + quader::QVec3 { 2.0F, -1.0F, 0.5F }));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	}

	SECTION("vertex selections leave unselected vertices untouched")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		const quader::QVec3 selected_before = find_vertex(fixture.document, fixture.bottom_left)->position;
		const quader::QVec3 unselected_before = find_vertex(fixture.document, fixture.bottom_right)->position;
		Selection selection = qdr_test::vertex_selection({ fixture.bottom_left }, fixture.bottom_left);

		const OperationResult result = translate_selection(fixture.document, selection, { 0.0F, 3.0F, 0.0F });

		REQUIRE(result.changed);
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_left)->position, selected_before + quader::QVec3 { 0.0F, 3.0F, 0.0F }));
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_right)->position, unselected_before));
	}

	SECTION("empty selections report no mutation")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection;
		selection.mode = SelectionMode::vertex;

		const OperationResult result = translate_selection(fixture.document, selection, { 1.0F, 0.0F, 0.0F });

		REQUIRE_FALSE(result.changed);
		REQUIRE(result.message == "No polygon elements are selected.");
	}
}
