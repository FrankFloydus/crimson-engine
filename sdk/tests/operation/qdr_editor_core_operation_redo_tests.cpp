#include "editor_operation_test_helpers.h"

TEST_CASE("redo operation reapplies the undone editor snapshot", "[operation][redo]")
{
	SECTION("redo reapplies an undone content command")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::Vec3 position_before = session.objects().front().document.vertices.front().position;
		REQUIRE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
		}).changed);
		REQUIRE(session.undo().applied);

		const quader_editor_core::HistoryRestoreResult redo = session.redo();

		REQUIRE(redo.applied);
		REQUIRE(session.history_status().can_undo);
		REQUIRE(session.objects().front().document.vertices.front().position.x == Catch::Approx(position_before.x + 2.0F));
	}

	SECTION("redo on an empty stack reports no restore")
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::HistoryRestoreResult redo = session.redo();

		REQUIRE_FALSE(redo.applied);
	}
}
