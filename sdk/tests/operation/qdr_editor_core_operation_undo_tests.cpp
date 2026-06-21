#include "editor_operation_test_helpers.h"

TEST_CASE("undo operation restores the previous editor snapshot", "[operation][undo]")
{
	SECTION("undo restores a successful content command")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::Vec3 position_before = session.objects().front().document.vertices.front().position;
		REQUIRE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
		}).changed);

		const quader_editor_core::HistoryRestoreResult undo = session.undo();

		REQUIRE(undo.applied);
		REQUIRE(session.history_status().can_redo);
		REQUIRE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices.front().position, position_before));
	}

	SECTION("undo on an empty stack reports no restore")
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::HistoryRestoreResult undo = session.undo();

		REQUIRE_FALSE(undo.applied);
	}
}
