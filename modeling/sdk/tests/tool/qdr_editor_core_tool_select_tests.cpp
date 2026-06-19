#include <catch2/catch_test_macros.hpp>

#include <quader_editor_core/session.h>
#include <quader_editor_core/tools.h>

#include <algorithm>

namespace {

quader_editor_core::RectangleSelectionUniverse simple_universe()
{
	quader_editor_core::RectangleSelectionUniverse universe;
	universe.object_ids = { 1, 2 };
	universe.faces = {
		{ 1, 0, { 0, 1, 2, 3 } },
		{ 1, 1, { 4, 5, 6, 7 } },
		{ 2, 0, { 0, 1, 2, 3 } },
	};
	universe.edges = {
		{ 1, 0, 1 },
		{ 1, 1, 2 },
		{ 2, 0, 1 },
	};
	universe.vertices = {
		{ 1, 0 },
		{ 1, 1 },
		{ 2, 0 },
	};
	return universe;
}

quader_editor_core::PickHitRecord surface_hit(int object_id, int face_id)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
	hit.kind = quader_poly::ElementKind::face;
	hit.face_id = face_id;
	return hit;
}

} // namespace

TEST_CASE("selection edit operations preserve expected host ids", "[editor-core][tool][select]")
{
	CHECK(quader_editor_core::selection_edit_operation_id(quader_editor_core::SelectionEditOperation::replace) == 0);
	CHECK(quader_editor_core::selection_edit_operation_id(quader_editor_core::SelectionEditOperation::add) == 1);
	CHECK(quader_editor_core::selection_edit_operation_id(quader_editor_core::SelectionEditOperation::remove) == 2);
	CHECK(quader_editor_core::selection_edit_operation_from_id(0) == quader_editor_core::SelectionEditOperation::replace);
	CHECK(quader_editor_core::selection_edit_operation_from_id(1) == quader_editor_core::SelectionEditOperation::add);
	CHECK(quader_editor_core::selection_edit_operation_from_id(2) == quader_editor_core::SelectionEditOperation::remove);
	CHECK(quader_editor_core::selection_edit_operation_from_id(99) == quader_editor_core::SelectionEditOperation::replace);
	CHECK(quader_editor_core::selection_edit_operation_from_modifiers(false, false) == quader_editor_core::SelectionEditOperation::replace);
	CHECK(quader_editor_core::selection_edit_operation_from_modifiers(true, false) == quader_editor_core::SelectionEditOperation::remove);
	CHECK(quader_editor_core::selection_edit_operation_from_modifiers(false, true) == quader_editor_core::SelectionEditOperation::add);
	CHECK(quader_editor_core::selection_edit_operation_preserves_existing(quader_editor_core::SelectionEditOperation::add));
	CHECK(quader_editor_core::selection_edit_operation_preserves_existing(quader_editor_core::SelectionEditOperation::remove));
	CHECK_FALSE(quader_editor_core::selection_edit_operation_preserves_existing(quader_editor_core::SelectionEditOperation::replace));
}

TEST_CASE("rectangle selection promotes full edge objects and keeps active target", "[editor-core][tool][select]")
{
	quader_editor_core::RectangleSelectionRequest request;
	request.current_selection.mode = quader_editor_core::SelectionMode::edge;
	request.operation = quader_editor_core::SelectionEditOperation::replace;
	request.universe = simple_universe();
	request.hits.edges = {
		{ 1, 1, 0 },
		{ 1, 1, 2 },
	};

	const quader_editor_core::RectangleSelectionResult result =
		quader_editor_core::apply_rectangle_selection(request);
	REQUIRE(result.selection.mode == quader_editor_core::SelectionMode::edge);
	REQUIRE(result.selection.selected_edge_full_object_ids.size() == 1);
	CHECK(result.selection.selected_edge_full_object_ids[0] == 1);
	CHECK(result.selection.selected_edges.empty());
	CHECK(result.selection.active_object_id == 1);
}

TEST_CASE("rectangle selection remove edits existing face selections", "[editor-core][tool][select]")
{
	quader_editor_core::RectangleSelectionRequest request;
	request.current_selection.mode = quader_editor_core::SelectionMode::face;
	request.current_selection.selected_faces = {
		{ 1, 0, { 0, 1, 2, 3 } },
		{ 1, 1, { 4, 5, 6, 7 } },
		{ 2, 0, { 0, 1, 2, 3 } },
	};
	request.operation = quader_editor_core::SelectionEditOperation::remove;
	request.universe = simple_universe();
	request.hits.faces = {
		{ 1, 1, { 4, 5, 6, 7 } },
	};

	const quader_editor_core::RectangleSelectionResult result =
		quader_editor_core::apply_rectangle_selection(request);
	REQUIRE(result.selection.mode == quader_editor_core::SelectionMode::face);
	REQUIRE(result.selection.selected_faces.size() == 2);
	CHECK(result.selection.selected_faces[0].object_id == 1);
	CHECK(result.selection.selected_faces[0].face_index == 0);
	CHECK(result.selection.selected_faces[1].object_id == 2);
	CHECK(result.selection.selected_faces[1].face_index == 0);
	CHECK(result.selection.active_object_id == 2);
	CHECK(result.selection.active_face_index == 0);
}

TEST_CASE("handle selection policy toggles grouped edge and face handles", "[editor-core][tool][select]")
{
	quader_editor_core::SelectionHandleRequest request;
	request.current_selection.mode = quader_editor_core::SelectionMode::edge;
	request.handle.kind = quader_editor_core::ActiveSelectionKind::edge;
	request.handle.object_id = 80;
	request.handle.vertex_indices = { 2, 5 };
	request.handle.element_groups = { { 80, { 2, 5 } }, { 81, { 1, 3 } } };

	quader_editor_core::RectangleSelectionResult result = quader_editor_core::select_handle(request);
	REQUIRE(result.selection.mode == quader_editor_core::SelectionMode::edge);
	REQUIRE(result.selection.selected_edges.size() == 2);
	CHECK(result.selection.selected_object_ids.size() == 2);
	CHECK(result.selection.active_object_id == 80);
	CHECK(result.selection.active_edge_vertex_indices == std::vector<int> { 2, 5 });

	request.current_selection = result.selection;
	request.additive = true;
	result = quader_editor_core::select_handle(request);
	CHECK(result.selection.selected_edges.empty());
	CHECK(result.selection.active_object_id == 81);

	request = {};
	request.current_selection.mode = quader_editor_core::SelectionMode::face;
	request.handle.kind = quader_editor_core::ActiveSelectionKind::face;
	request.handle.object_id = 90;
	request.handle.face_index = 4;
	request.handle.vertex_indices = { 0, 1, 2, 3 };

	result = quader_editor_core::select_handle(request);
	REQUIRE(result.selection.selected_faces.size() == 1);
	CHECK(result.selection.active_object_id == 90);
	CHECK(result.selection.active_face_index == 4);

	request.current_selection = result.selection;
	request.additive = true;
	result = quader_editor_core::select_handle(request);
	CHECK(result.selection.selected_faces.empty());
	CHECK(result.selection.active_object_id < 0);
}

TEST_CASE("picking policy cycles surfaces and shields selected handles", "[editor-core][tool][select]")
{
	std::vector<quader_editor_core::PickHitRecord> surface_hits {
		surface_hit(101, 5),
		surface_hit(102, 6),
	};

	quader_editor_core::PickSelectionState object_selection;
	object_selection.mode = quader_editor_core::SelectionMode::object;
	object_selection.selected_object_ids = { 101 };

	quader_editor_core::ClickPickRequest click;
	click.mode = quader_editor_core::SelectionMode::object;
	click.operation = quader_editor_core::SelectionEditOperation::replace;
	click.screen_position = { 24.0F, 32.0F };

	quader_editor_core::PickCycleState cycle;
	quader_editor_core::ClickPickPlan plan =
		quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click);
	REQUIRE(plan.has_target);
	CHECK(plan.hit_set == quader_editor_core::PickHitSet::surface);
	CHECK(plan.hit_index == 0);

	plan = quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click);
	REQUIRE(plan.has_target);
	CHECK(plan.hit_index == 1);
	CHECK(plan.cycle.cycled);

	click.screen_position = { 64.0F, 96.0F };
	plan = quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click);
	REQUIRE(plan.has_target);
	CHECK(plan.hit_index == 0);
	CHECK_FALSE(plan.cycle.cycled);

	quader_editor_core::PickHitRecord selected_edge_handle = surface_hits[0];
	selected_edge_handle.kind = quader_poly::ElementKind::edge;
	selected_edge_handle.edge = { 3, 9 };
	selected_edge_handle.face_id = quader_poly::invalid_element_id;
	quader_editor_core::PickHitRecord unselected_edge_handle = selected_edge_handle;
	unselected_edge_handle.object_id = 102;
	std::vector<quader_editor_core::PickHitRecord> handle_hits {
		selected_edge_handle,
		unselected_edge_handle,
	};

	quader_editor_core::PickSelectionState edge_selection;
	edge_selection.mode = quader_editor_core::SelectionMode::edge;
	edge_selection.selected_edges.push_back({ 101, quader_poly::ElementKind::edge, quader_poly::invalid_element_id, { 9, 3 } });

	click.mode = quader_editor_core::SelectionMode::edge;
	click.operation = quader_editor_core::SelectionEditOperation::add;
	click.handles_enabled = true;
	plan = quader_editor_core::plan_click_pick(cycle, edge_selection, handle_hits, {}, click);
	CHECK_FALSE(plan.has_target);
	CHECK(plan.clear_with_handle_state_on_miss);

	click.operation = quader_editor_core::SelectionEditOperation::remove;
	plan = quader_editor_core::plan_click_pick(cycle, edge_selection, handle_hits, surface_hits, click);
	REQUIRE(plan.has_target);
	CHECK(plan.hit_set == quader_editor_core::PickHitSet::handle);
	CHECK(plan.preserve_selection);
}

TEST_CASE("element handle picking and ownership policy are stable", "[editor-core][tool][select]")
{
	quader_editor_core::SelectionHandleTargetObjectRequest targets;
	targets.current_selection.mode = quader_editor_core::SelectionMode::edge;
	targets.current_selection.selected_edges = { { 101, 1, 3 }, { 102, 2, 4 }, { 101, 5, 6 } };
	targets.extra_target_object_ids = { 103, 101 };

	const std::vector<int> target_ids = quader_editor_core::handle_target_object_ids_for_selection(targets);
	CHECK(target_ids == std::vector<int> { 101, 102, 103 });

	std::vector<quader_editor_core::ElementHandlePickCandidate> candidates;
	candidates.push_back({ 0, quader_editor_core::ElementHandlePickKind::edge, 110, 4, 9, { 20.0F, 10.0F }, 4.0 });
	candidates.push_back({ 1, quader_editor_core::ElementHandlePickKind::vertex, 111, 2, -1, { 18.0F, 10.0F }, 8.0 });
	candidates.push_back({ 2, quader_editor_core::ElementHandlePickKind::face, 109, 1, -1, { 18.0F, 10.0F }, 2.0 });
	candidates.push_back({ 3, quader_editor_core::ElementHandlePickKind::vertex, 108, 1, -1, { 60.0F, 10.0F }, 1.0 });

	const std::vector<quader_editor_core::ElementHandlePickHit> hits =
		quader_editor_core::pick_element_handle_candidates(candidates, { 18.0F, 10.0F }, 8.0);
	REQUIRE(hits.size() == 3);
	CHECK(hits[0].candidate_index == 2);
	CHECK(hits[1].candidate_index == 1);
	CHECK(hits[2].candidate_index == 0);

	CHECK(quader_editor_core::selection_mode_uses_element_handles(quader_editor_core::SelectionMode::face));
	CHECK(quader_editor_core::selection_mode_uses_element_handles(quader_editor_core::SelectionMode::edge));
	CHECK(quader_editor_core::selection_mode_uses_element_handles(quader_editor_core::SelectionMode::vertex));
	CHECK_FALSE(quader_editor_core::selection_mode_uses_element_handles(quader_editor_core::SelectionMode::object));
	CHECK(quader_editor_core::default_viewport_tool_for_selection_mode(quader_editor_core::SelectionMode::face) ==
		quader_editor_core::ViewportToolIntent::extend);
}
