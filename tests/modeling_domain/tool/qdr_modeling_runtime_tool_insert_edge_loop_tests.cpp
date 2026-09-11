////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <vector>

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopFactorSnapsToGrid) {
	const quader_poly::Document cube = quader_poly::make_cube({});
	EXPECT_FALSE(cube.faces.empty());
	EXPECT_TRUE(cube.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { cube.faces[0].vertices[0], cube.faces[0].vertices[1] };

	EXPECT_NEAR(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, 0.51F, 1.0, false), 0.51F, 0.0001);
	EXPECT_NEAR(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, 0.49F, 1.0, true), 0.5F, 0.0001);
	EXPECT_NEAR(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, -10.0F, 1.0, false), 0.01F, 0.0001);
	EXPECT_NEAR(quader_editor_core::snap_insert_edge_loop_factor(cube, edge, 10.0F, 1.0, false), 0.99F, 0.0001);
}

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopPreviewRejectsEndpointSnapsWhenGridStillFitsEdge) {
	const quader_poly::Document cube = quader_poly::make_cube({});
	EXPECT_FALSE(cube.faces.empty());
	EXPECT_TRUE(cube.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { cube.faces[0].vertices[0], cube.faces[0].vertices[1] };

	const std::optional<float> midpoint =
		quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.49F, 1.0, true);
	EXPECT_TRUE(midpoint.has_value());
	EXPECT_NEAR(*midpoint, 0.5F, 0.0001);
	EXPECT_FALSE(quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.04F, 1.0, true).has_value());
	EXPECT_FALSE(quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.96F, 1.0, true).has_value());

	const std::optional<float> free_slide =
		quader_editor_core::snap_insert_edge_loop_preview_factor(cube, edge, 0.04F, 1.0, false);
	EXPECT_TRUE(free_slide.has_value());
	EXPECT_NEAR(*free_slide, 0.04F, 0.0001);
}

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopPreviewFallsBackToMidpointWhenGridHasNoInteriorPosition) {
	const quader_poly::Document cube = quader_poly::make_cube({});
	ASSERT_FALSE(cube.faces.empty());
	ASSERT_TRUE(cube.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { cube.faces[0].vertices[0], cube.faces[0].vertices[1] };
	const quader_poly::Vertex *start = quader_poly::find_vertex(cube, edge.a);
	const quader_poly::Vertex *end = quader_poly::find_vertex(cube, edge.b);
	ASSERT_TRUE(start != nullptr);
	ASSERT_TRUE(end != nullptr);
	const double edge_length = static_cast<double>(quader::length(end->position - start->position));
	ASSERT_GT(edge_length, 0.0001);

	for (const double grid_size : { edge_length, edge_length * 2.0 }) {
		for (const float hovered_factor : { 0.04F, 0.49F, 0.96F }) {
			const std::optional<float> resolved =
				quader_editor_core::snap_insert_edge_loop_preview_factor(
					cube, edge, hovered_factor, grid_size, true);
			ASSERT_TRUE(resolved.has_value());
			EXPECT_NEAR(*resolved, 0.5F, 0.0001);
		}
	}
}

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopPreviewStoresSeedAndClearsHoverCandidate) {
	quader_editor_core::PolygonObject object;
	object.id = 3;
	object.document = quader_poly::make_cube({});
	EXPECT_FALSE(object.document.faces.empty());
	EXPECT_TRUE(object.document.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { object.document.faces[0].vertices[0], object.document.faces[0].vertices[1] };
	std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::InsertEdgeLoopToolState state;
	quader_editor_core::SelectionHoverState hover;
	hover.object_id = 3;
	hover.candidate_object_id = 3;

	const bool changed =
		quader_editor_core::set_insert_edge_loop_preview(state, objects, hover, 3, edge, 0.5F, true);
	EXPECT_TRUE(changed);
	EXPECT_TRUE(state.active);
	EXPECT_TRUE(state.dragging);
	EXPECT_TRUE(state.object_id == 3);
	EXPECT_TRUE(state.edge.a == edge.a);
	EXPECT_TRUE(state.edge.b == edge.b);
	EXPECT_NEAR(state.factor, 0.5F, 0.0001);
	EXPECT_FALSE(state.preview_lines.empty());
	EXPECT_TRUE(quader_editor_core::insert_edge_loop_tool_has_seed(state));
	EXPECT_TRUE(hover.candidate_object_id == 3);
}

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopLockedSeedRejectsLowerGridEndpointAndFallsBackForCoarseGrid) {
	quader_editor_core::PolygonObject object;
	object.id = 3;
	object.document = quader_poly::make_cube({});
	EXPECT_FALSE(object.document.faces.empty());
	EXPECT_TRUE(object.document.faces[0].vertices.size() >= 2);
	const quader_poly::Edge edge { object.document.faces[0].vertices[0], object.document.faces[0].vertices[1] };
	const quader_poly::Vertex *start = quader_poly::find_vertex(object.document, edge.a);
	const quader_poly::Vertex *end = quader_poly::find_vertex(object.document, edge.b);
	EXPECT_TRUE(start != nullptr);
	EXPECT_TRUE(end != nullptr);
	std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::InsertEdgeLoopToolState state;
	quader_editor_core::SelectionHoverState hover;
	EXPECT_TRUE(quader_editor_core::set_insert_edge_loop_preview(state, objects, hover, 3, edge, 0.5F, true));
	EXPECT_FALSE(state.preview_lines.empty());

	const quader::QVec3 near_start {
		start->position.x + ((end->position.x - start->position.x) * 0.04F),
		start->position.y + ((end->position.y - start->position.y) * 0.04F),
		start->position.z + ((end->position.z - start->position.z) * 0.04F),
	};
	const quader_poly::Ray ray {
		{ near_start.x, near_start.y + 1.0F, near_start.z },
		{ 0.0F, -1.0F, 0.0F },
	};

	EXPECT_FALSE(quader_editor_core::update_insert_edge_loop_locked_seed(state, objects, hover, ray, 1.0, true, true));
	EXPECT_TRUE(state.active);
	EXPECT_TRUE(state.dragging);
	EXPECT_TRUE(state.object_id == 3);
	EXPECT_TRUE(state.edge == quader_poly::make_edge(edge.a, edge.b));
	EXPECT_TRUE(state.preview_lines.empty());
	EXPECT_TRUE(hover.object_id == 3);
        EXPECT_TRUE(hover.kind == quader_editor_core::SelectionHoverKind::Edge);

	const double edge_length = static_cast<double>(quader::length(end->position - start->position));
	ASSERT_GT(edge_length, 0.0001);
	EXPECT_TRUE(quader_editor_core::update_insert_edge_loop_locked_seed(
		state, objects, hover, ray, edge_length, true, true));
	EXPECT_NEAR(state.factor, 0.5F, 0.0001);
	EXPECT_FALSE(state.preview_lines.empty());
}

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopResetCanPreserveActiveOwnershipThroughRuntimeHelper) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool =
            quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
        runtime.insert_edge_loop.active = true;
	runtime.insert_edge_loop.dragging = true;
	runtime.insert_edge_loop.object_id = 3;
	runtime.insert_edge_loop.preview_lines.push_back({});

	quader_editor_core::reset_editor_runtime_insert_edge_loop_state(runtime, true);
	EXPECT_TRUE(runtime.insert_edge_loop.active);
	EXPECT_FALSE(runtime.insert_edge_loop.dragging);
	EXPECT_TRUE(runtime.insert_edge_loop.object_id == -1);
	EXPECT_TRUE(runtime.insert_edge_loop.preview_lines.empty());

	quader_editor_core::reset_editor_runtime_insert_edge_loop_state(runtime, false);
	EXPECT_FALSE(runtime.insert_edge_loop.active);
}

TEST(QdrModelingRuntimeToolInsertEdgeLoopTests, InsertEdgeLoopRuntimeHelpersPublishDragStateAndPromoteHoverCandidates) {
	quader_editor_core::EditorRuntime runtime;
	runtime.state().hover.object_id = 42;
	runtime.state().box.preview_valid = true;
	runtime.state().box_grid_size = 4.0;
	runtime.state().knife.active = true;

	quader_editor_core::EditorRuntimeToolActivationRequest request;
        request.requested_tool =
            quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
        request.selection_mode = quader_editor_core::SelectionMode::Object;
        const quader_editor_core::EditorRuntimeToolActivationResult result = runtime.activate_tool(request);

	EXPECT_TRUE(result.applied);
        EXPECT_TRUE(result.plan.selection_mode ==
                    quader_editor_core::SelectionMode::Edge);
        EXPECT_TRUE(result.plan.selection_mode_changed);
        EXPECT_TRUE(
            runtime.state().active_tool ==
            quader_editor_core::ViewportInteractionTool::InsertEdgeLoop);
        EXPECT_TRUE(runtime.state().insert_edge_loop.active);
	EXPECT_FALSE(runtime.state().knife.active);
	EXPECT_TRUE(runtime.state().hover.candidate_object_id == 42);
	EXPECT_FALSE(quader_editor_core::editor_runtime_status(runtime.state()).box_active);
        EXPECT_TRUE(runtime.state().box_grid_size ==
                    quader_editor_core::kDefaultEditorRuntimeBoxGridSize);

        quader_editor_core::InsertEdgeLoopToolState edge_loop;
	edge_loop.active = true;
	edge_loop.dragging = true;
	edge_loop.object_id = 13;
	edge_loop.edge = { 1, 2 };
	edge_loop.preview_lines.push_back({});
	runtime.set_insert_edge_loop_state(edge_loop);
	EXPECT_TRUE(runtime.status().insert_edge_loop_active);
	EXPECT_TRUE(runtime.status().insert_edge_loop_dragging);

	runtime.set_insert_edge_loop_dragging(false);
	EXPECT_TRUE(runtime.status().insert_edge_loop_active);
	EXPECT_FALSE(runtime.status().insert_edge_loop_dragging);
}
