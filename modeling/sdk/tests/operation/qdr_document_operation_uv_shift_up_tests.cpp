#include "uv_operation_test_helpers.h"

#include <catch2/catch_approx.hpp>

using namespace quader_poly;

TEST_CASE("UV shift up moves selected loops by the configured world amount", "[operation][uv_shift_up]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* face = find_face(fixture.document, fixture.face);
	REQUIRE(face != nullptr);
	qdr_test::assign_offset_uvs(*face);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = apply_uv_operation(fixture.document, selection, "shift_up", 2.0F);

	REQUIRE(result.changed);
	REQUIRE(qdr_test::uv_bounds(*face).min_v == Catch::Approx(2.0F));
}
