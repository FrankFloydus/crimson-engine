#include "editor_operation_test_helpers.h"

TEST_CASE("invert selection operation swaps selected and unselected object sets", "[operation][invert_selection]")
{
	SECTION("object mode swaps selected and unselected object sets")
	{
		quader_editor_core::EditorSession session;
		REQUIRE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		REQUIRE(session.create_box_from_bounds({ 3.0F, -1.0F, -1.0F }, { 5.0F, 1.0F, 1.0F }).changed);
		const int first_id = session.objects()[0].id;
		const int second_id = session.objects()[1].id;
		session.set_selected_object_ids({ first_id }, first_id);

		const quader_editor_core::CommandResult result = session.invert_selection();

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.selected_object_ids().size() == 1U);
		REQUIRE(session.selected_object_ids().front() == second_id);
	}

	SECTION("vertex mode selects the complement on the active object")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId selected_vertex = session.objects().front().document.vertices.front().id;
		session.set_component_selection(
			session.objects().front().id,
			qdr_test::vertex_selection({ selected_vertex }, selected_vertex),
			quader_editor_core::SelectionMode::vertex);

		const quader_editor_core::CommandResult result = session.invert_selection();

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.objects().front().selection.vertices.size() == session.objects().front().document.vertices.size() - 1U);
		REQUIRE_FALSE(quader_poly::selection_contains(session.objects().front().selection, selected_vertex));
		REQUIRE(session.objects().front().selection.has_active);
	}

	SECTION("inverting an empty session is a no-op")
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult result = session.invert_selection();

		REQUIRE(result.ok);
		REQUIRE_FALSE(result.changed);
	}
}
