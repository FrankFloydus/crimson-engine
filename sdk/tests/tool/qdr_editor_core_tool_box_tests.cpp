#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <quader_editor_core/editor_runtime.h>
#include <quader_editor_core/tools.h>

#include <optional>
#include <vector>

TEST_CASE("box footprint snaps directional pairs and preserves signed size", "[editor-core][tool][box]")
{
	quader_editor_core::BoxToolState state;
	REQUIRE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);

	const quader_editor_core::BoxToolStateResult begin =
		quader_editor_core::begin_box_footprint(
			state,
			{ 0.2F, 0.0F, 0.2F },
			{ 1.2F, 0.0F, 2.8F },
			true,
			1.0);
	REQUIRE(begin.ok);
	CHECK(begin.state.drag_state == quader_editor_core::BoxDragState::footprint);

	const quader_editor_core::BoxPreviewDescriptor descriptor =
		quader_editor_core::box_preview_descriptor(begin.state, true, 1.0);
	REQUIRE(descriptor.ok);
	REQUIRE(descriptor.footprint.footprint.size() == 4);
	CHECK(descriptor.footprint.size.x == Catch::Approx(2.0F));
	CHECK(descriptor.footprint.size.y == Catch::Approx(3.0F));
	CHECK_FALSE(descriptor.bounds.empty);
	CHECK(descriptor.bounds.min.y == Catch::Approx(0.0F));
	CHECK(descriptor.bounds.max.y == Catch::Approx(0.0F));
}

TEST_CASE("box runtime reset helper restores default grid size", "[editor-core][tool][box]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.box.preview_valid = true;
	runtime.box_grid_size = 9.0;

	quader_editor_core::reset_editor_runtime_box_state(runtime);
	CHECK_FALSE(quader_editor_core::editor_runtime_status(runtime).box_active);
	CHECK(runtime.box_grid_size == Catch::Approx(quader_editor_core::default_editor_runtime_box_grid_size));
}

TEST_CASE("box height preview snaps to grid and flips normal for negative height", "[editor-core][tool][box]")
{
	quader_editor_core::BoxToolState state;
	REQUIRE(quader_editor_core::begin_box_footprint(
			state,
			{ 0.0F, 0.0F, 0.0F },
			{ 1.0F, 0.0F, 1.0F },
			true,
			1.0).ok);

	REQUIRE(quader_editor_core::begin_box_height(state, { 100.0F, 100.0F }, 0.0, true, true, 1.0).ok);
	const quader_editor_core::BoxToolStateResult updated =
		quader_editor_core::update_box_height_preview(state, -3.0, true, { 100.0F, 120.0F }, true, 1.0);
	REQUIRE(updated.ok);
	CHECK(updated.state.drag_state == quader_editor_core::BoxDragState::height);
	CHECK(updated.state.height_distance == Catch::Approx(-2.0));

	const quader_editor_core::BoxPreviewDescriptor descriptor =
		quader_editor_core::box_preview_descriptor(updated.state, true, 1.0);
	REQUIRE(descriptor.ok);
	CHECK(descriptor.distance == Catch::Approx(2.0));
	CHECK(descriptor.normal.y == Catch::Approx(-1.0F));
	CHECK(descriptor.bounds.min.y == Catch::Approx(-2.0F));
	CHECK(descriptor.bounds.max.y == Catch::Approx(0.0F));
}

TEST_CASE("box height screen fallback uses vertical motion when axis distance is unavailable", "[editor-core][tool][box]")
{
	quader_editor_core::BoxToolState state;
	REQUIRE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }).ok);
	REQUIRE(quader_editor_core::begin_box_footprint(
			state,
			{ 0.0F, -1.0F, -1.0F },
			{ 0.0F, 1.0F, 1.0F },
			false,
			1.0).ok);

	REQUIRE(quader_editor_core::begin_box_height(
			state,
			{ 400.0F, 300.0F },
			0.0,
			false,
			true,
			1.0).ok);

	const quader_editor_core::BoxToolStateResult downward =
		quader_editor_core::update_box_height_preview(
			state,
			0.0,
			false,
			{ 400.0F, 396.0F },
			true,
			1.0);
	REQUIRE(downward.ok);
	CHECK(downward.state.height_distance == Catch::Approx(-1.0));

	const quader_editor_core::BoxToolStateResult sideways =
		quader_editor_core::update_box_height_preview(
			state,
			0.0,
			false,
			{ 496.0F, 300.0F },
			true,
			1.0);
	REQUIRE(sideways.ok);
	CHECK(sideways.state.height_distance == Catch::Approx(1.0));

	REQUIRE(quader_editor_core::begin_box_height(
			state,
			{ 400.0F, 300.0F },
			1.0,
			true,
			true,
			1.0).ok);
	const quader_editor_core::BoxToolStateResult axis_distance =
		quader_editor_core::update_box_height_preview(
			state,
			4.0,
			true,
			{ 496.0F, 300.0F },
			true,
			1.0);
	REQUIRE(axis_distance.ok);
	CHECK(axis_distance.state.height_distance == Catch::Approx(4.0));
}

TEST_CASE("box preview state and reset clear transient brush identity", "[editor-core][tool][box]")
{
	quader_editor_core::BoxToolState state;
	state.drag_state = quader_editor_core::BoxDragState::height;
	quader_editor_core::set_box_preview_state(state, 42, true);
	CHECK(state.preview_brush_id == 42);
	CHECK(state.preview_valid);

	quader_editor_core::clear_box_preview_state(state);
	CHECK(state.preview_brush_id == -1);
	CHECK_FALSE(state.preview_valid);

	quader_editor_core::reset_box(state);
	CHECK(state.drag_state == quader_editor_core::BoxDragState::idle);
	CHECK(state.preview_brush_id == -1);
	CHECK_FALSE(state.preview_valid);
	CHECK(state.hover_cell_points.empty());
}

TEST_CASE("box construction ray hover cell and sync preserve preview contract", "[editor-core][tool][box]")
{
	quader_editor_core::BoxToolState state;
	REQUIRE(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);

	const quader_editor_core::BoxConstructionPointResult point =
		quader_editor_core::box_construction_point_from_ray(
			state.plane,
			{ { 8.0F, 64.0F, 24.0F }, { 0.0F, -1.0F, 0.0F } });
	REQUIRE(point.ok);
	CHECK(point.position.y == Catch::Approx(0.0F));
	CHECK(point.distance == Catch::Approx(64.0));

	const quader_editor_core::BoxToolStateResult hover =
		quader_editor_core::update_box_hover_cell(state, { 33.0F, 0.0F, 33.0F }, true, 16.0);
	REQUIRE(hover.ok);
	REQUIRE(state.hover_cell_points.size() == 4);
	CHECK(state.hover_cell_points[0].x == Catch::Approx(32.0F));
	CHECK(state.hover_cell_points[2].x == Catch::Approx(48.0F));

	REQUIRE(quader_editor_core::begin_box_footprint(
			state,
			{ 0.0F, 0.0F, 0.0F },
			{ 32.0F, 0.0F, 32.0F },
			true,
			16.0).ok);
	REQUIRE(quader_editor_core::begin_box_height(state, { 100.0F, 100.0F }, 0.0, false, true, 16.0).ok);
	REQUIRE(quader_editor_core::update_box_height_preview(state, 0.0, false, { 100.0F, 244.0F }, true, 16.0).ok);
	quader_editor_core::set_box_preview_state(state, 42, true);

	const quader_editor_core::BoxPreviewDescriptor descriptor =
		quader_editor_core::box_preview_descriptor(state, true, 16.0);
	REQUIRE(descriptor.ok);
	CHECK(descriptor.signed_height == Catch::Approx(-32.0));
	CHECK(descriptor.distance == Catch::Approx(32.0));
	CHECK(descriptor.normal.y == Catch::Approx(-1.0F));
	CHECK(descriptor.preview_brush_id == 42);
	CHECK(descriptor.preview_valid);

	quader_editor_core::BoxToolState copied;
	quader_editor_core::sync_box_state(copied, state);
	CHECK(copied.preview_brush_id == 42);
	CHECK(copied.plane.normal.y == Catch::Approx(1.0F));
}

TEST_CASE("box construction plane can be seeded from a mesh surface below the grid", "[editor-core][tool][box]")
{
	quader_editor_core::EditorSession session;
	REQUIRE(session.create_box_from_bounds({ -1.0F, -2.0F, -1.0F }, { 1.0F, -1.0F, 1.0F }).changed);

	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	const quader_poly::Ray ray { { 0.0F, 4.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	const std::optional<quader_editor_core::PickHitRecord> hit =
		session.pick_nearest({ ray, options });
	REQUIRE(hit.has_value());
	CHECK(hit->local_position.y == Catch::Approx(-1.0F));
	CHECK(hit->local_normal.y == Catch::Approx(1.0F));

	quader_editor_core::BoxToolState state;
	REQUIRE(quader_editor_core::set_box_construction_plane(state, hit->local_position, hit->local_normal).ok);
	const quader_editor_core::BoxConstructionPointResult point =
		quader_editor_core::box_construction_point_from_ray(state.plane, ray);
	REQUIRE(point.ok);
	CHECK(point.position.y == Catch::Approx(-1.0F));
}

TEST_CASE("box preview corners commit a wall-planar oriented volume", "[editor-core][tool][box]")
{
	quader_editor_core::BoxToolState state;
	REQUIRE(quader_editor_core::set_box_construction_plane(state, { 2.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }).ok);
	REQUIRE(quader_editor_core::begin_box_footprint(
			state,
			{ 2.0F, 0.0F, 0.0F },
			{ 2.0F, -2.0F, 3.0F },
			false,
			1.0).ok);
	state.drag_state = quader_editor_core::BoxDragState::height;
	state.height_distance = 2.0;

	const quader_editor_core::BoxPreviewDescriptor preview =
		quader_editor_core::box_preview_descriptor(state, false, 1.0);
	REQUIRE(preview.ok);
	const std::vector<quader_poly::Vec3> corners = quader_editor_core::box_preview_corners(preview);
	REQUIRE(corners.size() == 8);

	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult result = session.create_box_from_corners(corners);
	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 1);
	const quader_poly::Document& document = session.objects().front().document;
	REQUIRE(document.vertices.size() == 8);
	REQUIRE(document.faces.size() == 6);
	CHECK_FALSE(quader_poly::compile_document(document).indices.empty());

	int base_vertex_count = 0;
	int top_vertex_count = 0;
	for (const quader_poly::Vertex& vertex : document.vertices) {
		if (vertex.position.x == Catch::Approx(2.0F)) {
			++base_vertex_count;
		}
		if (vertex.position.x == Catch::Approx(4.0F)) {
			++top_vertex_count;
		}
	}
	CHECK(base_vertex_count == 4);
	CHECK(top_vertex_count == 4);

	bool found_base_face = false;
	bool found_top_face = false;
	for (const quader_poly::Face& face : document.faces) {
		bool all_base_x = true;
		bool all_top_x = true;
		for (quader_poly::ElementId vertex_id : face.vertices) {
			const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
			REQUIRE(vertex != nullptr);
			all_base_x = all_base_x && vertex->position.x == Catch::Approx(2.0F);
			all_top_x = all_top_x && vertex->position.x == Catch::Approx(4.0F);
		}
		const quader_poly::Vec3 normal = quader_poly::face_normal(document, face);
		if (all_base_x) {
			found_base_face = true;
			CHECK(normal.x == Catch::Approx(-1.0F));
		}
		if (all_top_x) {
			found_top_face = true;
			CHECK(normal.x == Catch::Approx(1.0F));
		}
	}
	CHECK(found_base_face);
	CHECK(found_top_face);
}
