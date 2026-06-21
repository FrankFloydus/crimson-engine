#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("bridge faces connects two open face islands", "[operation][bridge_faces]")
{
	Document document = qdr_test::make_parallel_quad_document();
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection);

	REQUIRE(result.changed);
	REQUIRE(document.faces.size() == 6U);
	REQUIRE(selection.mode == SelectionMode::face);
	REQUIRE(selection.faces.size() == 4U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST_CASE("interpolated bridge faces creates one ring per step", "[operation][bridge_faces][interpolated]")
{
	Document document = qdr_test::make_parallel_quad_document();
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection, 3);

	REQUIRE(result.changed);
	REQUIRE(document.faces.size() == 14U);
	REQUIRE(selection.faces.size() == 12U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}
