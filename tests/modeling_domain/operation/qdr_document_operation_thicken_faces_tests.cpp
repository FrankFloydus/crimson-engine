////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>

using namespace quader_poly;

TEST(QdrDocumentOperationThickenFacesTests, ThickenFacesTurnsOneOpenFaceIntoAClosedThinVolume) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{2.0F, -0.5F}, {0.25F, 0.75F}, 70.0F};
	fixture.document.faces.front().texture_transform = texture;
	ensure_face_uvs(fixture.document);
	const std::vector<quader::QVec2> source_uvs = fixture.document.faces.front().uvs;
	const std::vector<ElementId> source_vertices = fixture.document.faces.front().vertices;
	const quader::QVec3 source_normal = face_normal(fixture.document, fixture.document.faces.front());
	std::vector<quader::QVec3> source_positions;
	for (const ElementId vertex_id : source_vertices) {
		const Vertex* vertex = find_vertex(fixture.document, vertex_id);
		ASSERT_NE(vertex, nullptr);
		source_positions.push_back(vertex->position);
	}
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, 0.5F, false);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.vertices.size() == 4U);
	EXPECT_TRUE(result.created.faces.size() == 5U);
	EXPECT_TRUE(result.created.edges.empty());
	EXPECT_TRUE(result.deleted.empty());
	EXPECT_TRUE(result.affected.vertices.empty());
	EXPECT_TRUE(result.affected.edges.empty());
	EXPECT_TRUE(result.affected.faces.size() == 1U);
	EXPECT_TRUE(result.affected.faces.front() == fixture.face);
	EXPECT_TRUE(fixture.document.faces.size() == 6U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}
	ASSERT_FALSE(result.created.faces.empty());
	const Face* offset_cap = find_face(fixture.document, result.created.faces.front());
	ASSERT_NE(offset_cap, nullptr);
	ASSERT_EQ(offset_cap->uvs.size(), source_uvs.size());
	EXPECT_LT(dot(face_normal(fixture.document, *offset_cap), source_normal), -0.99F);
	for (std::size_t index = 0U; index < source_uvs.size(); ++index) {
		const Vertex* offset_vertex = find_vertex(fixture.document, offset_cap->vertices[index]);
		ASSERT_NE(offset_vertex, nullptr);
		EXPECT_EQ(std::ranges::find(source_vertices, offset_vertex->id), source_vertices.end());
		const auto original = std::ranges::find_if(source_positions, [&](quader::QVec3 position) {
			return qdr_test::vec_nearly_equal(offset_vertex->position, position - source_normal * 0.5F);
		});
		ASSERT_NE(original, source_positions.end());
		const auto source_index = static_cast<std::size_t>(original - source_positions.begin());
		const auto &expected = source_uvs[source_index];
		EXPECT_EQ(offset_cap->uvs[index].x, expected.x);
		EXPECT_EQ(offset_cap->uvs[index].y, expected.y);
	}
	const Face* source_cap = find_face(fixture.document, fixture.face);
	ASSERT_NE(source_cap, nullptr);
	ASSERT_EQ(source_cap->uvs.size(), source_uvs.size());
	for (std::size_t index = 0U; index < source_vertices.size(); ++index) {
		const auto retained = std::ranges::find(source_cap->vertices, source_vertices[index]);
		ASSERT_NE(retained, source_cap->vertices.end());
		const auto corner = static_cast<std::size_t>(retained - source_cap->vertices.begin());
		EXPECT_EQ(source_cap->uvs[corner].x, source_uvs[index].x);
		EXPECT_EQ(source_cap->uvs[corner].y, source_uvs[index].y);
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 6U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_face == fixture.face);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationThickenFacesTests, NegativeThickenGrowsFromTheFixedSourceCapAndWindsInward) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	const ElementId original_face_id = selection.active_face;
	const Face* source_before = find_face(fixture.document, original_face_id);
	EXPECT_TRUE(source_before != nullptr);
	const quader::QVec3 source_normal = face_normal(fixture.document, *source_before);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, -0.5F, false);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 6U);
	EXPECT_TRUE(selection.has_active);
        EXPECT_TRUE(selection.active_kind == ElementKind::Face);
        EXPECT_TRUE(selection.active_face == original_face_id);
	const Face* source_cap = find_face(fixture.document, original_face_id);
	const auto offset_cap = std::find_if(fixture.document.faces.begin(), fixture.document.faces.end(), [&fixture](const Face& face) {
		return qdr_test::face_centroid(fixture.document, face).z > 0.49F;
	});
	EXPECT_TRUE(source_cap != nullptr);
	EXPECT_TRUE(offset_cap != fixture.document.faces.end());
	EXPECT_TRUE(dot(face_normal(fixture.document, *source_cap), source_normal) > 0.5F);
	EXPECT_TRUE(dot(face_normal(fixture.document, *offset_cap), source_normal) < -0.5F);
	EXPECT_TRUE(qdr_test::face_centroid(fixture.document, *source_cap).z < 0.01F);
	EXPECT_TRUE(qdr_test::face_centroid(fixture.document, *offset_cap).z > 0.49F);
	const auto front_side_face = std::find_if(fixture.document.faces.begin(), fixture.document.faces.end(), [&fixture](const Face& face) {
		const quader::QVec3 center = qdr_test::face_centroid(fixture.document, face);
		return center.y < -0.9F && center.z > 0.01F && center.z < 0.49F;
	});
	EXPECT_TRUE(front_side_face != fixture.document.faces.end());
	EXPECT_TRUE(dot(face_normal(fixture.document, *front_side_face), { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationThickenFacesTests, NegativeThickenIgnoresLegacyFromCenterMovement) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, -0.5F, true);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	const auto minmax = std::minmax_element(fixture.document.vertices.begin(), fixture.document.vertices.end(), [](const Vertex& left, const Vertex& right) {
		return left.position.z < right.position.z;
	});
	EXPECT_TRUE(minmax.first != fixture.document.vertices.end());
	EXPECT_TRUE(minmax.first->position.z < 0.01F);
	EXPECT_TRUE(minmax.second->position.z > 0.49F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
}

TEST(QdrDocumentOperationThickenFacesTests, ThickenCopiesFaceShadingAndExactModesToTheOffsetCap) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face& source = fixture.document.faces.front();
	source.normal_shading = SurfaceShading::Smooth;
	const std::vector<ElementId> source_vertices = source.vertices;
	const Edge hard_source = make_edge(source_vertices[0], source_vertices[1]);
	const Edge soft_source = make_edge(source_vertices[2], source_vertices[3]);
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, hard_source, EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, soft_source, EdgeNormalMode::Soft));
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(
		fixture.document, selection, 0.5F, false);

	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_FALSE(result.created.faces.empty());
	const Face* offset_cap = find_face(fixture.document, result.created.faces.front());
	ASSERT_NE(offset_cap, nullptr);
	EXPECT_EQ(offset_cap->normal_shading, SurfaceShading::Smooth);
	auto offset_vertex_for = [&](ElementId source_id) {
		const Vertex* source_vertex = find_vertex(fixture.document, source_id);
		if (source_vertex == nullptr) {
			return kInvalidElementId;
		}
		const auto found = std::ranges::find_if(
			fixture.document.vertices,
			[&](const Vertex& candidate) {
				return candidate.id != source_id &&
					qdr_test::vec_nearly_equal(
						candidate.position,
						source_vertex->position + quader::QVec3 { 0.0F, 0.0F, -0.5F });
			});
		return found == fixture.document.vertices.end() ? kInvalidElementId : found->id;
	};
	const Edge hard_copy = make_edge(
		offset_vertex_for(source_vertices[0]), offset_vertex_for(source_vertices[1]));
	const Edge soft_copy = make_edge(
		offset_vertex_for(source_vertices[2]), offset_vertex_for(source_vertices[3]));
	ASSERT_NE(hard_copy.a, kInvalidElementId);
	ASSERT_NE(soft_copy.a, kInvalidElementId);
	EXPECT_EQ(qdr_test::edge_normal_mode(fixture.document, hard_copy), EdgeNormalMode::Hard);
	EXPECT_EQ(qdr_test::edge_normal_mode(fixture.document, soft_copy), EdgeNormalMode::Soft);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(
			fixture.document,
			make_edge(source_vertices[0], offset_vertex_for(source_vertices[0]))),
		EdgeNormalMode::Default);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}
