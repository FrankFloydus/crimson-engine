////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests, BridgeEdgeBoundariesCreatesABridgeBetweenMatchingOpenChainsClosedMatchingBoundariesCreateASteppedShellBetweenFaceIslands) {
	{
		Document document = qdr_test::make_parallel_quad_document();
		EXPECT_TRUE(document.faces.size() == 2U);
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		const std::vector<ElementId> second_vertices = document.faces[1].vertices;
		Selection selection;

		const OperationResult result = bridge_edge_boundaries(document, selection, first_vertices, second_vertices, true, 2);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(document, result);
                EXPECT_TRUE(selection.mode == SelectionMode::Face);
                EXPECT_TRUE(selection.faces.size() == 8U);
		EXPECT_TRUE(document.faces.size() == 10U);
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}

	
}

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests, BridgeEdgeBoundariesKeepsSharedEdgesOppositeAcrossStepCounts) {
	for (const int steps : {1, 3}) {
		Document document = qdr_test::make_parallel_quad_document();
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		const std::vector<ElementId> second_vertices = document.faces[1].vertices;
		Selection selection;

		const OperationResult result = bridge_edge_boundaries(
			document, selection, first_vertices, second_vertices, true, steps);

		ASSERT_TRUE(result.changed) << result.message;
		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
		EXPECT_EQ(selection.mode, SelectionMode::Face);
		EXPECT_EQ(selection.faces.size(), 4U * static_cast<std::size_t>(steps));
	}
}

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests, BridgeEdgeBoundariesCreatesABridgeBetweenMatchingOpenChainsMismatchedOrTooShortBoundariesFailWithoutMutating) {
	

	{
		Document document = qdr_test::make_parallel_quad_document();
		const std::size_t face_count_before = document.faces.size();
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		std::vector<ElementId> second_vertices = document.faces[1].vertices;
		second_vertices.pop_back();
		Selection selection;

		const OperationResult mismatch_result = bridge_edge_boundaries(document, selection, first_vertices, second_vertices, true, 1);
		EXPECT_FALSE(mismatch_result.changed);
		EXPECT_TRUE(mismatch_result.message == "Bridge needs matching boundary vertex counts.");
		EXPECT_TRUE(document.faces.size() == face_count_before);

		const std::vector<ElementId> short_vertices { first_vertices.front() };
		const OperationResult short_result = bridge_edge_boundaries(document, selection, short_vertices, short_vertices, false, 1);
		EXPECT_FALSE(short_result.changed);
		EXPECT_TRUE(short_result.message == "Bridge needs two valid boundary edge chains.");
	}
}

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests,
     SymmetricClosedBoundariesExposeAStableActualRailWhenMeanTangentsCancel) {
  for (const float scale : {0.01F, 1.0F, 100.0F}) {
    SCOPED_TRACE(scale);
    Document source = qdr_test::make_parallel_quad_document();
    for (Vertex &vertex : source.vertices)
      vertex.position = vertex.position * scale;
    const auto first_vertices = source.faces[0].vertices;
    const auto second_vertices = source.faces[1].vertices;
    source.faces[0].material_slot = 7U;
    source.faces[0].texture_transform = {{2.0F, -0.5F}, {0.25F, 0.75F}, 55.0F};
    source.hard_normal_edges = {
        make_edge(first_vertices[0], first_vertices[1])};
    source.soft_normal_edges = {
        make_edge(second_vertices[0], second_vertices[1])};
    const ElementId first_generated = source.next_vertex_id;
    const auto *start = find_vertex(source, first_vertices.front());
    const auto *end = find_vertex(source, second_vertices.front());
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    std::optional<BridgeShapeFeedback> previous;
    for (const float start_tension : {1.0F, 2.0F}) {
      Document document = source;
      Selection selection;
      const OperationResult result = bridge_edge_boundaries(
          document, selection, first_vertices, second_vertices, true,
          BridgeOptions{.steps = 4, .start_tension = start_tension});
      ASSERT_TRUE(result.success) << result.message;
      ASSERT_TRUE(result.bridge_shape.has_value());
      const auto &shape = *result.bridge_shape;
      EXPECT_TRUE(shape.representative_rail);
      EXPECT_GT(quader::length_squared(shape.start_handle_axis), 0.0F);
      EXPECT_GT(quader::length_squared(shape.end_handle_axis), 0.0F);
      EXPECT_FLOAT_EQ(shape.controls[0].x, start->position.x);
      EXPECT_FLOAT_EQ(shape.controls[0].y, start->position.y);
      EXPECT_FLOAT_EQ(shape.controls[0].z, start->position.z);
      EXPECT_FLOAT_EQ(shape.controls[3].x, end->position.x);
      EXPECT_FLOAT_EQ(shape.controls[3].y, end->position.y);
      EXPECT_FLOAT_EQ(shape.controls[3].z, end->position.z);
      for (int step = 1; step < 4; ++step) {
        const float t = static_cast<float>(step) / 4.0F;
        const float u = 1.0F - t;
        const auto sample = shape.controls[0] * (u * u * u) +
                            shape.controls[1] * (3.0F * u * u * t) +
                            shape.controls[2] * (3.0F * u * t * t) +
                            shape.controls[3] * (t * t * t);
        EXPECT_TRUE(
            std::ranges::any_of(document.vertices, [&](const Vertex &vertex) {
              return vertex.id >= first_generated &&
                     quader::length_squared(vertex.position - sample) <
                         scale * scale * 0.000000001F;
            }));
      }
      for (const Vertex &vertex : source.vertices) {
        const Vertex *retained = find_vertex(document, vertex.id);
        ASSERT_NE(retained, nullptr);
        EXPECT_FLOAT_EQ(retained->position.x, vertex.position.x);
        EXPECT_FLOAT_EQ(retained->position.y, vertex.position.y);
        EXPECT_FLOAT_EQ(retained->position.z, vertex.position.z);
      }
      for (const Face &face : source.faces) {
        const Face *retained = find_face(document, face.id);
        ASSERT_NE(retained, nullptr);
        EXPECT_EQ(retained->vertices, face.vertices);
        EXPECT_EQ(retained->material_slot, face.material_slot);
        EXPECT_EQ(retained->texture_transform, face.texture_transform);
        EXPECT_EQ(retained->normal_shading, face.normal_shading);
      }
      EXPECT_EQ(document.hard_normal_edges, source.hard_normal_edges);
      EXPECT_EQ(document.soft_normal_edges, source.soft_normal_edges);
      EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
      EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
      if (previous.has_value()) {
        EXPECT_GT(
            quader::length_squared(shape.controls[1] - previous->controls[1]),
            0.0F);
        EXPECT_FLOAT_EQ(shape.start_handle_axis.x,
                        previous->start_handle_axis.x);
        EXPECT_FLOAT_EQ(shape.start_handle_axis.y,
                        previous->start_handle_axis.y);
        EXPECT_FLOAT_EQ(shape.start_handle_axis.z,
                        previous->start_handle_axis.z);
      }
      previous = shape;
      qdr_test::expect_valid_polygon_document_after_success(document, result);
    }
  }
}
