#include "editor_operation_test_helpers.h"

#include <catch2/catch_approx.hpp>

TEST_CASE("bevel vertex command settings use focused grid distance", "[operation][bevel_vertices]")
{
	quader_editor_core::BevelVerticesOperationSettings settings;
	settings.focused_grid = { 0.25, true };

	CHECK(quader_editor_core::bevel_vertices_distance_from_settings(settings) == Catch::Approx(0.25F));

	settings.focused_grid = { 0.0, false };
	CHECK(quader_editor_core::bevel_vertices_distance_from_settings(settings) == Catch::Approx(
		static_cast<float>(quader_editor_core::default_command_grid_world_size)));

	settings.focused_grid = { 1.0, true };
	settings.explicit_distance = { 0.125, true };
	CHECK(quader_editor_core::bevel_vertices_distance_from_settings(settings) == Catch::Approx(0.125F));
}

TEST_CASE("bevel vertex command settings apply through editor session", "[operation][bevel_vertices]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
	session.set_component_selection(
		object_id,
		qdr_test::vertex_selection({ vertex_id }, vertex_id),
		quader_editor_core::SelectionMode::vertex);

	quader_editor_core::BevelVerticesOperationSettings settings;
	settings.focused_grid = { 0.25, true };
	const quader_editor_core::CommandResult bevel = session.bevel_selected_vertices(settings);

	REQUIRE(bevel.ok);
	REQUIRE(bevel.changed);
	REQUIRE(session.objects().size() == 1U);
	CHECK(session.objects().front().selection.mode == quader_poly::SelectionMode::vertex);
	CHECK(session.objects().front().selection.vertices.size() >= 2U);
}
