#include "uv_operation_test_helpers.h"

#include <catch2/catch_approx.hpp>

using namespace quader_poly;

TEST_CASE("UV fit x normalizes only the selected U range", "[operation][uv_fit_x]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	qdr_test::assign_offset_uvs(*face);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "fit_x", 64.0F);

	REQUIRE(result.changed);
	face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	const qdr_test::UvBounds bounds = qdr_test::uv_bounds(*face);
	REQUIRE(bounds.min_u == Catch::Approx(0.0F));
	REQUIRE(bounds.max_u == Catch::Approx(1.0F));
	REQUIRE(bounds.min_v == Catch::Approx(3.0F));
	REQUIRE(bounds.max_v == Catch::Approx(7.0F));
}
