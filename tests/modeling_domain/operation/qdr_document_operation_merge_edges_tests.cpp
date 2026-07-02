////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

#include <array>

using namespace quader_poly;

namespace {

Document make_parallel_quad_edge_document(Edge& active_edge, Edge& source_edge)
{
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId source_right = add_vertex(document, { 1.0F, 3.0F, 0.0F });
	const ElementId source_left = add_vertex(document, { -1.0F, 3.0F, 0.0F });
	const ElementId source_top_left = add_vertex(document, { -1.0F, 4.0F, 0.0F });
	const ElementId source_top_right = add_vertex(document, { 1.0F, 4.0F, 0.0F });

	const std::array first_face { bottom_left, bottom_right, top_right, top_left };
	const std::array second_face { source_right, source_left, source_top_left, source_top_right };
        EXPECT_TRUE(add_face(document, first_face) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, second_face) != kInvalidElementId);

        active_edge = make_edge(bottom_left, bottom_right);
	source_edge = make_edge(source_right, source_left);
	return document;
}

} // namespace

TEST(QdrDocumentOperationMergeEdgesTests, MergeEdgesWeldsSourceEndpointsIntoTheClosestSurvivorEdgeVertices) {
	Edge active_edge;
	Edge source_edge;
	Document document = make_parallel_quad_edge_document(active_edge, source_edge);
	Selection selection = qdr_test::edge_selection({ active_edge, source_edge }, active_edge);

	const OperationResult result = merge_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_vertex(document, active_edge.a) != nullptr);
	EXPECT_TRUE(find_vertex(document, active_edge.b) != nullptr);
	EXPECT_TRUE(find_vertex(document, source_edge.a) == nullptr);
	EXPECT_TRUE(find_vertex(document, source_edge.b) == nullptr);
	EXPECT_TRUE(document.faces.size() == 2U);
	EXPECT_TRUE(qdr_test::document_has_edge(document, active_edge));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(selection.edges.front() == active_edge);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_edge == active_edge);
}

TEST(QdrDocumentOperationMergeEdgesTests, MergeEdgesRejectsIncompleteEdgeSelections) {
	Edge active_edge;
	Edge source_edge;
	Document document = make_parallel_quad_edge_document(active_edge, source_edge);
	Selection selection = qdr_test::edge_selection({ active_edge }, active_edge);

	const OperationResult result = merge_selected_edges(document, selection);

	EXPECT_FALSE(result.changed);
	EXPECT_FALSE(result.message.empty());
}
