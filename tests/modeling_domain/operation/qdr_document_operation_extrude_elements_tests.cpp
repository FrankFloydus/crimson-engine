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
