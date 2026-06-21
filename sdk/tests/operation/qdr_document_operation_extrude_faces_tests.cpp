#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("extrude faces creates a cap and side walls while keeping the cap selected", "[operation][extrude_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(11);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = extrude_selected_faces(fixture.document, selection, 0.5F);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.vertices.size() == 8U);
	REQUIRE(fixture.document.faces.size() == 5U);
	REQUIRE(selection.mode == SelectionMode::face);
	REQUIRE(selection.faces.size() == 1U);
	REQUIRE(selection.faces.front() == fixture.face);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}
