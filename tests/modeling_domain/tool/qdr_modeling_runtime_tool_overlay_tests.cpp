////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <modeling/interactions/interaction_api.h>

TEST_CASE("selection overlay lookup includes selected and hover targets", "[editor-core][tool][overlay]")
{
	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::face;
	selection.selected_faces = {
		{ 1, 0, { 0, 1, 2, 3 } },
	};

	quader_editor_core::SelectionOverlayLookupRequest request;
	request.current_selection = selection;
	request.extra_target_object_ids = { 2 };
	request.hovered_object_id = 3;

	const quader_editor_core::SelectionOverlayLookup lookup =
		quader_editor_core::build_selection_overlay_lookup(request);
	CHECK(quader_editor_core::selection_overlay_lookup_has_selected_face(lookup, { 1, 0, { 0, 1, 2, 3 } }));
	CHECK(quader_editor_core::selection_overlay_lookup_targets_object(lookup, 1));
	CHECK(quader_editor_core::selection_overlay_lookup_targets_object(lookup, 2));
	CHECK_FALSE(quader_editor_core::selection_overlay_lookup_targets_object(lookup, 3));
	CHECK_FALSE(quader_editor_core::selection_overlay_lookup_targets_object(lookup, 4));

	quader_editor_core::SelectionOverlayLookupRequest hover_request;
	hover_request.hovered_object_id = 3;
	const quader_editor_core::SelectionOverlayLookup hover_lookup =
		quader_editor_core::build_selection_overlay_lookup(hover_request);
	CHECK(quader_editor_core::selection_overlay_lookup_targets_object(hover_lookup, 3));
}

TEST_CASE("selection overlay lookup normalizes element handles and full object groups", "[editor-core][tool][overlay]")
{
	quader_editor_core::SelectionOverlayLookupRequest request;
	request.current_selection.mode = quader_editor_core::SelectionMode::edge;
	request.current_selection.selected_edges = { { 76, 3, 1 } };
	request.current_selection.selected_edge_full_object_ids = { 77 };

	const quader_editor_core::SelectionOverlayLookup edge_lookup =
		quader_editor_core::build_selection_overlay_lookup(request);
	CHECK(quader_editor_core::selection_overlay_lookup_has_selected_edge(edge_lookup, { 76, 1, 3 }));
	CHECK(quader_editor_core::selection_overlay_lookup_has_selected_object(edge_lookup, 77));

	request = {};
	request.current_selection.mode = quader_editor_core::SelectionMode::vertex;
	request.current_selection.selected_vertices = { { 78, 5 } };
	request.current_selection.selected_vertex_full_object_ids = { 79 };

	const quader_editor_core::SelectionOverlayLookup vertex_lookup =
		quader_editor_core::build_selection_overlay_lookup(request);
	CHECK(quader_editor_core::selection_overlay_lookup_has_selected_vertex(vertex_lookup, { 78, 5 }));
	CHECK(quader_editor_core::selection_overlay_lookup_has_selected_object(vertex_lookup, 79));
}

TEST_CASE("selection overlay policy distinguishes dense handles from sparse active targets", "[editor-core][tool][overlay]")
{
	quader_editor_core::SelectionOverlayPolicyRequest request;
	request.current_selection.mode = quader_editor_core::SelectionMode::edge;
	request.current_selection.selected_edge_full_object_ids = { 80, 81, 82 };
	request.selected_edge_limit = 24;

	quader_editor_core::SelectionOverlayPolicy policy = quader_editor_core::selection_overlay_policy(request);
	CHECK(policy.dense_selected_edge_overlay);
	CHECK(policy.dense_element_minimal_overlay);
	CHECK_FALSE(policy.should_build_lookup);

	request = {};
	request.current_selection.mode = quader_editor_core::SelectionMode::vertex;
	request.current_selection.selected_vertices = { { 83, 1 }, { 83, 2 } };
	request.current_selection.active_object_id = 83;
	request.selected_vertex_limit = 16;

	policy = quader_editor_core::selection_overlay_policy(request);
	CHECK_FALSE(policy.dense_selected_vertex_overlay);
	CHECK(policy.element_target_reference_count == 8);

	request.current_selection.mode = quader_editor_core::SelectionMode::object;
	request.current_selection.selected_object_ids = { 84 };
	request.suppress_selected_object_overlay = true;

	policy = quader_editor_core::selection_overlay_policy(request);
	CHECK(policy.suppress_selected_object_feedback);
	CHECK_FALSE(policy.should_build_lookup);
}
