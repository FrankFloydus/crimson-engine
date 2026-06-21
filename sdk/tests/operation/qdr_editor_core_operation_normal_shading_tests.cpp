#include "editor_operation_test_helpers.h"

using namespace quader_editor_core;

TEST_CASE("shade smooth and flat operate on selected mesh objects", "[operation][normal_shading]")
{
	EditorSession session = qdr_test::selected_cube_session();

	const CommandResult smooth = session.shade_selected_faces_smooth();

	REQUIRE(smooth.ok);
	REQUIRE(smooth.changed);
	for (const quader_poly::Face& face : session.objects().front().document.faces) {
		CHECK(face.normal_shading == quader_poly::SurfaceShading::smooth);
	}

	const CommandResult flat = session.shade_selected_faces_flat();

	REQUIRE(flat.ok);
	REQUIRE(flat.changed);
	for (const quader_poly::Face& face : session.objects().front().document.faces) {
		CHECK(face.normal_shading == quader_poly::SurfaceShading::flat);
	}
}

TEST_CASE("hard and soft normal operations operate on selected edges", "[operation][normal_shading]")
{
	EditorSession session = qdr_test::selected_cube_session();
	quader_editor_core::PolygonObject& object = session.objects().front();
	const quader_poly::Face first_face = object.document.faces.front();
	const quader_poly::Edge edge = quader_poly::make_edge(first_face.vertices[0], first_face.vertices[1]);
	session.set_component_selection(
		object.id,
		qdr_test::edge_selection({ edge }, edge),
		SelectionMode::edge);

	const CommandResult soft_default = session.soften_selected_edge_normals();

	REQUIRE(soft_default.ok);
	REQUIRE(soft_default.changed);
	std::size_t smooth_face_count = 0;
	for (const quader_poly::Face& face : session.objects().front().document.faces) {
		if (face.normal_shading == quader_poly::SurfaceShading::smooth) {
			++smooth_face_count;
		}
	}
	REQUIRE(smooth_face_count > 0U);
	REQUIRE(session.objects().front().document.hard_normal_edges.empty());

	const CommandResult hard = session.harden_selected_edge_normals();

	REQUIRE(hard.ok);
	REQUIRE(hard.changed);
	REQUIRE(session.objects().front().document.hard_normal_edges.size() == 1U);

	const CommandResult soft = session.soften_selected_edge_normals();

	REQUIRE(soft.ok);
	REQUIRE(soft.changed);
	REQUIRE(session.objects().front().document.hard_normal_edges.empty());
}
