////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <modeling/runtime/editor_runtime.h>
#include <modeling/runtime/session.h>
#include <modeling/interactions/interaction_api.h>
#include <modeling/polygon/document.h>

#include <vector>

TEST_CASE("insert edge loop factor snaps to grid", "[editor-core][tool][insert-edge-loop]")
{
	const quader_poly::Document cube = quader_poly::make_cube({});
	REQUIRE_FALSE(cube.faces.empty());
	REQUIRE(cube.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { cube.faces[0].vertices[0], cube.faces[0].vertices[1] };

	CHECK(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, 0.51F, 1.0, false) == Catch::Approx(0.51F));
	CHECK(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, 0.49F, 1.0, true) == Catch::Approx(0.5F));
	CHECK(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, -10.0F, 1.0, false) == Catch::Approx(0.01F));
	CHECK(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, 10.0F, 1.0, false) == Catch::Approx(0.99F));
}

TEST_CASE("insert edge loop preview rejects grid snaps onto existing edges", "[editor-core][tool][insert-edge-loop]")
{
	const quader_poly::Document cube = quader_poly::make_cube({});
	REQUIRE_FALSE(cube.faces.empty());
	REQUIRE(cube.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { cube.faces[0].vertices[0], cube.faces[0].vertices[1] };

	const std::optional<float> midpoint =
		quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.49F, 1.0, true);
	REQUIRE(midpoint.has_value());
	CHECK(*midpoint == Catch::Approx(0.5F));
	CHECK_FALSE(quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.04F, 1.0, true).has_value());
	CHECK_FALSE(quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.96F, 1.0, true).has_value());

	const std::optional<float> free_slide =
		quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.04F, 1.0, false);
	REQUIRE(free_slide.has_value());
	CHECK(*free_slide == Catch::Approx(0.04F));
}

TEST_CASE("insert edge loop preview stores seed and clears hover candidate", "[editor-core][tool][insert-edge-loop]")
{
	quader_editor_core::PolygonObject object;
	object.id = 3;
	object.document = quader_poly::make_cube({});
	REQUIRE_FALSE(object.document.faces.empty());
	REQUIRE(object.document.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { object.document.faces[0].vertices[0], object.document.faces[0].vertices[1] };
	std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::InsertEdgeLoopToolState state;
	quader_editor_core::SelectionHoverState hover;
	hover.object_id = 3;
	hover.candidate_object_id = 3;

	const bool changed =
		quader_editor_core::set_insert_edge_loop_preview(state, objects, hover, 3, edge, 0.5F, true);
	REQUIRE(changed);
	CHECK(state.active);
	CHECK(state.dragging);
	CHECK(state.object_id == 3);
	CHECK(state.edge.a == edge.a);
	CHECK(state.edge.b == edge.b);
	CHECK(state.factor == Catch::Approx(0.5F));
	CHECK_FALSE(state.preview_lines.empty());
	CHECK(quader_editor_core::insert_edge_loop_tool_has_seed(state));
	CHECK(hover.candidate_object_id == 3);
}

TEST_CASE("insert edge loop preview clears when snapped drag reaches an existing edge", "[editor-core][tool][insert-edge-loop]")
{
	quader_editor_core::PolygonObject object;
	object.id = 3;
	object.document = quader_poly::make_cube({});
	REQUIRE_FALSE(object.document.faces.empty());
	REQUIRE(object.document.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { object.document.faces[0].vertices[0], object.document.faces[0].vertices[1] };
	const quader_poly::Vertex *start = quader_poly::find_vertex(object.document, edge.a);
	const quader_poly::Vertex *end = quader_poly::find_vertex(object.document, edge.b);
	REQUIRE(start != nullptr);
	REQUIRE(end != nullptr);
	std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::InsertEdgeLoopToolState state;
	quader_editor_core::SelectionHoverState hover;
	REQUIRE(quader_editor_core::set_insert_edge_loop_preview(state, objects, hover, 3, edge, 0.5F, true));
	REQUIRE_FALSE(state.preview_lines.empty());

	const quader_poly::Vec3 near_start {
		start->position.x + ((end->position.x - start->position.x) * 0.04F),
		start->position.y + ((end->position.y - start->position.y) * 0.04F),
		start->position.z + ((end->position.z - start->position.z) * 0.04F),
	};
	const quader_poly::Ray ray {
		{ near_start.x, near_start.y + 1.0F, near_start.z },
		{ 0.0F, -1.0F, 0.0F },
	};

	CHECK_FALSE(quader_editor_core::update_insert_edge_loop_locked_seed(state, objects, hover, ray, 1.0, true, true));
	CHECK(state.active);
	CHECK(state.dragging);
	CHECK(state.object_id == 3);
	CHECK(state.edge == quader_poly::make_edge(edge.a, edge.b));
	CHECK(state.preview_lines.empty());
	CHECK(hover.object_id == 3);
	CHECK(hover.kind == quader_editor_core::SelectionHoverKind::edge);
}

TEST_CASE("insert edge loop reset can preserve active ownership through runtime helper", "[editor-core][tool][insert-edge-loop]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::insert_edge_loop;
	runtime.insert_edge_loop.active = true;
	runtime.insert_edge_loop.dragging = true;
	runtime.insert_edge_loop.object_id = 3;
	runtime.insert_edge_loop.preview_lines.push_back({});

	quader_editor_core::reset_editor_runtime_insert_edge_loop_state(runtime, true);
	CHECK(runtime.insert_edge_loop.active);
	CHECK_FALSE(runtime.insert_edge_loop.dragging);
	CHECK(runtime.insert_edge_loop.object_id == -1);
	CHECK(runtime.insert_edge_loop.preview_lines.empty());

	quader_editor_core::reset_editor_runtime_insert_edge_loop_state(runtime, false);
	CHECK_FALSE(runtime.insert_edge_loop.active);
}

TEST_CASE("insert edge loop runtime helpers publish drag state and promote hover candidates", "[editor-core][tool][insert-edge-loop]")
{
	quader_editor_core::EditorRuntime runtime;
	runtime.state().hover.object_id = 42;
	runtime.state().box.preview_valid = true;
	runtime.state().box_grid_size = 4.0;
	runtime.state().knife.active = true;

	quader_editor_core::EditorRuntimeToolActivationRequest request;
	request.requested_tool = quader_editor_core::ViewportInteractionTool::insert_edge_loop;
	request.selection_mode = quader_editor_core::SelectionMode::object;
	const quader_editor_core::EditorRuntimeToolActivationResult result = runtime.activate_tool(request);

	REQUIRE(result.applied);
	CHECK(result.plan.selection_mode == quader_editor_core::SelectionMode::edge);
	CHECK(result.plan.selection_mode_changed);
	CHECK(runtime.state().active_tool == quader_editor_core::ViewportInteractionTool::insert_edge_loop);
	CHECK(runtime.state().insert_edge_loop.active);
	CHECK_FALSE(runtime.state().knife.active);
	CHECK(runtime.state().hover.candidate_object_id == 42);
	CHECK_FALSE(quader_editor_core::editor_runtime_status(runtime.state()).box_active);
	CHECK(runtime.state().box_grid_size == quader_editor_core::default_editor_runtime_box_grid_size);

	quader_editor_core::InsertEdgeLoopToolState edge_loop;
	edge_loop.active = true;
	edge_loop.dragging = true;
	edge_loop.object_id = 13;
	edge_loop.edge = { 1, 2 };
	edge_loop.preview_lines.push_back({});
	runtime.set_insert_edge_loop_state(edge_loop);
	CHECK(runtime.status().insert_edge_loop_active);
	CHECK(runtime.status().insert_edge_loop_dragging);

	runtime.set_insert_edge_loop_dragging(false);
	CHECK(runtime.status().insert_edge_loop_active);
	CHECK_FALSE(runtime.status().insert_edge_loop_dragging);
}
