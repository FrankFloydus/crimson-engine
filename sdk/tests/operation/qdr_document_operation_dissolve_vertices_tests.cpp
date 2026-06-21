#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("dissolve vertices removes redundant inline vertices and selects neighbors", "[operation][dissolve_vertices]")
{
	Document document;
	const ElementId left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId middle = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	REQUIRE(add_face(document, std::array<ElementId, 4> { left, middle, right, top }, 5) != invalid_element_id);
	Selection selection = qdr_test::vertex_selection({ middle });

	const OperationResult result = dissolve_selected_vertices(document, selection);

	REQUIRE(result.changed);
	REQUIRE(find_vertex(document, middle) == nullptr);
	REQUIRE(document.faces.size() == 1U);
	REQUIRE(document.faces.front().vertices.size() == 3U);
	REQUIRE(selection_contains(selection, left));
	REQUIRE(selection_contains(selection, right));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST_CASE("dissolve vertices rejects true face corners without mutation", "[operation][dissolve_vertices]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertex_count_before = fixture.document.vertices.size();
	const std::size_t face_count_before = fixture.document.faces.size();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left });

	const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

	REQUIRE_FALSE(result.changed);
	REQUIRE(fixture.document.vertices.size() == vertex_count_before);
	REQUIRE(fixture.document.faces.size() == face_count_before);
}
