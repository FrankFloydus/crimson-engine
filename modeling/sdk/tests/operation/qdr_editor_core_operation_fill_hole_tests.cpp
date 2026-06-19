#include "editor_operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("fill hole switches editor selection to the new face and rejects repeated fills", "[operation][fill_hole]")
{
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());

	Selection edge_selection;
	edge_selection.mode = SelectionMode::edge;
	for (std::size_t index = 0; index < removed_face.vertices.size(); ++index) {
		edge_selection.edges.push_back(make_edge(
				removed_face.vertices[index],
				removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}

	quader_editor_core::EditorSession session({ qdr_test::make_object(1, document) });
	session.set_component_selection(1, edge_selection, quader_editor_core::SelectionMode::edge);
	const std::size_t face_count_before = session.objects().front().document.faces.size();

	const quader_editor_core::CommandResult first_result = session.fill_hole_from_selected_edges();

	REQUIRE(first_result.ok);
	REQUIRE(first_result.changed);
	REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::face);
	REQUIRE(session.objects().front().document.faces.size() == face_count_before + 1U);
	REQUIRE(session.objects().front().selection.mode == SelectionMode::face);
	REQUIRE(session.objects().front().selection.faces.size() == 1U);

	const std::size_t face_count_after_fill = session.objects().front().document.faces.size();
	const quader_editor_core::CommandResult repeat_result = session.fill_hole_from_selected_edges();

	REQUIRE_FALSE(repeat_result.ok);
	REQUIRE_FALSE(repeat_result.changed);
	REQUIRE(session.objects().front().document.faces.size() == face_count_after_fill);
}
