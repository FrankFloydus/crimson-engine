#include "editor_operation_test_helpers.h"

TEST_CASE("create box operation adds one selected object with cube topology", "[operation][create_box]")
{
	SECTION("valid bounds create a selected renderable cube")
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult result =
			session.create_box_from_bounds({ -2.0F, -1.0F, -3.0F }, { 2.0F, 1.0F, 3.0F });

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.objects().size() == 1U);
		REQUIRE(session.objects().front().selected);
		REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		REQUIRE(session.active_object_id() == session.objects().front().id);
		REQUIRE(qdr_test::every_edge_is_closed(session.objects().front().document));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
	}

	SECTION("reversed bounds are normalized and previous selection is cleared")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int first_id = session.objects().front().id;

		const quader_editor_core::CommandResult result =
			session.create_box_from_bounds({ 4.0F, 5.0F, 6.0F }, { 2.0F, 1.0F, 3.0F });

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.objects().size() == 2U);
		REQUIRE_FALSE(session.objects().front().selected);
		REQUIRE(session.objects().back().selected);
		REQUIRE(session.objects().back().id != first_id);
		REQUIRE(session.active_object_id() == session.objects().back().id);
		const quader_editor_core::Bounds3 bounds = session.selection_bounds();
		REQUIRE_FALSE(bounds.empty);
		REQUIRE(qdr_test::vec_nearly_equal(bounds.min, { 2.0F, 1.0F, 3.0F }));
		REQUIRE(qdr_test::vec_nearly_equal(bounds.max, { 4.0F, 5.0F, 6.0F }));
	}

	SECTION("degenerate bounds fail without consuming an object id")
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult failed =
			session.create_box_from_bounds({ 1.0F, 1.0F, 1.0F }, { 1.0F, 2.0F, 3.0F });
		const quader_editor_core::CommandResult created =
			session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });

		REQUIRE_FALSE(failed.ok);
		REQUIRE_FALSE(failed.changed);
		REQUIRE(created.ok);
		REQUIRE(created.changed);
		REQUIRE(session.objects().size() == 1U);
		REQUIRE(session.objects().front().id == 1);
	}
}
