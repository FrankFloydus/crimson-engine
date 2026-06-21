#include "editor_operation_test_helpers.h"

TEST_CASE("delete operation removes selected editor targets", "[operation][delete]")
{
	SECTION("object mode removes selected objects and normalizes active object")
	{
		quader_editor_core::EditorSession session;
		REQUIRE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		REQUIRE(session.create_box_from_bounds({ 3.0F, -1.0F, -1.0F }, { 5.0F, 1.0F, 1.0F }).changed);
		const int remaining_id = session.objects().front().id;
		session.set_selected_object_ids({ session.objects().back().id }, session.objects().back().id);

		const quader_editor_core::CommandResult result = session.delete_selected();

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.objects().size() == 1U);
		REQUIRE(session.objects().front().id == remaining_id);
		REQUIRE_FALSE(session.objects().front().selected);
		REQUIRE(session.active_object_id() == -1);
	}

	SECTION("face mode clears component selection after deleting selected faces")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId face_id = session.objects().front().document.faces.front().id;
		const std::size_t face_count_before = session.objects().front().document.faces.size();
		session.set_component_selection(
			session.objects().front().id,
			qdr_test::face_selection({ face_id }, face_id),
			quader_editor_core::SelectionMode::face);

		const quader_editor_core::CommandResult result = session.delete_selected();

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before - 1U);
		REQUIRE_FALSE(session.objects().front().selected);
		REQUIRE(session.objects().front().selection.empty());
		REQUIRE(session.active_object_id() == -1);
	}

	SECTION("missing selection reports an error without changing content")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		session.clear_selection();
		const std::size_t object_count_before = session.objects().size();

		const quader_editor_core::CommandResult result = session.delete_selected();

		REQUIRE_FALSE(result.ok);
		REQUIRE_FALSE(result.changed);
		REQUIRE(session.objects().size() == object_count_before);
	}
}
