#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("delete selection prunes affected faces and unused vertices", "[operation][delete_selection]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const ElementId loose_vertex = add_vertex(fixture.document, { 4.0F, 0.0F, 0.0F });
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left });

	const OperationResult result = delete_selection(fixture.document, selection);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.empty());
	REQUIRE(find_vertex(fixture.document, loose_vertex) == nullptr);
	REQUIRE(fixture.document.vertices.empty());
}
