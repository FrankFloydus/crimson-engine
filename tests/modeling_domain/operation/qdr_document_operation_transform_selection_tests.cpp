////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("transform selection applies the supplied transform to selected vertices", "[operation][transform_selection]")
{
	SECTION("face selections scale all referenced vertices")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);
		Transform3 transform;
		transform.x_axis = { 2.0F, 0.0F, 0.0F };
		transform.y_axis = { 0.0F, 3.0F, 0.0F };
		transform.z_axis = { 0.0F, 0.0F, 1.0F };

		const OperationResult result = transform_selection(fixture.document, selection, transform);

		REQUIRE(result.changed);
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_left)->position, { -2.0F, -3.0F, 0.0F }));
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.top_right)->position, { 2.0F, 3.0F, 0.0F }));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	}

	SECTION("edge selections transform only the edge endpoints")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		const Vec3 unselected_before = find_vertex(fixture.document, fixture.top_right)->position;
		Selection selection = qdr_test::edge_selection({
			make_edge(fixture.bottom_left, fixture.bottom_right),
		});
		Transform3 transform;
		transform.origin = { 5.0F, 0.0F, 0.0F };

		const OperationResult result = transform_selection(fixture.document, selection, transform);

		REQUIRE(result.changed);
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_left)->position, { 4.0F, -1.0F, 0.0F }));
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_right)->position, { 6.0F, -1.0F, 0.0F }));
		REQUIRE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.top_right)->position, unselected_before));
	}

	SECTION("empty selections report no mutation")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection;
		selection.mode = SelectionMode::face;

		const OperationResult result = transform_selection(fixture.document, selection, {});

		REQUIRE_FALSE(result.changed);
		REQUIRE(result.message == "No polygon elements are selected.");
	}
}
