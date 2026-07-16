////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>

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
        hit.kind = quader_poly::ElementKind::Face;
        hit.face_id = face_id;
	return hit;
}

} // namespace

TEST(QdrModelingRuntimeToolSelectTests, SelectionEditOperationsPreserveExpectedHostIds) {
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_id(
                  quader_editor_core::SelectionEditOperation::Replace) == 0);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_id(
                  quader_editor_core::SelectionEditOperation::Add) == 1);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_id(
                  quader_editor_core::SelectionEditOperation::Remove) == 2);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_id(
                  quader_editor_core::SelectionEditOperation::Toggle) == 3);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_id(0) ==
              quader_editor_core::SelectionEditOperation::Replace);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_id(1) ==
              quader_editor_core::SelectionEditOperation::Add);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_id(2) ==
              quader_editor_core::SelectionEditOperation::Remove);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_id(3) ==
              quader_editor_core::SelectionEditOperation::Toggle);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_id(99) ==
              quader_editor_core::SelectionEditOperation::Replace);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_modifiers(
                  false, false) ==
              quader_editor_core::SelectionEditOperation::Replace);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_modifiers(
                  true, false) ==
              quader_editor_core::SelectionEditOperation::Remove);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_from_modifiers(
                  false, true) ==
              quader_editor_core::SelectionEditOperation::Add);
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_preserves_existing(
      quader_editor_core::SelectionEditOperation::Add));
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_preserves_existing(
      quader_editor_core::SelectionEditOperation::Remove));
  EXPECT_TRUE(quader_editor_core::selection_edit_operation_preserves_existing(
      quader_editor_core::SelectionEditOperation::Toggle));
  EXPECT_FALSE(quader_editor_core::selection_edit_operation_preserves_existing(
      quader_editor_core::SelectionEditOperation::Replace));
}

TEST(QdrModelingRuntimeToolSelectTests, RectangleSelectionPromotesFullEdgeObjectsAndKeepsActiveTarget) {
	quader_editor_core::RectangleSelectionRequest request;
        request.current_selection.mode =
            quader_editor_core::SelectionMode::Edge;
        request.operation = quader_editor_core::SelectionEditOperation::Replace;
        request.universe = simple_universe();
	request.hits.edges = {
		{ 1, 1, 0 },
		{ 1, 1, 2 },
	};

	const quader_editor_core::RectangleSelectionResult result =
		quader_editor_core::apply_rectangle_selection(request);
        EXPECT_TRUE(result.selection.mode ==
                    quader_editor_core::SelectionMode::Edge);
        EXPECT_TRUE(result.selection.selected_edge_full_object_ids.size() == 1);
	EXPECT_TRUE(result.selection.selected_edge_full_object_ids[0] == 1);
	EXPECT_TRUE(result.selection.selected_edges.empty());
	EXPECT_TRUE(result.selection.active_object_id == 1);
}

TEST(QdrModelingRuntimeToolSelectTests, RectangleSelectionRemoveEditsExistingFaceSelections) {
	quader_editor_core::RectangleSelectionRequest request;
        request.current_selection.mode =
            quader_editor_core::SelectionMode::Face;
        request.current_selection.selected_faces = {
		{ 1, 0, { 0, 1, 2, 3 } },
		{ 1, 1, { 4, 5, 6, 7 } },
		{ 2, 0, { 0, 1, 2, 3 } },
	};
        request.operation = quader_editor_core::SelectionEditOperation::Remove;
        request.universe = simple_universe();
	request.hits.faces = {
		{ 1, 1, { 4, 5, 6, 7 } },
	};

	const quader_editor_core::RectangleSelectionResult result =
		quader_editor_core::apply_rectangle_selection(request);
        EXPECT_TRUE(result.selection.mode ==
                    quader_editor_core::SelectionMode::Face);
        EXPECT_TRUE(result.selection.selected_faces.size() == 2);
	EXPECT_TRUE(result.selection.selected_faces[0].object_id == 1);
	EXPECT_TRUE(result.selection.selected_faces[0].face_index == 0);
	EXPECT_TRUE(result.selection.selected_faces[1].object_id == 2);
	EXPECT_TRUE(result.selection.selected_faces[1].face_index == 0);
	EXPECT_TRUE(result.selection.active_object_id == 2);
	EXPECT_TRUE(result.selection.active_face_index == 0);
}

TEST(QdrModelingRuntimeToolSelectTests, ProjectedRectangleObjectHitsDistinguishContainAndCrossing) {
	quader_editor_core::RectangleProjectedHitsRequest request;
        request.mode = quader_editor_core::SelectionMode::Object;
        request.rect = { 0.0, 0.0, 10.0, 10.0 };
	request.objects = {
		{
			.object_id = 1,
			.has_screen_bounds = true,
			.screen_min = { 2.0f, 2.0f },
			.screen_max = { 8.0f, 8.0f },
			.screen_points = { { 2.0f, 2.0f }, { 8.0f, 8.0f } },
		},
		{
			.object_id = 2,
			.has_screen_bounds = true,
			.screen_min = { -2.0f, 2.0f },
			.screen_max = { 5.0f, 8.0f },
			.screen_points = { { -2.0f, 2.0f }, { 5.0f, 8.0f } },
		},
	};

        request.match = quader_editor_core::RectangleSelectionMatch::Contain;
        quader_editor_core::RectangleSelectionHits hits =
		quader_editor_core::rectangle_hits_from_projected_candidates(request);
	EXPECT_TRUE(hits.object_ids.size() == 1);
	EXPECT_TRUE(hits.object_ids[0] == 1);

        request.match = quader_editor_core::RectangleSelectionMatch::Crossing;
        hits = quader_editor_core::rectangle_hits_from_projected_candidates(request);
	EXPECT_TRUE(hits.object_ids.size() == 2);
	EXPECT_TRUE(hits.object_ids[0] == 1);
	EXPECT_TRUE(hits.object_ids[1] == 2);
}

TEST(QdrModelingRuntimeToolSelectTests, ProjectedRectangleObjectCrossingIgnoresLooseScreenBoundsGaps) {
	quader_editor_core::RectangleProjectedHitsRequest request;
        request.mode = quader_editor_core::SelectionMode::Object;
        request.match = quader_editor_core::RectangleSelectionMatch::Crossing;
        request.objects = {
		{
			.object_id = 1,
			.has_screen_center = true,
			.screen_center = { 5.0f, 5.0f },
			.has_screen_bounds = true,
			.screen_min = { 0.0f, 0.0f },
			.screen_max = { 10.0f, 10.0f },
			.screen_points = { { 5.0f, 0.0f }, { 10.0f, 5.0f }, { 5.0f, 10.0f }, { 0.0f, 5.0f } },
			.screen_segments = {
				{ { 5.0f, 0.0f }, { 10.0f, 5.0f } },
				{ { 10.0f, 5.0f }, { 5.0f, 10.0f } },
				{ { 5.0f, 10.0f }, { 0.0f, 5.0f } },
				{ { 0.0f, 5.0f }, { 5.0f, 0.0f } },
			},
			.screen_polygons = { { { 5.0f, 0.0f }, { 10.0f, 5.0f }, { 5.0f, 10.0f }, { 0.0f, 5.0f } } },
		},
	};

	request.rect = { 0.5, 0.5, 2.0, 2.0 };
	EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(request).object_ids.empty());

	request.rect = { 1.0, 4.0, 3.0, 6.0 };
	EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(request).object_ids.size() == 1);

	request.rect = { 4.0, 4.0, 6.0, 6.0 };
	EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(request).object_ids.size() == 1);
}

TEST(QdrModelingRuntimeToolSelectTests, ProjectedRectangleComponentHitsUseSegmentAndPolygonCrossing) {
	quader_editor_core::RectangleProjectedHitsRequest edge_request;
        edge_request.mode = quader_editor_core::SelectionMode::Edge;
        edge_request.rect = { 0.0, 0.0, 10.0, 10.0 };
	edge_request.edges = {
		{
			.object_id = 1,
			.a_index = 1,
			.b_index = 2,
			.has_screen_start = true,
			.screen_start = { -5.0f, 5.0f },
			.has_screen_end = true,
			.screen_end = { 15.0f, 5.0f },
		},
	};
        edge_request.match =
            quader_editor_core::RectangleSelectionMatch::Contain;
        EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(edge_request).edges.empty());
        edge_request.match =
            quader_editor_core::RectangleSelectionMatch::Crossing;
        EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(edge_request).edges.size() == 1);

	quader_editor_core::RectangleProjectedHitsRequest face_request;
        face_request.mode = quader_editor_core::SelectionMode::Face;
        face_request.rect = { 0.0, 0.0, 10.0, 10.0 };
	face_request.faces = {
		{
			.object_id = 3,
			.face_index = 7,
			.vertex_indices = { 1, 2, 3, 4 },
			.screen_vertices = {
				{ -10.0f, -10.0f },
				{ 20.0f, -10.0f },
				{ 20.0f, 20.0f },
				{ -10.0f, 20.0f },
			},
		},
	};
        face_request.match =
            quader_editor_core::RectangleSelectionMatch::Contain;
        EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(face_request).faces.empty());
        face_request.match =
            quader_editor_core::RectangleSelectionMatch::Crossing;
        EXPECT_TRUE(quader_editor_core::rectangle_hits_from_projected_candidates(face_request).faces.size() == 1);
}

TEST(QdrModelingRuntimeToolSelectTests, ProjectedRectangleVertexHitsRequirePointContainment) {
	quader_editor_core::RectangleProjectedHitsRequest request;
        request.mode = quader_editor_core::SelectionMode::Vertex;
        request.rect = { 0.0, 0.0, 10.0, 10.0 };
	request.vertices = {
		{ .object_id = 1, .vertex_index = 4, .has_screen_position = true, .screen_position = { 5.0f, 5.0f } },
		{ .object_id = 1, .vertex_index = 5, .has_screen_position = true, .screen_position = { 11.0f, 5.0f } },
	};

        request.match = quader_editor_core::RectangleSelectionMatch::Contain;
        quader_editor_core::RectangleSelectionHits hits =
		quader_editor_core::rectangle_hits_from_projected_candidates(request);
	EXPECT_TRUE(hits.vertices.size() == 1);
	EXPECT_TRUE(hits.vertices[0].vertex_index == 4);

        request.match = quader_editor_core::RectangleSelectionMatch::Crossing;
        hits = quader_editor_core::rectangle_hits_from_projected_candidates(request);
	EXPECT_TRUE(hits.vertices.size() == 1);
	EXPECT_TRUE(hits.vertices[0].vertex_index == 4);
}

TEST(QdrModelingRuntimeToolSelectTests, HandleSelectionPolicyTogglesGroupedEdgeAndFaceHandles) {
	quader_editor_core::SelectionHandleRequest request;
        request.current_selection.mode =
            quader_editor_core::SelectionMode::Edge;
        request.handle.kind = quader_editor_core::ActiveSelectionKind::Edge;
        request.handle.object_id = 80;
	request.handle.vertex_indices = { 2, 5 };
	request.handle.element_groups = { { 80, { 2, 5 } }, { 81, { 1, 3 } } };

	quader_editor_core::RectangleSelectionResult result = quader_editor_core::select_handle(request);
        EXPECT_TRUE(result.selection.mode ==
                    quader_editor_core::SelectionMode::Edge);
        EXPECT_TRUE(result.selection.selected_edges.size() == 2);
	EXPECT_TRUE(result.selection.selected_object_ids.size() == 2);
	EXPECT_TRUE(result.selection.active_object_id == 80);
	EXPECT_TRUE((result.selection.active_edge_vertex_indices == std::vector<int> { 2, 5 }));

	request.current_selection = result.selection;
	request.additive = true;
	result = quader_editor_core::select_handle(request);
	EXPECT_TRUE(result.selection.selected_edges.empty());
	EXPECT_TRUE(result.selection.active_object_id == 81);

	request = {};
        request.current_selection.mode =
            quader_editor_core::SelectionMode::Face;
        request.handle.kind = quader_editor_core::ActiveSelectionKind::Face;
        request.handle.object_id = 90;
	request.handle.face_index = 4;
	request.handle.vertex_indices = { 0, 1, 2, 3 };

	result = quader_editor_core::select_handle(request);
	EXPECT_TRUE(result.selection.selected_faces.size() == 1);
	EXPECT_TRUE(result.selection.active_object_id == 90);
	EXPECT_TRUE(result.selection.active_face_index == 4);

	request.current_selection = result.selection;
	request.additive = true;
	result = quader_editor_core::select_handle(request);
	EXPECT_TRUE(result.selection.selected_faces.empty());
	EXPECT_TRUE(result.selection.active_object_id < 0);
}

TEST(QdrModelingRuntimeToolSelectTests, PickingPolicyCyclesSurfacesAndShieldsSelectedHandles) {
	std::vector<quader_editor_core::PickHitRecord> surface_hits {
		surface_hit(101, 5),
		surface_hit(102, 6),
	};

	quader_editor_core::PickSelectionState object_selection;
        object_selection.mode = quader_editor_core::SelectionMode::Object;
        object_selection.selected_object_ids = { 101 };

	quader_editor_core::ClickPickRequest click;
        click.mode = quader_editor_core::SelectionMode::Object;
        click.operation = quader_editor_core::SelectionEditOperation::Replace;
        click.screen_position = { 24.0F, 32.0F };

	quader_editor_core::PickCycleState cycle;
	quader_editor_core::ClickPickPlan plan =
		quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click);
	EXPECT_TRUE(plan.has_target);
        EXPECT_TRUE(plan.hit_set == quader_editor_core::PickHitSet::Surface);
        EXPECT_TRUE(plan.hit_index == 0);

	plan = quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click);
	EXPECT_TRUE(plan.has_target);
	EXPECT_TRUE(plan.hit_index == 1);
	EXPECT_TRUE(plan.cycle.cycled);

	click.screen_position = { 64.0F, 96.0F };
	plan = quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click);
	EXPECT_TRUE(plan.has_target);
	EXPECT_TRUE(plan.hit_index == 0);
	EXPECT_FALSE(plan.cycle.cycled);

	quader_editor_core::PickHitRecord selected_edge_handle = surface_hits[0];
        selected_edge_handle.kind = quader_poly::ElementKind::Edge;
        selected_edge_handle.edge = { 3, 9 };
        selected_edge_handle.face_id = quader_poly::kInvalidElementId;
        quader_editor_core::PickHitRecord unselected_edge_handle = selected_edge_handle;
	unselected_edge_handle.object_id = 102;
	std::vector<quader_editor_core::PickHitRecord> handle_hits {
		selected_edge_handle,
		unselected_edge_handle,
	};

	quader_editor_core::PickSelectionState edge_selection;
        edge_selection.mode = quader_editor_core::SelectionMode::Edge;
        edge_selection.selected_edges.push_back({101,
                                                 quader_poly::ElementKind::Edge,
                                                 quader_poly::kInvalidElementId,
                                                 {9, 3}});

        click.mode = quader_editor_core::SelectionMode::Edge;
        click.operation = quader_editor_core::SelectionEditOperation::Add;
        click.handles_enabled = true;
	plan = quader_editor_core::plan_click_pick(cycle, edge_selection, handle_hits, {}, click);
	EXPECT_FALSE(plan.has_target);
	EXPECT_TRUE(plan.clear_with_handle_state_on_miss);

        click.operation = quader_editor_core::SelectionEditOperation::Remove;
        plan = quader_editor_core::plan_click_pick(cycle, edge_selection, handle_hits, surface_hits, click);
	EXPECT_TRUE(plan.has_target);
        EXPECT_TRUE(plan.hit_set == quader_editor_core::PickHitSet::Handle);
        EXPECT_TRUE(plan.preserve_selection);
}

TEST(QdrModelingRuntimeToolSelectTests, ElementHandlePickingAndOwnershipPolicyAreStable) {
	quader_editor_core::SelectionHandleTargetObjectRequest targets;
        targets.current_selection.mode =
            quader_editor_core::SelectionMode::Edge;
        targets.current_selection.selected_edges = { { 101, 1, 3 }, { 102, 2, 4 }, { 101, 5, 6 } };
	targets.extra_target_object_ids = { 103, 101 };

	const std::vector<int> target_ids = quader_editor_core::handle_target_object_ids_for_selection(targets);
	EXPECT_TRUE((target_ids == std::vector<int> { 101, 102, 103 }));

	std::vector<quader_editor_core::ElementHandlePickCandidate> candidates;
        candidates.push_back({0,
                              quader_editor_core::ElementHandlePickKind::Edge,
                              110,
                              4,
                              9,
                              {20.0F, 10.0F},
                              4.0});
        candidates.push_back({1,
                              quader_editor_core::ElementHandlePickKind::Vertex,
                              111,
                              2,
                              -1,
                              {18.0F, 10.0F},
                              8.0});
        candidates.push_back({2,
                              quader_editor_core::ElementHandlePickKind::Face,
                              109,
                              1,
                              -1,
                              {18.0F, 10.0F},
                              2.0});
        candidates.push_back({3,
                              quader_editor_core::ElementHandlePickKind::Vertex,
                              108,
                              1,
                              -1,
                              {60.0F, 10.0F},
                              1.0});

        const std::vector<quader_editor_core::ElementHandlePickHit> hits =
		quader_editor_core::pick_element_handle_candidates(candidates, { 18.0F, 10.0F }, 8.0);
	EXPECT_TRUE(hits.size() == 3);
	EXPECT_TRUE(hits[0].candidate_index == 2);
	EXPECT_TRUE(hits[1].candidate_index == 1);
	EXPECT_TRUE(hits[2].candidate_index == 0);

        EXPECT_TRUE(quader_editor_core::selection_mode_uses_element_handles(
            quader_editor_core::SelectionMode::Face));
        EXPECT_TRUE(quader_editor_core::selection_mode_uses_element_handles(
            quader_editor_core::SelectionMode::Edge));
        EXPECT_TRUE(quader_editor_core::selection_mode_uses_element_handles(
            quader_editor_core::SelectionMode::Vertex));
        EXPECT_FALSE(quader_editor_core::selection_mode_uses_element_handles(
            quader_editor_core::SelectionMode::Object));
        EXPECT_TRUE(
            quader_editor_core::default_viewport_tool_for_selection_mode(
                quader_editor_core::SelectionMode::Face) ==
            quader_editor_core::ViewportToolIntent::Move);
}
