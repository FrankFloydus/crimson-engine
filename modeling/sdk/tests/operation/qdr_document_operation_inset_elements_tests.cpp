#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("inset selected edge elements transforms a copied edge into a new face strip", "[operation][inset_elements]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge source = make_edge(fixture.bottom_left, fixture.bottom_right);
	Selection selection = qdr_test::edge_selection({ source }, source);
	Transform3 transform;
	transform.origin = { 0.0F, -0.5F, 0.0F };

	const OperationResult result = inset_selected_elements(fixture.document, selection, transform, {});

	REQUIRE(result.changed);
	REQUIRE(selection.mode == SelectionMode::edge);
	REQUIRE(selection.edges.size() == 1U);
	REQUIRE(fixture.document.faces.size() == 2U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}
