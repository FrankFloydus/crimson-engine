#include "editor_operation_test_helpers.h"

#include <array>

using namespace quader_poly;

namespace {

quader_editor_core::EditorSession make_merge_edges_session(Edge& active_edge, Edge& source_edge)
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

	quader_editor_core::EditorSession session;
	session.set_objects({ qdr_test::make_object(1, std::move(document), true) });
	session.set_component_selection(
		1,
		qdr_test::edge_selection({ active_edge, source_edge }, active_edge),
		quader_editor_core::SelectionMode::edge);
	return session;
}

} // namespace

TEST_CASE("merge edges editor operation welds selected edges through session history", "[operation][merge_edges]")
{
	Edge active_edge;
	Edge source_edge;
	quader_editor_core::EditorSession session = make_merge_edges_session(active_edge, source_edge);

	const quader_editor_core::CommandResult result = session.merge_selected_edges();

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	const quader_editor_core::PolygonObject& object = session.objects().front();
	REQUIRE(find_vertex(object.document, active_edge.a) != nullptr);
	REQUIRE(find_vertex(object.document, active_edge.b) != nullptr);
	REQUIRE(find_vertex(object.document, source_edge.a) == nullptr);
	REQUIRE(find_vertex(object.document, source_edge.b) == nullptr);
	REQUIRE(object.selection.mode == SelectionMode::edge);
	REQUIRE(object.selection.edges.size() == 1U);
	REQUIRE(object.selection.edges.front() == active_edge);
	REQUIRE(object.selection.has_active);
	REQUIRE(object.selection.active_edge == active_edge);
}
