#include "uv_operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("UV operations reject unknown commands and empty selections without mutation", "[operation][uv_validation]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult unknown_result = apply_uv_operation(fixture.document, selection, "not_a_uv_operation", 64.0F);
	REQUIRE_FALSE(unknown_result.changed);
	REQUIRE(unknown_result.message == "Unknown UV operation.");

	Selection empty_selection;
	empty_selection.mode = SelectionMode::face;
	const OperationResult empty_result = apply_uv_operation(fixture.document, empty_selection, "align_face", 64.0F);
	REQUIRE_FALSE(empty_result.changed);
	REQUIRE(empty_result.message == "Select a face, edge, vertex, or object first.");
}
