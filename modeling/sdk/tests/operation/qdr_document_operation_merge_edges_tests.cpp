#include "operation_test_helpers.h"

#include <array>

using namespace quader_poly;

namespace {

Document make_parallel_quad_edge_document(Edge& active_edge, Edge& source_edge)
{
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId source_right = add_vertex(document, { 1.0F, 3.0F, 0.0F });
	const ElementId source_left = add_vertex(document, { -1.0F, 3.0F, 0.0F });
	const ElementId source_top_left = add_vertex(document, { -1.0F, 4.0F, 0.0F });
	const ElementId source_top_right = add_vertex(document, { 1.0F, 4.0F, 0.0F });

	const std::array first_face { bottom_left, bottom_right, top_right, top_left };
	const std::array second_face { source_right, source_left, source_top_left, source_top_right };
	REQUIRE(add_face(document, first_face) != invalid_element_id);
	REQUIRE(add_face(document, second_face) != invalid_element_id);

	active_edge = make_edge(bottom_left, bottom_right);
	source_edge = make_edge(source_right, source_left);
	return document;
}

} // namespace

TEST_CASE("merge edges welds source endpoints into the closest survivor edge vertices", "[operation][merge_edges]")
{
	Edge active_edge;
	Edge source_edge;
	Document document = make_parallel_quad_edge_document(active_edge, source_edge);
	Selection selection = qdr_test::edge_selection({ active_edge, source_edge }, active_edge);

	const OperationResult result = merge_selected_edges(document, selection);

	REQUIRE(result.changed);
	REQUIRE(find_vertex(document, active_edge.a) != nullptr);
	REQUIRE(find_vertex(document, active_edge.b) != nullptr);
	REQUIRE(find_vertex(document, source_edge.a) == nullptr);
	REQUIRE(find_vertex(document, source_edge.b) == nullptr);
	REQUIRE(document.faces.size() == 2U);
	REQUIRE(qdr_test::document_has_edge(document, active_edge));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(selection.mode == SelectionMode::edge);
	REQUIRE(selection.edges.size() == 1U);
	REQUIRE(selection.edges.front() == active_edge);
	REQUIRE(selection.has_active);
	REQUIRE(selection.active_edge == active_edge);
}

TEST_CASE("merge edges rejects incomplete edge selections", "[operation][merge_edges]")
{
	Edge active_edge;
	Edge source_edge;
	Document document = make_parallel_quad_edge_document(active_edge, source_edge);
	Selection selection = qdr_test::edge_selection({ active_edge }, active_edge);

	const OperationResult result = merge_selected_edges(document, selection);

	REQUIRE_FALSE(result.changed);
	REQUIRE_FALSE(result.message.empty());
}
