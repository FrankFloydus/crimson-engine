////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <ranges>

using namespace quader_poly;

TEST(QdrDocumentOperationSplitEdgesTests, SplitEdgesSeparatesAdjacentFacesWithoutDeletingEitherSide) {
	Document document = make_cube({});
	for (Face& face : document.faces) {
		const float island = static_cast<float>(face.id) * 10.0F;
		face.uvs = {{island, 0.0F}, {island + 2.0F, 0.0F},
		            {island + 3.0F, 1.0F}, {island, 4.0F}};
	}
	const Document source = document;
	const Edge source_edge = document_edges(document).front();
	Selection selection = qdr_test::edge_selection({ source_edge }, source_edge);
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = split_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.vertices.size() == 2U);
	EXPECT_TRUE(result.created.edges.size() == 1U);
	EXPECT_TRUE(result.created.faces.empty());
	EXPECT_TRUE(result.deleted.empty());
	EXPECT_TRUE(result.affected.vertices.empty());
	EXPECT_TRUE(result.affected.edges.size() == 1U);
	EXPECT_TRUE(result.affected.edges.front() == source_edge);
	EXPECT_TRUE(result.affected.faces.size() == 1U);
	EXPECT_TRUE(result.created.edges.front() != source_edge);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == face_count_before);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 2U);
	for (const Edge& edge : selection.edges) {
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, edge) == 1);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	for (const Face& face : document.faces) {
		const Face* before = find_face(source, face.id);
		ASSERT_NE(before, nullptr);
		ASSERT_EQ(face.uvs.size(), before->uvs.size());
		ASSERT_EQ(face.vertices.size(), before->vertices.size());
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			EXPECT_EQ(face.uvs[corner].x, before->uvs[corner].x);
			EXPECT_EQ(face.uvs[corner].y, before->uvs[corner].y);
			EXPECT_TRUE(qdr_test::vec_nearly_equal(
				find_vertex(document, face.vertices[corner])->position,
				find_vertex(source, before->vertices[corner])->position));
		}
	}
}

TEST(QdrDocumentOperationSplitEdgesTests,
	 SplitEdgesCopiesHardSoftAndDefaultModesToEveryRekeyedFaceEdge) {
	Document document = make_cube({});
	const Edge selected_edge = document_edges(document).front();
	const auto source_face_it = std::ranges::find_if(
		document.faces,
		[selected_edge](const Face& face) {
			for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
				if (make_edge(
						face.vertices[index],
						face.vertices[(index + 1U) % face.vertices.size()]) ==
				    selected_edge) {
					return true;
				}
			}
			return false;
		});
	ASSERT_NE(source_face_it, document.faces.end());
	const Face source_face = *source_face_it;
	std::size_t selected_index = source_face.vertices.size();
	for (std::size_t index = 0U; index < source_face.vertices.size(); ++index) {
		if (make_edge(source_face.vertices[index],
		              source_face.vertices[(index + 1U) %
		                                   source_face.vertices.size()]) ==
		    selected_edge) {
			selected_index = index;
			break;
		}
	}
	ASSERT_LT(selected_index, source_face.vertices.size());
	const std::size_t previous_index =
		(selected_index + source_face.vertices.size() - 1U) %
		source_face.vertices.size();
	const std::size_t next_index =
		(selected_index + 1U) % source_face.vertices.size();
	const std::size_t default_index =
		(selected_index + 2U) % source_face.vertices.size();
	const Edge hard_neighbor = make_edge(
		source_face.vertices[previous_index],
		source_face.vertices[(previous_index + 1U) % source_face.vertices.size()]);
	const Edge soft_neighbor = make_edge(
		source_face.vertices[next_index],
		source_face.vertices[(next_index + 1U) % source_face.vertices.size()]);
	document.hard_normal_edges = {selected_edge, hard_neighbor};
	document.soft_normal_edges = {soft_neighbor};
	Selection selection = qdr_test::edge_selection({selected_edge}, selected_edge);

	const OperationResult result = split_selected_edges(document, selection);

	ASSERT_TRUE(result.changed) << result.message;
	qdr_test::expect_valid_polygon_document_after_success(document, result);
	const Face* rekeyed_face = find_face(document, source_face.id);
	ASSERT_NE(rekeyed_face, nullptr);
	ASSERT_EQ(rekeyed_face->vertices.size(), source_face.vertices.size());
	const auto rekeyed_edge = [&](std::size_t index) {
		return make_edge(
			rekeyed_face->vertices[index],
			rekeyed_face->vertices[(index + 1U) % rekeyed_face->vertices.size()]);
	};
	EXPECT_TRUE(qdr_test::contains_edge(
		document.hard_normal_edges, rekeyed_edge(selected_index)));
	EXPECT_TRUE(qdr_test::contains_edge(
		document.hard_normal_edges, rekeyed_edge(previous_index)));
	EXPECT_TRUE(qdr_test::contains_edge(
		document.soft_normal_edges, rekeyed_edge(next_index)));
	EXPECT_FALSE(qdr_test::contains_edge(
		document.hard_normal_edges, rekeyed_edge(default_index)));
	EXPECT_FALSE(qdr_test::contains_edge(
		document.soft_normal_edges, rekeyed_edge(default_index)));
	EXPECT_TRUE(qdr_test::contains_edge(
		document.hard_normal_edges, selected_edge));
	EXPECT_TRUE(qdr_test::contains_edge(
		document.hard_normal_edges, hard_neighbor));
	EXPECT_TRUE(qdr_test::contains_edge(
		document.soft_normal_edges, soft_neighbor));
}
