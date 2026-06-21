#include "editor_operation_test_helpers.h"

#include <quader_editor_core/editor_runtime.h>

#include <vector>

TEST_CASE("copy operation returns selected object copies only in object mode", "[operation][copy]")
{
	SECTION("selected object copies preserve source content")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();

		REQUIRE(copied.size() == 1U);
		REQUIRE(copied.front().id == session.objects().front().id);
		REQUIRE(copied.front().document.vertices.size() == session.objects().front().document.vertices.size());
	}

	SECTION("component mode does not produce object clipboard copies")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
		session.set_component_selection(
			session.objects().front().id,
			qdr_test::vertex_selection({ vertex_id }, vertex_id),
			quader_editor_core::SelectionMode::vertex);

		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();

		REQUIRE(copied.empty());
	}
}

TEST_CASE("editor runtime clipboard copy captures selected object copies", "[operation][copy]")
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	quader_editor_core::EditorRuntime runtime;

	CHECK(runtime.copy_selected_objects_to_clipboard(session) == 0);
	CHECK_FALSE(runtime.clipboard_available());

	session.set_selected_object_ids({ 1 }, 1);
	REQUIRE(runtime.copy_selected_objects_to_clipboard(session) == 1);
	CHECK(runtime.clipboard_available());
	REQUIRE_FALSE(runtime.clipboard_objects().empty());
	CHECK(runtime.clipboard_objects().front().id == 1);
	CHECK_FALSE(runtime.clipboard_objects().front().selected);

	runtime.clear_clipboard();
	CHECK_FALSE(runtime.clipboard_available());
	CHECK(runtime.status().clipboard_object_count == 0);
}
