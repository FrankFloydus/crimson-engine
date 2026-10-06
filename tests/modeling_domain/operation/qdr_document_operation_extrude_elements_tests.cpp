////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>

using namespace quader_poly;

namespace {

/**
 * Stores the Circular Boundary Fixture data contract used by the automated test suite.
 */
struct CircularBoundaryFixture {
	Document document;
	std::array<Edge, 8> inner_edges {};
};

CircularBoundaryFixture make_circular_boundary_fixture()
{
	CircularBoundaryFixture fixture;
	std::array<ElementId, 8> inner {};
	std::array<ElementId, 8> outer {};
	const std::array<quader::QVec3, 8> directions {
		quader::QVec3 { -0.70710677F, 0.0F, -0.70710677F },
		quader::QVec3 { 0.0F, 0.0F, -1.0F },
		quader::QVec3 { 0.70710677F, 0.0F, -0.70710677F },
		quader::QVec3 { 1.0F, 0.0F, 0.0F },
		quader::QVec3 { 0.70710677F, 0.0F, 0.70710677F },
		quader::QVec3 { 0.0F, 0.0F, 1.0F },
		quader::QVec3 { -0.70710677F, 0.0F, 0.70710677F },
		quader::QVec3 { -1.0F, 0.0F, 0.0F },
	};

	for (std::size_t index = 0; index < directions.size(); ++index) {
		inner[index] = add_vertex(fixture.document, directions[index] * 0.5F);
		outer[index] = add_vertex(fixture.document, directions[index] * 0.75F);
	}

	for (std::size_t index = 0; index < directions.size(); ++index) {
		const std::size_t next = (index + 1U) % directions.size();
                EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                           inner[index],
                                                           inner[next],
                                                           outer[next],
                                                           outer[index],
                                                       }) != kInvalidElementId);
                fixture.inner_edges[index] = make_edge(inner[index], inner[next]);
	}

	for (const Face& face : fixture.document.faces) {
		EXPECT_TRUE(dot(face_normal(fixture.document, face), { 0.0F, 1.0F, 0.0F }) > 0.5F);
	}
	return fixture;
}

bool vertical_wall_normals_have_consistent_radial_direction(const Document& document)
{
	std::optional<float> expected_sign;
	for (const Face& face : document.faces) {
		if (!qdr_test::face_has_vertical_span(document, face)) {
			continue;
		}

		const quader::QVec3 normal = face_normal(document, face);
		const quader::QVec3 center = qdr_test::face_centroid(document, face);
		const float radial_dot = dot(normal, { center.x, 0.0F, center.z });
		if (std::abs(radial_dot) <= 0.001F) {
			return false;
		}
		const float sign = radial_dot > 0.0F ? 1.0F : -1.0F;
		if (!expected_sign.has_value()) {
			expected_sign = sign;
		} else if (*expected_sign != sign) {
			return false;
		}
	}
	return expected_sign.has_value();
}

} // namespace

TEST(QdrDocumentOperationExtrudeElementsTests, ExtrudeSelectedEdgeElementsCreatesSideGeometryAndSelectsTheOffsetEdge) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge source = make_edge(fixture.bottom_left, fixture.bottom_right);
	Selection selection = qdr_test::edge_selection({ source }, source);

	const OperationResult result = extrude_selected_elements(fixture.document, selection, { 0.0F, -0.5F, 0.0F });

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationExtrudeElementsTests, TransformExtrudeSelectedFaceCopiesCapThroughSuppliedTransform) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{-2.0F, 0.5F}, {0.25F, 0.75F}, 40.0F};
	fixture.document.faces.front().texture_transform = texture;
    const std::vector<quader::QVec2> authored_uvs{{2.0F, 3.0F}, {6.0F, 4.0F}, {9.0F, 10.0F}, {1.0F, 8.0F}};
    fixture.document.faces.front().uvs = authored_uvs;
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	Transform3 transform;
	transform.x_axis = { 0.0F, 1.0F, 0.0F };
	transform.y_axis = { -1.0F, 0.0F, 0.0F };
	transform.origin = { 0.0F, 0.0F, 1.0F };

	const OperationResult result =
		transform_extrude_selected_elements(fixture.document, selection, transform, {});
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(selection.mode == SelectionMode::Face);
	EXPECT_TRUE(selection.faces.size() == 1U);
	EXPECT_TRUE(selection.faces.front() == fixture.face);
	EXPECT_TRUE(fixture.document.vertices.size() == 8U);
	EXPECT_TRUE(fixture.document.faces.size() == 5U);
	const Face* cap = find_face(fixture.document, fixture.face);
	ASSERT_NE(cap, nullptr);
	ASSERT_EQ(cap->vertices.size(), 4U);
    ASSERT_EQ(cap->uvs.size(), authored_uvs.size());
    for (std::size_t corner = 0; corner < authored_uvs.size(); ++corner) {
        EXPECT_FLOAT_EQ(cap->uvs[corner].x, authored_uvs[corner].x);
        EXPECT_FLOAT_EQ(cap->uvs[corner].y, authored_uvs[corner].y);
    }
	EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, cap->vertices[0])->position, { 1.0F, -1.0F, 1.0F }));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, cap->vertices[1])->position, { 1.0F, 1.0F, 1.0F }));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, cap->vertices[2])->position, { -1.0F, 1.0F, 1.0F }));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, cap->vertices[3])->position, { -1.0F, -1.0F, 1.0F }));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	EXPECT_EQ(qdr_test::open_edge_count(fixture.document), 4U);
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
}

TEST(QdrDocumentOperationExtrudeElementsTests, EdgeExtrudeCopiesExactModeAndDefaultsItsConnectors) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge source = make_edge(fixture.bottom_left, fixture.bottom_right);
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, source, EdgeNormalMode::Soft));
	Selection selection = qdr_test::edge_selection({ source }, source);

	const OperationResult result = extrude_selected_elements(
		fixture.document, selection, { 0.0F, -0.5F, 0.0F });

	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_EQ(selection.edges.size(), 1U);
	const Edge copied = selection.edges.front();
	EXPECT_EQ(qdr_test::edge_normal_mode(fixture.document, source), EdgeNormalMode::Soft);
	EXPECT_EQ(qdr_test::edge_normal_mode(fixture.document, copied), EdgeNormalMode::Soft);
	const Face& strip = fixture.document.faces.back();
	for (const Edge edge : qdr_test::face_edges_for_test(strip)) {
		if (edge == source || edge == copied) {
			continue;
		}
		EXPECT_EQ(qdr_test::edge_normal_mode(fixture.document, edge), EdgeNormalMode::Default);
	}
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationExtrudeElementsTests, ClosedEdgeLedgeCopiesSelectedAndClippedCapModes) {
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const ElementId bottom_middle = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId top_middle = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	ASSERT_NE(add_face(document, std::array { bottom_left, bottom_middle, top_middle, top_left }), kInvalidElementId);
	ASSERT_NE(add_face(document, std::array { bottom_middle, bottom_right, top_right, top_middle }), kInvalidElementId);
	const Edge source = make_edge(bottom_middle, top_middle);
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, source, EdgeNormalMode::Hard));
	for (const Edge edge : document_edges(document)) {
		if (edge != source) {
			ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, edge, EdgeNormalMode::Soft));
		}
	}
	const ElementId last_source_vertex_id = document.next_vertex_id - 1U;
	const quader::QVec3 source_a_position = find_vertex(document, source.a)->position;
	const quader::QVec3 source_b_position = find_vertex(document, source.b)->position;
	Selection selection = qdr_test::edge_selection({ source }, source);

	const OperationResult result = extrude_selected_elements(
		document, selection, { 0.0F, 0.0F, 1.0F }, 0.25F);

	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_EQ(selection.edges.size(), 1U);
	EXPECT_EQ(qdr_test::edge_normal_mode(document, selection.edges.front()), EdgeNormalMode::Hard);
	auto new_vertex_at = [&](quader::QVec3 position) {
		const auto found = std::ranges::find_if(document.vertices, [&](const Vertex& vertex) {
			return vertex.id > last_source_vertex_id &&
				qdr_test::vec_nearly_equal(vertex.position, position);
		});
		return found == document.vertices.end() ? kInvalidElementId : found->id;
	};
	const ElementId root_a = new_vertex_at(source_a_position);
	const ElementId root_b = new_vertex_at(source_b_position);
	ASSERT_NE(root_a, kInvalidElementId);
	ASSERT_NE(root_b, kInvalidElementId);
	const Edge root_edge = make_edge(root_a, root_b);
	EXPECT_EQ(qdr_test::edge_normal_mode(document, root_edge), EdgeNormalMode::Hard);
	for (const ElementId root : { root_a, root_b }) {
		bool found_clipped_cap_edge = false;
		for (const Edge edge : document_edges(document)) {
			if ((edge.a != root && edge.b != root) || edge == root_edge) {
				continue;
			}
			const ElementId other_id = edge.a == root ? edge.b : edge.a;
			const Vertex* other = find_vertex(document, other_id);
			if (other == nullptr || other_id <= last_source_vertex_id ||
				!qdr_test::nearly_equal(other->position.z, 0.0F)) {
				continue;
			}
			found_clipped_cap_edge = true;
			EXPECT_EQ(qdr_test::edge_normal_mode(document, edge), EdgeNormalMode::Soft);
		}
		EXPECT_TRUE(found_clipped_cap_edge);
	}
	qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationExtrudeElementsTests, ExtrudeSelectedStandaloneFaceReplacesSourceAndLeavesBaseOpen) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(7U);
	Face* original_face = find_face(fixture.document, fixture.face);
	ASSERT_NE(original_face, nullptr);
	original_face->normal_shading = SurfaceShading::Smooth;
	original_face->uvs = {
		{ 0.0F, 0.0F },
		{ 1.0F, 0.0F },
		{ 1.0F, 1.0F },
		{ 0.0F, 1.0F },
	};
	const Edge hard_source = make_edge(fixture.bottom_left, fixture.bottom_right);
	const Edge soft_source = make_edge(fixture.top_right, fixture.top_left);
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, hard_source, EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(fixture.document, soft_source, EdgeNormalMode::Soft));
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result =
		extrude_selected_elements(fixture.document, selection, { 0.0F, 0.0F, 2.0F });

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(selection.mode == SelectionMode::Face);
	EXPECT_TRUE(selection.faces == std::vector<ElementId> { fixture.face });
	EXPECT_TRUE(fixture.document.vertices.size() == 8U);
	EXPECT_TRUE(fixture.document.faces.size() == 5U);
	EXPECT_EQ(qdr_test::open_edge_count(fixture.document), 4U);
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));

	const Face* moved_cap = find_face(fixture.document, fixture.face);
	ASSERT_NE(moved_cap, nullptr);
	EXPECT_EQ(moved_cap->material_slot, 7U);
	EXPECT_EQ(moved_cap->normal_shading, SurfaceShading::Smooth);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(
			fixture.document,
			make_edge(moved_cap->vertices[0], moved_cap->vertices[1])),
		EdgeNormalMode::Hard);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(
			fixture.document,
			make_edge(moved_cap->vertices[2], moved_cap->vertices[3])),
		EdgeNormalMode::Soft);
    const std::vector<quader::QVec2> expected_moved_uvs{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    ASSERT_EQ(moved_cap->uvs.size(), expected_moved_uvs.size());
    for (std::size_t corner = 0; corner < expected_moved_uvs.size(); ++corner) {
        EXPECT_FLOAT_EQ(moved_cap->uvs[corner].x, expected_moved_uvs[corner].x);
        EXPECT_FLOAT_EQ(moved_cap->uvs[corner].y, expected_moved_uvs[corner].y);
    }
	EXPECT_TRUE(std::ranges::all_of(moved_cap->vertices, [&fixture](ElementId vertex_id) {
		const Vertex* vertex = find_vertex(fixture.document, vertex_id);
		return vertex != nullptr && qdr_test::nearly_equal(vertex->position.z, 2.0F);
	}));

	const auto source_cap = std::ranges::find_if(fixture.document.faces, [&fixture](const Face& face) {
		return face.id != fixture.face && face.vertices == std::vector<ElementId> {
			fixture.top_left,
			fixture.top_right,
			fixture.bottom_right,
			fixture.bottom_left,
		};
	});
	EXPECT_EQ(source_cap, fixture.document.faces.end());
}

TEST(QdrDocumentOperationExtrudeElementsTests, ExtrudeSelectedClosedMeshFaceDoesNotAddOverlappingSourceCap) {
	Document document = make_cube({});
	ASSERT_EQ(document.faces.size(), 6U);
	const ElementId selected_face_id = document.faces.front().id;
	const quader::QVec3 offset = face_normal(document, document.faces.front());
	Selection selection = qdr_test::face_selection({ selected_face_id }, selected_face_id);

	const OperationResult result = extrude_selected_elements(document, selection, offset);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(selection.faces == std::vector<ElementId> { selected_face_id });
	EXPECT_TRUE(document.vertices.size() == 12U);
	EXPECT_TRUE(document.faces.size() == 10U);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
}

TEST(QdrDocumentOperationExtrudeElementsTests, OpenInsetPanelExtrusionReplacesSourceAndKeepsOnlyBoundaryRoots) {
    for (const bool use_transform : {false, true}) {
        SCOPED_TRACE(use_transform);
        Document document;
        const std::array outer{
            add_vertex(document, {0.0F, -5.5F, -5.5F}),
            add_vertex(document, {0.0F, -5.5F, 5.5F}),
            add_vertex(document, {0.0F, 5.5F, 5.5F}),
            add_vertex(document, {0.0F, 5.5F, -5.5F}),
        };
        const std::array inner{
            add_vertex(document, {-0.5F, -4.5F, -4.5F}),
            add_vertex(document, {-0.5F, -4.5F, 4.5F}),
            add_vertex(document, {-0.5F, 4.5F, 4.5F}),
            add_vertex(document, {-0.5F, 4.5F, -4.5F}),
        };
        Selection selection;
        selection.mode = SelectionMode::Face;
        selection.faces.push_back(add_face(document, inner));
        for (std::size_t index = 0; index < outer.size(); ++index) {
            const auto next = (index + 1U) % outer.size();
            selection.faces.push_back(add_face(document,
                std::array{outer[index], outer[next], inner[next], inner[index]}));
        }
        selection.has_active = true;
        selection.active_kind = ElementKind::Face;
        selection.active_face = selection.faces.front();
        const auto selected_faces = selection.faces;
        const auto loose_vertex = add_vertex(document, {100.0F, 200.0F, 300.0F});
        const Document before = document;
        Transform3 transform;
        transform.x_axis = {0.0F, 0.0F, -1.0F};
        transform.z_axis = {1.0F, 0.0F, 0.0F};
        transform.origin = {0.0F, 0.0F, 11.0F};

        const auto result = use_transform
            ? transform_extrude_selected_elements(document, selection, transform, {})
            : extrude_selected_elements(document, selection, {0.0F, 0.0F, 11.0F});

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        EXPECT_EQ(document.vertices.size(), 13U);
        EXPECT_EQ(document.faces.size(), 9U);
        EXPECT_EQ(selection.faces, selected_faces);
        EXPECT_EQ(selection.active_face, selected_faces.front());
        EXPECT_NE(find_vertex(document, loose_vertex), nullptr);
        for (const auto id : outer) {
            EXPECT_NE(find_vertex(document, id), nullptr);
        }
        for (const auto id : inner) {
            EXPECT_EQ(find_vertex(document, id), nullptr);
        }
        for (const auto id : selected_faces) {
            const Face* source = find_face(before, id);
            const Face* cap = find_face(document, id);
            ASSERT_NE(source, nullptr);
            ASSERT_NE(cap, nullptr);
            ASSERT_EQ(cap->vertices.size(), source->vertices.size());
            for (std::size_t corner = 0; corner < cap->vertices.size(); ++corner) {
                const auto source_position = find_vertex(before, source->vertices[corner])->position;
                const auto expected = use_transform
                    ? quader::QVec3{source_position.z, source_position.y, 11.0F - source_position.x}
                    : source_position + quader::QVec3{0.0F, 0.0F, 11.0F};
                const Vertex* moved = find_vertex(document, cap->vertices[corner]);
                ASSERT_NE(moved, nullptr);
                EXPECT_TRUE(qdr_test::vec_nearly_equal(moved->position, expected));
                EXPECT_EQ(find_vertex(before, moved->id), nullptr);
            }
        }
        for (const Face& face : document.faces) {
            EXPECT_FALSE(std::ranges::all_of(face.vertices, [&](ElementId id) {
                return find_vertex(before, id) != nullptr;
            }));
        }
        EXPECT_EQ(qdr_test::open_edge_count(document), 4U);
        EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
        const auto connected = connected_component_selection(document,
            PickResult{.hit = true, .kind = ElementKind::Face, .face_id = selected_faces.front()});
        EXPECT_EQ(connected.faces.size(), 9U);
        EXPECT_TRUE(validate_polygon_document(
            document, PolygonDocumentValidationPolicy::AuthoredTopology).ok());
        if (!use_transform) {
            // Sideways movement leaves two collapsed walls in authored topology for diagnostics.
            std::size_t collapsed_walls = 0U;
            for (const auto& diagnostic : validate_polygon_document(document).diagnostics) {
                if (diagnostic.code == PolygonDocumentDiagnosticCode::TriangulationFailure) {
                    EXPECT_EQ(std::ranges::find(selected_faces, diagnostic.face_id), selected_faces.end());
                    ++collapsed_walls;
                }
            }
            EXPECT_EQ(collapsed_walls, 2U);
        }
    }
}

TEST(QdrDocumentOperationExtrudeElementsTests, ExtrudeSelectedCircularBoundaryEdgesKeepsWallNormalsAndWindingCoherent) {
	for (const quader::QVec3 offset : { quader::QVec3 { 0.0F, 0.5F, 0.0F }, quader::QVec3 { 0.0F, -0.5F, 0.0F } }) {
		CircularBoundaryFixture fixture = make_circular_boundary_fixture();
		Selection selection = qdr_test::edge_selection({
			fixture.inner_edges[0],
			fixture.inner_edges[1],
			fixture.inner_edges[2],
			fixture.inner_edges[3],
			fixture.inner_edges[4],
			fixture.inner_edges[5],
			fixture.inner_edges[6],
			fixture.inner_edges[7],
		}, fixture.inner_edges.back());

		const OperationResult result = extrude_selected_elements(fixture.document, selection, offset);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
                EXPECT_TRUE(selection.mode == SelectionMode::Edge);
                EXPECT_TRUE(selection.edges.size() == fixture.inner_edges.size());
		EXPECT_TRUE(fixture.document.faces.size() == 16U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
		EXPECT_TRUE(vertical_wall_normals_have_consistent_radial_direction(fixture.document));
		EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
	}
}
