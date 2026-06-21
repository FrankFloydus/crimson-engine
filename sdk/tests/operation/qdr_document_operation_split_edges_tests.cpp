#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("split edges separates adjacent faces without deleting either side", "[operation][split_edges]")
{
	Document document = make_cube({});
	const Edge source_edge = document_edges(document).front();
	Selection selection = qdr_test::edge_selection({ source_edge }, source_edge);
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = split_selected_edges(document, selection);

	REQUIRE(result.changed);
	REQUIRE(document.vertices.size() == vertex_count_before + 2U);
	REQUIRE(document.faces.size() == face_count_before);
	REQUIRE(selection.mode == SelectionMode::edge);
	REQUIRE(selection.edges.size() == 2U);
	for (const Edge& edge : selection.edges) {
		REQUIRE(qdr_test::edge_incidence_count(document, edge) == 1);
	}
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}
