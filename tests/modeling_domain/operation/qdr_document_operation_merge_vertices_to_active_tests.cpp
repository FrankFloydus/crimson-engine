////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace quader_poly;

namespace {

Document make_boundary_arc_merge_document(std::size_t arc_count) {
    Document document;
    constexpr std::array kPositions{
        quader::QVec3{0.0F, 0.0F, 0.0F},
        quader::QVec3{2.0F, 0.0F, 0.0F},
        quader::QVec3{1.0F, 1.0F, 0.0F},
        quader::QVec3{0.0F, 0.0F, 0.0F},
        quader::QVec3{-1.0F, 1.0F, 0.0F},
        quader::QVec3{-2.0F, 0.0F, 0.0F},
        quader::QVec3{0.0F, 0.0F, 0.0F},
        quader::QVec3{-1.0F, -1.0F, 0.0F},
        quader::QVec3{1.0F, -1.0F, 0.0F}};
    std::vector<ElementId> boundary;
    for (std::size_t index = 0; index < arc_count * 3U; ++index) {
        boundary.push_back(add_vertex(document, kPositions[index]));
    }
    const ElementId face_id = add_face(document, boundary, 17U);
    Face& face = *find_face(document, face_id);
    face.normal_shading = SurfaceShading::Smooth;
    face.texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const float value = static_cast<float>(index);
        face.uvs.push_back({value + 0.25F, -value - 0.5F});
    }
    document.next_face_id = 100U;
    return document;
}

void expect_boundary_arc_face(const Face& face, const Face& source,
                             ElementId face_id,
                             const std::vector<ElementId>& vertices,
                             const std::vector<std::size_t>& uv_corners) {
    EXPECT_EQ(face.id, face_id);
    EXPECT_EQ(face.vertices, vertices);
    EXPECT_EQ(face.material_slot, source.material_slot);
    EXPECT_EQ(face.normal_shading, source.normal_shading);
    EXPECT_EQ(face.texture_transform.scale.x, source.texture_transform.scale.x);
    EXPECT_EQ(face.texture_transform.scale.y, source.texture_transform.scale.y);
    EXPECT_EQ(face.texture_transform.shift_uv.x, source.texture_transform.shift_uv.x);
    EXPECT_EQ(face.texture_transform.shift_uv.y, source.texture_transform.shift_uv.y);
    EXPECT_EQ(face.texture_transform.rotation_degrees,
              source.texture_transform.rotation_degrees);
    ASSERT_EQ(face.uvs.size(), uv_corners.size());
    for (std::size_t index = 0; index < uv_corners.size(); ++index) {
        EXPECT_EQ(face.uvs[index].x, source.uvs[uv_corners[index]].x);
        EXPECT_EQ(face.uvs[index].y, source.uvs[uv_corners[index]].y);
    }
}

} // namespace

TEST(QdrDocumentOperationMergeVerticesToActiveTests,
     BoundaryArcRepairPreservesOrderedFacesAndCornerDonors) {
    for (std::size_t rotation = 0; rotation < 6U; ++rotation) {
        SCOPED_TRACE(rotation);
        Document document = make_boundary_arc_merge_document(2U);
        const Face source = document.faces.front();
        Face& rotated = document.faces.front();
        std::rotate(rotated.vertices.begin(), rotated.vertices.begin() + rotation,
                    rotated.vertices.end());
        std::rotate(rotated.uvs.begin(), rotated.uvs.begin() + rotation,
                    rotated.uvs.end());
        document.hard_normal_edges = {make_edge(4U, 5U)};
        document.soft_normal_edges = {make_edge(3U, 4U)};
        Selection selection = qdr_test::vertex_selection({4U, 1U}, 1U);

        const OperationResult result =
            merge_selected_vertices_to_active(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(document.faces.size(), 2U);
        expect_boundary_arc_face(document.faces[0], source, source.id,
                                 {1U, 5U, 6U}, {3U, 4U, 5U});
        expect_boundary_arc_face(document.faces[1], source, 100U,
                                 {1U, 2U, 3U}, {0U, 1U, 2U});
        EXPECT_EQ(document.next_face_id, 101U);
        EXPECT_EQ(document.next_vertex_id, 7U);
        EXPECT_EQ(selection.vertices, (std::vector<ElementId>{1U}));
        EXPECT_TRUE(selection.has_active);
        EXPECT_EQ(selection.active_vertex, 1U);
        EXPECT_EQ(qdr_test::edge_normal_mode(document, make_edge(1U, 5U)),
                  EdgeNormalMode::Hard);
        EXPECT_EQ(qdr_test::edge_normal_mode(document, make_edge(1U, 3U)),
                  EdgeNormalMode::Soft);
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(3U, 5U)));
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(6U, 2U)));
        qdr_test::expect_valid_polygon_document_after_success(document, result);
    }
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests,
     BoundaryArcRepairKeepsMultipleSourceOrderingUnderRigidTransformsAndStoragePermutation) {
    for (const bool permuted : {false, true}) {
        SCOPED_TRACE(permuted);
        Document document = make_boundary_arc_merge_document(3U);
        const Face source = document.faces.front();
        if (permuted) {
            std::reverse(document.vertices.begin(), document.vertices.end());
            for (Vertex& vertex : document.vertices) {
                const auto position = vertex.position;
                vertex.position = {11.0F + position.z * 2.0F,
                                   -7.0F + position.x * 2.0F,
                                   5.0F + position.y * 2.0F};
            }
        }
        Selection selection = qdr_test::vertex_selection({7U, 1U, 4U}, 1U);

        const OperationResult result =
            merge_selected_vertices_to_active(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(document.faces.size(), 3U);
        expect_boundary_arc_face(document.faces[0], source, source.id,
                                 {1U, 8U, 9U}, {6U, 7U, 8U});
        expect_boundary_arc_face(document.faces[1], source, 100U,
                                 {1U, 5U, 6U}, {0U, 4U, 5U});
        expect_boundary_arc_face(document.faces[2], source, 101U,
                                 {1U, 2U, 3U}, {0U, 1U, 2U});
        EXPECT_EQ(document.next_face_id, 102U);
        EXPECT_EQ(document.next_vertex_id, 10U);
        EXPECT_EQ(selection.vertices, (std::vector<ElementId>{1U}));
        EXPECT_EQ(selection.active_vertex, 1U);
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(3U, 5U)));
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(6U, 8U)));
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(9U, 2U)));
        qdr_test::expect_valid_polygon_document_after_success(document, result);
    }
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests,
     BoundaryArcRepairRetainsExternalSurvivorRepeatedCornerPolicy) {
    Document document = make_boundary_arc_merge_document(2U);
    const ElementId external_survivor = add_vertex(document, {});
    Selection selection = qdr_test::vertex_selection(
        {1U, 4U, external_survivor}, external_survivor);

    const OperationResult result =
        merge_selected_vertices_to_active(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_TRUE(document.faces.empty());
    EXPECT_EQ(document.vertices.size(), 5U);
    EXPECT_NE(find_vertex(document, external_survivor), nullptr);
    EXPECT_EQ(find_vertex(document, 1U), nullptr);
    EXPECT_EQ(find_vertex(document, 4U), nullptr);
    EXPECT_EQ(document.next_face_id, 100U);
    EXPECT_EQ(selection.vertices, (std::vector<ElementId>{external_survivor}));
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_vertex, external_survivor);
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests,
     PreservesUnrelatedCollapsedFacesAndLooseVertices) {
    auto fixture = qdr_test::make_quad_document();
    const Document island =
        qdr_test::append_unrelated_authored_geometry(fixture.document);
    const auto vertex_count_before = fixture.document.vertices.size();
    const auto face_count_before = fixture.document.faces.size();
    const auto next_vertex_id_before = fixture.document.next_vertex_id;
    const auto next_face_id_before = fixture.document.next_face_id;
    Selection selection = qdr_test::vertex_selection(
        {fixture.bottom_left, fixture.bottom_right}, fixture.bottom_right);

    const auto result =
        merge_selected_vertices_to_active(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(fixture.document.vertices.size(), vertex_count_before - 1U);
    EXPECT_EQ(fixture.document.faces.size(), face_count_before);
    EXPECT_EQ(find_vertex(fixture.document, fixture.bottom_left), nullptr);
    const Vertex* survivor = find_vertex(fixture.document, fixture.bottom_right);
    ASSERT_NE(survivor, nullptr);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, {1.0F, -1.0F, 0.0F}));
    const Face* face = find_face(fixture.document, fixture.face);
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(face->vertices, (std::vector<ElementId>{
        fixture.bottom_right, fixture.top_right, fixture.top_left}));
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(fixture.document, *face));
    EXPECT_EQ(selection.mode, SelectionMode::Vertex);
    EXPECT_EQ(selection.vertices, (std::vector<ElementId>{fixture.bottom_right}));
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, ElementKind::Vertex);
    EXPECT_EQ(selection.active_vertex, fixture.bottom_right);
    EXPECT_EQ(fixture.document.next_vertex_id, next_vertex_id_before);
    EXPECT_EQ(fixture.document.next_face_id, next_face_id_before);
    qdr_test::expect_unrelated_authored_geometry_preserved(fixture.document, island);
    EXPECT_TRUE(validate_polygon_document(
        fixture.document, PolygonDocumentValidationPolicy::AuthoredTopology).ok());
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests,
     RejectsMalformedUnrelatedAuthoredDataAtomically) {
    for (const auto fault : qdr_test::kAuthoredInputFaults) {
        SCOPED_TRACE(static_cast<int>(fault));
        auto fixture = qdr_test::make_quad_document();
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
        qdr_test::corrupt_unrelated_authored_geometry(fixture.document, fault);
        Selection selection = qdr_test::vertex_selection(
            {fixture.bottom_left, fixture.bottom_right}, fixture.bottom_right);
        const auto source = qdr_test::document_fingerprint(fixture.document);
        const auto source_selection = qdr_test::selection_fingerprint(selection);

        const auto result =
            merge_selected_vertices_to_active(fixture.document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
    }
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests, MergeVerticesToActivePreservesActiveVertexAndRewiresFaces) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right }, fixture.bottom_left);

	const OperationResult result = merge_selected_vertices_to_active(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(find_vertex(fixture.document, fixture.bottom_left) != nullptr);
	EXPECT_TRUE(find_vertex(fixture.document, fixture.bottom_right) == nullptr);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(selection.vertices.front() == fixture.bottom_left);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests,
     MergeVerticesToActiveTransfersExplicitSoftEdgeState) {
    Document document;
    const ElementId active = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId removed = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId top = add_vertex(document, {0.0F, 1.0F, 0.0F});
    ASSERT_NE(add_face(document, std::array{removed, right, top}),
              kInvalidElementId);
    document.soft_normal_edges = {make_edge(removed, right)};
    Selection selection = qdr_test::vertex_selection({active, removed}, active);

    const OperationResult result =
        merge_selected_vertices_to_active(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(document, removed), nullptr);
    const Edge replacement = make_edge(active, right);
    EXPECT_TRUE(qdr_test::document_has_edge(document, replacement));
    EXPECT_TRUE(qdr_test::contains_edge(document.soft_normal_edges,
                                        replacement));
    EXPECT_FALSE(qdr_test::contains_edge(document.hard_normal_edges,
                                         replacement));
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests, MergeVerticesToActivePreservesCubeTopologyAndSlopedUVLoopsAdjacentCubeVerticesCollapseIntoTheActiveIdWithoutOpeningTheHull) {
	{
		Document document = make_cube({});
		const Edge edge = document_edges(document).front();
		const std::size_t vertex_count_before = document.vertices.size();
		Selection selection = qdr_test::vertex_selection({ edge.a, edge.b }, edge.a);

		const OperationResult result = merge_selected_vertices_to_active(document, selection);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(document.vertices.size() == vertex_count_before - 1U);
		EXPECT_TRUE(find_vertex(document, edge.a) != nullptr);
		EXPECT_TRUE(find_vertex(document, edge.b) == nullptr);
		EXPECT_TRUE(selection.has_active);
		EXPECT_TRUE(selection.vertices.size() == 1U);
		EXPECT_TRUE(selection.active_vertex == edge.a);
		EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	}

	

	
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests, MergeVerticesToActivePreservesCubeTopologyAndSlopedUVLoopsDiagonalSameFaceMergeBakesSourceAxisUVsOntoGeneratedSlopes) {
	

	{
		Document document = make_cube({});
		const Face source_face = document.faces.front();
		const ElementId active_vertex = source_face.vertices[0];
		const ElementId source_vertex = source_face.vertices[2];
		const std::size_t vertex_count_before = document.vertices.size();
		Selection selection = qdr_test::vertex_selection({ active_vertex, source_vertex }, active_vertex);

		const OperationResult result = merge_selected_vertices_to_active(document, selection);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(document.vertices.size() == vertex_count_before - 1U);
		EXPECT_TRUE(find_vertex(document, active_vertex) != nullptr);
		EXPECT_TRUE(find_vertex(document, source_vertex) == nullptr);
		EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));

		const CompiledMesh mesh = compile_document(document);
		std::size_t expected_compiled_vertices = 0;
		for (const Face& face : document.faces) {
			expected_compiled_vertices += face.vertices.size();
		}
		EXPECT_TRUE(mesh.vertices.size() == expected_compiled_vertices);

		bool checked_sloped_uv = false;
		std::size_t compiled_vertex_offset = 0;
		for (const Face& face : document.faces) {
			const quader::QVec3 normal = face_normal(document, face);
			if (std::abs(normal.y) > 0.25F && qdr_test::face_has_vertical_span(document, face)) {
				EXPECT_TRUE(face.uvs.size() == face.vertices.size());
				for (std::size_t index = 0; index < face.vertices.size(); ++index) {
					EXPECT_TRUE(qdr_test::vec2_nearly_equal(mesh.vertices[compiled_vertex_offset + index].uv0, face.uvs[index], 0.001F));
				}
				checked_sloped_uv = true;
				break;
			}
			compiled_vertex_offset += face.vertices.size();
		}
		EXPECT_TRUE(checked_sloped_uv);
	}

	
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests, MergeVerticesToActivePreservesCubeTopologyAndSlopedUVLoopsSequentialRampMergesKeepTheDiagonalSeamAndBakedSlopedUVs) {
	

	

	{
		Document document = make_cube({});
		const ElementId top_front_left = 4;
		const ElementId top_front_right = 3;
		const ElementId top_back_right = 7;
		const ElementId top_back_left = 6;

		Selection first = qdr_test::vertex_selection({ top_front_left, top_front_right }, top_front_right);
		EXPECT_TRUE(merge_selected_vertices_to_active(document, first).changed);
		qdr_test::expect_valid_polygon_document(document, "successful topology operation");
		EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(top_front_right, top_back_left)));

		Selection second = qdr_test::vertex_selection({ top_back_right, top_back_left }, top_back_left);
		EXPECT_TRUE(merge_selected_vertices_to_active(document, second).changed);
		qdr_test::expect_valid_polygon_document(document, "successful topology operation");
		EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(top_front_right, top_back_left)));

		bool checked_sloped_uv = false;
		for (const Face& face : document.faces) {
			const quader::QVec3 normal = face_normal(document, face);
			if (std::abs(normal.y) > 0.25F &&
				qdr_test::face_has_vertical_span(document, face) &&
				face.uvs.size() == face.vertices.size()) {
				checked_sloped_uv = true;
				break;
			}
		}
		EXPECT_TRUE(checked_sloped_uv);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	}
}

TEST(QdrDocumentOperationMergeVerticesToActiveTests, MergeVerticesToActiveCollapsesOppositeTopCubeCornersToTheActiveCorner) {
	Document document = make_cube({});
	const ElementId bottom_front_left = 1;
	const ElementId bottom_front_right = 2;
	const ElementId top_front_right = 3;
	const ElementId top_front_left = 4;
	const ElementId bottom_back_left = 5;
	const ElementId top_back_left = 6;
	const ElementId top_back_right = 7;
	const ElementId bottom_back_right = 8;

	Selection selection = qdr_test::vertex_selection({ top_front_left, top_back_right }, top_back_right);

	const OperationResult result = merge_selected_vertices_to_active(document, selection);

	const auto has_face = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return std::any_of(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return face.vertices.size() == expected.size() && qdr_test::face_contains_all_vertices(face, expected);
		});
	};
	const auto has_cyclic_face = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return std::any_of(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return qdr_test::face_vertices_match_cyclic(face, expected);
		});
	};
	const auto face_count = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return static_cast<std::size_t>(std::count_if(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return face.vertices.size() == expected.size() && qdr_test::face_contains_all_vertices(face, expected);
		}));
	};
	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(find_vertex(document, top_front_left) == nullptr);
	const Vertex* active = find_vertex(document, top_back_right);
	EXPECT_TRUE(active != nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(active->position, { 1.0F, 1.0F, -1.0F }));
	EXPECT_FALSE(has_face({ bottom_back_left, bottom_front_left, bottom_front_right, top_front_right, top_back_right, top_back_left }));
	EXPECT_TRUE(has_cyclic_face({ bottom_front_left, bottom_front_right, top_front_right, top_back_right }));
	EXPECT_TRUE(has_cyclic_face({ bottom_back_left, bottom_front_left, top_back_right, top_back_left }));
	EXPECT_TRUE(has_cyclic_face({ bottom_back_right, top_back_right, top_front_right, bottom_front_right }));
	EXPECT_TRUE(has_cyclic_face({ bottom_back_left, top_back_left, top_back_right, bottom_back_right }));
	EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(top_back_left, top_front_right)));
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(bottom_front_left, top_back_right)) == 2);
	EXPECT_TRUE(face_count({ top_back_left, top_front_right, top_back_right }) == 0U);
	EXPECT_TRUE(document.faces.size() == 5U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(selection.active_vertex == top_back_right);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	const CompiledMesh mesh = compile_document(document);
	EXPECT_TRUE(mesh.primitives.size() == document.faces.size());
	EXPECT_TRUE(std::all_of(mesh.primitives.begin(), mesh.primitives.end(), [](const CompiledPrimitive& primitive) {
		return primitive.index_count == 6U;
	}));
}
