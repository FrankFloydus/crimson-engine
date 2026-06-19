#include "editor_operation_test_helpers.h"

using namespace quader_editor_core;

namespace {

void translate_document(quader_poly::Document& document, quader_poly::Vec3 delta)
{
	for (quader_poly::Vertex& vertex : document.vertices) {
		vertex.position += delta;
	}
}

} // namespace

TEST_CASE("combine mesh merges selected mesh objects into the active survivor", "[operation][combine_mesh]")
{
	quader_poly::Document first = qdr_test::make_quad_document(2).document;
	quader_poly::Document second = qdr_test::make_quad_document(7).document;
	translate_document(second, { 4.0F, 0.0F, 0.0F });
	EditorSession session({ qdr_test::make_object(1, first), qdr_test::make_object(2, second) });
	session.set_selected_object_ids({ 1, 2 }, 1);

	const CommandResult result = session.combine_selected_meshes();

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.objects().front().id == 1);
	REQUIRE(session.objects().front().selected);
	REQUIRE(session.active_object_id() == 1);
	REQUIRE(session.selection_mode() == SelectionMode::object);
	REQUIRE(session.objects().front().document.vertices.size() == first.vertices.size() + second.vertices.size());
	REQUIRE(session.objects().front().document.faces.size() == first.faces.size() + second.faces.size());
	REQUIRE(std::ranges::any_of(session.objects().front().document.faces, [](const quader_poly::Face& face) {
		return face.material_slot == 7U;
	}));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
}

TEST_CASE("combine mesh records one undoable history entry when committed through history", "[operation][combine_mesh]")
{
	EditorSession session({
		qdr_test::make_object(1, qdr_test::make_quad_document().document),
		qdr_test::make_object(2, qdr_test::make_quad_document().document),
	});
	session.set_selected_object_ids({ 1, 2 }, 2);

	const CommandResult result = session.apply_history_command([](EditorSession& editor_session) {
		return editor_session.combine_selected_meshes();
	});

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.objects().front().id == 2);
	REQUIRE(session.history_status().undo_count == 1U);
	REQUIRE(session.undo().applied);
	REQUIRE(session.objects().size() == 2U);
	REQUIRE(session.active_object_id() == 2);
}

TEST_CASE("combine mesh rejects non-object mode and undersized selections", "[operation][combine_mesh]")
{
	EditorSession session({ qdr_test::make_object(1, qdr_test::make_quad_document().document) });
	session.set_selected_object_ids({ 1 }, 1);

	REQUIRE_FALSE(session.combine_selected_meshes().ok);

	quader_poly::Selection vertex_selection = qdr_test::vertex_selection({ session.objects().front().document.vertices.front().id });
	session.set_component_selection(1, vertex_selection, SelectionMode::vertex);
	const CommandResult result = session.combine_selected_meshes();

	REQUIRE_FALSE(result.ok);
}
