#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("inset faces creates an inner cap and rim faces", "[operation][inset_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = inset_selected_faces(fixture.document, selection, 0.25F);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.vertices.size() == 8U);
	REQUIRE(fixture.document.faces.size() == 5U);
	REQUIRE(selection.mode == SelectionMode::face);
	REQUIRE(selection.faces.size() == 1U);
	REQUIRE(selection.faces.front() == fixture.face);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}
