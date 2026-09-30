////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <cmath>

using namespace quader_poly;

namespace {

/**
 * Stores the Angled Gap Fixture data contract used by the automated test suite.
 */
struct AngledGapFixture {
	Document document;
	Edge left_edge;
	Edge right_edge;
};

/**
 * Stores the Crossed Gap Fixture data contract used by the automated test suite.
 */
struct CrossedGapFixture {
	Document document;
	Edge left_edge;
	Edge right_edge;
        ElementId left_front = kInvalidElementId;
        ElementId left_back = kInvalidElementId;
        ElementId right_front = kInvalidElementId;
        ElementId right_back = kInvalidElementId;
};

AngledGapFixture make_angled_gap_fixture()
{
	AngledGapFixture fixture;
	const ElementId left_front = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	const ElementId left_back = add_vertex(fixture.document, { 0.0F, 0.0F, 1.0F });
	const ElementId left_front_top = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId left_back_top = add_vertex(fixture.document, { 0.0F, 1.0F, 1.0F });
	const ElementId right_front = add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
	const ElementId right_back = add_vertex(fixture.document, { 1.5F, 0.0F, 0.5F });
	const ElementId right_front_top = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId right_back_top = add_vertex(fixture.document, { 1.5F, 1.0F, 0.5F });

	// Add the non-gap side first so document-order adjacent-face picking would
	// wind the new bridge downward across this angled gap.
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 left_front, left_back, left_back_top,
                                 left_front_top}) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 right_front, right_front_top, right_back_top,
                                 right_back}) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 left_front, left_front_top, left_back_top,
                                 left_back}) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 right_front, right_back, right_back_top,
                                 right_front_top}) != kInvalidElementId);

        fixture.left_edge = make_edge(left_front, left_back);
	fixture.right_edge = make_edge(right_front, right_back);
	return fixture;
}

CrossedGapFixture make_crossed_gap_fixture()
{
	CrossedGapFixture fixture;
	fixture.left_front = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	fixture.left_back = add_vertex(fixture.document, { 0.0F, 0.0F, 2.0F });
	const ElementId left_front_top = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId left_back_top = add_vertex(fixture.document, { 0.0F, 1.0F, 2.0F });
	fixture.right_front = add_vertex(fixture.document, { 4.0F, 0.0F, 0.0F });
	fixture.right_back = add_vertex(fixture.document, { 3.0F, 0.0F, 2.0F });
	const ElementId right_front_top = add_vertex(fixture.document, { 4.0F, 1.0F, 0.0F });
	const ElementId right_back_top = add_vertex(fixture.document, { 3.0F, 1.0F, 2.0F });
	const ElementId right_front_bottom = add_vertex(fixture.document, { 4.0F, -1.0F, 0.0F });
	const ElementId right_back_bottom = add_vertex(fixture.document, { 3.0F, -1.0F, 2.0F });

        EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                   fixture.left_back,
                                                   fixture.left_front,
                                                   left_front_top,
                                                   left_back_top,
                                               }) != kInvalidElementId);
        // Add this tied-scoring adjacent face first. A one-edge-at-a-time bridge
	// side pick would use it and cross the generated quad diagonally.
        EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                   fixture.right_back,
                                                   fixture.right_front,
                                                   right_front_bottom,
                                                   right_back_bottom,
                                               }) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                   fixture.right_front,
                                                   fixture.right_back,
                                                   right_back_top,
                                                   right_front_top,
                                               }) != kInvalidElementId);

        fixture.left_edge = make_edge(fixture.left_front, fixture.left_back);
	fixture.right_edge = make_edge(fixture.right_front, fixture.right_back);
	return fixture;
}

AngledGapFixture make_curved_gap_fixture(quader::QVec3 start, quader::QVec3 end,
                                       quader::QVec3 start_outward, quader::QVec3 end_outward) {
    AngledGapFixture fixture;
    const quader::QVec3 depth{0.0F, 0.0F, 1.0F};
    const ElementId first_front = add_vertex(fixture.document, start);
    const ElementId first_back = add_vertex(fixture.document, start + depth);
    const ElementId first_outer_front = add_vertex(fixture.document, start - start_outward);
    const ElementId first_outer_back = add_vertex(fixture.document, start - start_outward + depth);
    const ElementId second_front = add_vertex(fixture.document, end);
    const ElementId second_back = add_vertex(fixture.document, end + depth);
    const ElementId second_outer_front = add_vertex(fixture.document, end - end_outward);
    const ElementId second_outer_back = add_vertex(fixture.document, end - end_outward + depth);
    EXPECT_NE(add_face(fixture.document, std::array<ElementId, 4>{
        first_outer_front, first_front, first_back, first_outer_back}), kInvalidElementId);
    EXPECT_NE(add_face(fixture.document, std::array<ElementId, 4>{
        second_front, second_outer_front, second_outer_back, second_back}), kInvalidElementId);
    fixture.left_edge = make_edge(first_front, first_back);
    fixture.right_edge = make_edge(second_front, second_back);
    return fixture;
}

AngledGapFixture make_quarter_turn_gap_fixture(float reflection) {
    return make_curved_gap_fixture({}, {reflection, 1.0F, 0.0F},
        {reflection, 0.0F, 0.0F}, {0.0F, -1.0F, 0.0F});
}

} // namespace

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesCreatesASingleQuadBetweenTwoOpenEdges) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());
	Selection selection = qdr_test::edge_selection({
		make_edge(removed_face.vertices[0], removed_face.vertices[1]),
		make_edge(removed_face.vertices[2], removed_face.vertices[3]),
	});

	const OperationResult result = bridge_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* bridged_face = find_face(document, selection.faces.front());
	EXPECT_TRUE(bridged_face != nullptr);
	EXPECT_TRUE(bridged_face->vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesRejectsTwoNonBoundarySpansWithoutMutation) {
	AngledGapFixture fixture = make_angled_gap_fixture();
	Selection selection = qdr_test::edge_selection({
		fixture.left_edge,
		fixture.right_edge,
	}, fixture.right_edge);
	const std::string original_document = qdr_test::document_fingerprint(fixture.document);
	const std::string original_selection = qdr_test::selection_fingerprint(selection);

	const OperationResult result = bridge_selected_edges(fixture.document, selection);

	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.changed);
	EXPECT_FALSE(result.message.empty());
	EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), original_document);
	EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesRejectsOneNonBoundarySpanWithoutMutation) {
	CrossedGapFixture fixture = make_crossed_gap_fixture();
	Selection selection = qdr_test::edge_selection({
		fixture.left_edge,
		fixture.right_edge,
	}, fixture.right_edge);
	const std::string original_document = qdr_test::document_fingerprint(fixture.document);
	const std::string original_selection = qdr_test::selection_fingerprint(selection);

	const OperationResult result = bridge_selected_edges(fixture.document, selection);

	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.changed);
	EXPECT_FALSE(result.message.empty());
	EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), original_document);
	EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesSupportsSteppedSpansBetweenTwoOpenEdges) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());
	bool has_plane_z = false;
	float plane_z = 0.0F;
	for (ElementId vertex_id : removed_face.vertices) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		if (vertex == nullptr) {
			continue;
		}
		plane_z = has_plane_z ? std::max(plane_z, vertex->position.z) : vertex->position.z;
		has_plane_z = true;
	}
	Selection selection = qdr_test::edge_selection({
		make_edge(removed_face.vertices[0], removed_face.vertices[1]),
		make_edge(removed_face.vertices[2], removed_face.vertices[3]),
	});

	const OperationResult result = bridge_selected_edges(document, selection, 3);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 3U);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(removed_face.vertices[0], removed_face.vertices[1])) == 2);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(removed_face.vertices[2], removed_face.vertices[3])) == 2);
	EXPECT_TRUE(has_plane_z);
	EXPECT_TRUE(std::any_of(document.vertices.begin(), document.vertices.end(), [plane_z](const Vertex& vertex) {
		return vertex.position.z > plane_z + 0.001F;
	}));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesPreservesUnrelatedCollapsedFacesAndLooseVertices) {
    for (const int steps : {1, 3}) {
        SCOPED_TRACE(steps);
        Document document = make_cube({});
        const Face removed_face = document.faces.front();
        document.faces.erase(document.faces.begin());
        const Document island = qdr_test::append_unrelated_authored_geometry(document);
        Selection selection = qdr_test::edge_selection({
            make_edge(removed_face.vertices[0], removed_face.vertices[1]),
            make_edge(removed_face.vertices[2], removed_face.vertices[3])});

        const OperationResult result = bridge_selected_edges(document, selection, steps);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        EXPECT_EQ(selection.mode, SelectionMode::Face);
        ASSERT_EQ(selection.faces.size(), static_cast<std::size_t>(steps));
        for (const ElementId face_id : selection.faces) {
            const Face* face = find_face(document, face_id);
            ASSERT_NE(face, nullptr);
            EXPECT_TRUE(qdr_test::face_compiles_to_triangles(document, *face));
        }
        EXPECT_TRUE(validate_polygon_selection_liveness(document, selection).ok());
        qdr_test::expect_unrelated_authored_geometry_preserved(document, island);
    }
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesRejectsNonfiniteOrMalformedInputAtomically) {
    for (const auto fault : qdr_test::kAuthoredInputFaults) {
        SCOPED_TRACE(static_cast<int>(fault));
        Document document = make_cube({});
        const Face removed_face = document.faces.front();
        document.faces.erase(document.faces.begin());
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(document));
        qdr_test::corrupt_unrelated_authored_geometry(document, fault);
        const Edge first = make_edge(removed_face.vertices[0], removed_face.vertices[1]);
        const Edge second = make_edge(removed_face.vertices[2], removed_face.vertices[3]);
        Selection selection = qdr_test::edge_selection({first, second}, second);
        const std::string original_document = qdr_test::document_fingerprint(document);
        const std::string original_selection = qdr_test::selection_fingerprint(selection);

        const OperationResult result = bridge_selected_edges(document, selection, 3);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(document), original_document);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
    }
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesPreservesSourceChartsAndUsesTheFirstDonorAttributes) {
    AngledGapFixture fixture = make_angled_gap_fixture();
    fixture.document.faces.resize(2);
    Face& first_source = fixture.document.faces[0];
    Face& second_source = fixture.document.faces[1];
    first_source.material_slot = 7;
    first_source.normal_shading = SurfaceShading::Smooth;
    first_source.texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
    first_source.uvs = {{0.25F, 0.5F}, {1.25F, 0.5F}, {1.25F, 2.0F}, {0.25F, 2.0F}};
    second_source.material_slot = 11;
    second_source.normal_shading = SurfaceShading::Flat;
    second_source.texture_transform = {{0.5F, -1.5F}, {-0.25F, 0.75F}, -20.0F};
    second_source.uvs = {{2.0F, 0.5F}, {2.0F, 3.0F}, {3.0F, 3.0F}, {3.0F, 0.5F}};
    const Face donor = first_source;
    std::map<ElementId, quader::QVec2> expected_uvs;
    for (const Face& source_face : fixture.document.faces) {
        for (std::size_t corner = 0; corner < source_face.vertices.size(); ++corner) {
            expected_uvs.emplace(source_face.vertices[corner], apply_face_texture_transform(
                source_face.uvs[corner], source_face.texture_transform));
        }
    }
    const Document original = fixture.document;
    Selection selection = qdr_test::edge_selection({fixture.left_edge, fixture.right_edge});

    const OperationResult result = bridge_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(selection.faces.size(), 1U);
    const Face* bridge = find_face(fixture.document, selection.faces.front());
    ASSERT_NE(bridge, nullptr);
    EXPECT_EQ(bridge->material_slot, donor.material_slot);
    EXPECT_EQ(bridge->normal_shading, donor.normal_shading);
    EXPECT_EQ(bridge->texture_transform, donor.texture_transform);
    ASSERT_EQ(bridge->uvs.size(), bridge->vertices.size());
    for (std::size_t corner = 0; corner < bridge->vertices.size(); ++corner) {
        const auto actual = apply_face_texture_transform(bridge->uvs[corner], bridge->texture_transform);
        const auto expected = expected_uvs.at(bridge->vertices[corner]);
        EXPECT_TRUE(std::isfinite(actual.x));
        EXPECT_TRUE(std::isfinite(actual.y));
        EXPECT_NEAR(actual.x, expected.x, 0.0001F);
        EXPECT_NEAR(actual.y, expected.y, 0.0001F);
    }
    Document retained_sources = fixture.document;
    retained_sources.faces.resize(original.faces.size());
    retained_sources.next_face_id = original.next_face_id;
    EXPECT_EQ(qdr_test::document_fingerprint(retained_sources), qdr_test::document_fingerprint(original));
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationBridgeEdgesTests, CurvedBridgeRightAngleMatchesQuarterCircleAndReflectedCoordinates) {
    for (const float reflection : {1.0F, -1.0F}) {
        for (const int steps : {1, 4, 16}) {
            SCOPED_TRACE(reflection);
            SCOPED_TRACE(steps);
            AngledGapFixture fixture = make_quarter_turn_gap_fixture(reflection);
            const Document original = fixture.document;
            Selection selection = qdr_test::edge_selection({fixture.left_edge, fixture.right_edge});

            const OperationResult result = bridge_selected_edges(fixture.document, selection, steps);

            ASSERT_TRUE(result.success) << result.message;
            ASSERT_TRUE(result.changed);
            ASSERT_EQ(selection.faces.size(), static_cast<std::size_t>(steps));
            ASSERT_EQ(fixture.document.vertices.size(), original.vertices.size() +
                2U * static_cast<std::size_t>(steps - 1));
            for (const Vertex& vertex : original.vertices) {
                const Vertex* retained = find_vertex(fixture.document, vertex.id);
                ASSERT_NE(retained, nullptr);
                EXPECT_FLOAT_EQ(retained->position.x, vertex.position.x);
                EXPECT_FLOAT_EQ(retained->position.y, vertex.position.y);
                EXPECT_FLOAT_EQ(retained->position.z, vertex.position.z);
            }
            for (std::size_t index = original.vertices.size(); index < fixture.document.vertices.size(); ++index) {
                const auto position = fixture.document.vertices[index].position;
                EXPECT_TRUE(std::isfinite(position.x));
                EXPECT_TRUE(std::isfinite(position.y));
                EXPECT_TRUE(std::isfinite(position.z));
                // The standard cubic quarter arc stays within 0.000273 of its
                // unit-radius circle centered at (0, 1), on both bridge rails.
                const float radius = std::hypot(position.x, position.y - 1.0F);
                EXPECT_NEAR(radius, 1.0F, 0.00028F);
                EXPECT_TRUE(position.z == 0.0F || position.z == 1.0F);
            }
            if (steps > 1) {
                // Independent cubic samples using the quarter-circle Bezier
                // control distance 0.55228475, at t=1/4, 1/2 and 3/4.
                const std::array<quader::QVec2, 3> samples{{
                    {0.3892451288F, 0.0785849571F},
                    {0.7071067812F, 0.2928932188F},
                    {0.9214150429F, 0.6107548712F},
                }};
                for (const auto sample : samples) {
                    for (const float z : {0.0F, 1.0F}) {
                        EXPECT_TRUE(std::ranges::any_of(fixture.document.vertices, [&](const Vertex& vertex) {
                            return std::abs(vertex.position.x - reflection * sample.x) < 0.00001F &&
                                std::abs(vertex.position.y - sample.y) < 0.00001F &&
                                vertex.position.z == z;
                        }));
                    }
                }
            }
            qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        }
    }
}

TEST(QdrDocumentOperationBridgeEdgesTests, CurvedBridgeStraightRailsKeepUniformLinearSpacing) {
    AngledGapFixture fixture = make_curved_gap_fixture(
        {-3.0F, 2.0F, 0.0F}, {5.0F, 2.0F, 0.0F},
        {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
    Selection selection = qdr_test::edge_selection({fixture.left_edge, fixture.right_edge});
    const OperationResult result = bridge_selected_edges(fixture.document, selection, 8);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    for (int step = 0; step <= 8; ++step) {
        SCOPED_TRACE(step);
        for (const float z : {0.0F, 1.0F}) {
            EXPECT_TRUE(std::ranges::any_of(fixture.document.vertices, [&](const Vertex& vertex) {
                return vertex.position.x == -3.0F + static_cast<float>(step) &&
                    vertex.position.y == 2.0F && vertex.position.z == z;
            }));
        }
    }
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationBridgeEdgesTests, CurvedBridgeCircularScaleSupportsAcuteAndObtuseJoins) {
    for (const float sign : {1.0F, -1.0F}) {
        SCOPED_TRACE(sign);
        // Unit tangent frames describe 60- and 120-degree arcs of radius three.
        AngledGapFixture fixture = make_curved_gap_fixture(
            {3.0F, 0.0F, 0.0F}, {sign * 1.5F, 2.5980762114F, 0.0F},
            {0.0F, 1.0F, 0.0F}, {0.8660254038F, -sign * 0.5F, 0.0F});
        const std::size_t original_vertex_count = fixture.document.vertices.size();
        Selection selection = qdr_test::edge_selection({fixture.left_edge, fixture.right_edge});
        const OperationResult result = bridge_selected_edges(fixture.document, selection, 2);
        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(fixture.document.vertices.size(), original_vertex_count + 2U);
        for (std::size_t index = original_vertex_count; index < fixture.document.vertices.size(); ++index) {
            const auto midpoint = fixture.document.vertices[index].position;
            EXPECT_NEAR(midpoint.x, sign > 0.0F ? 2.5980762114F : 1.5F, 0.00001F);
            EXPECT_NEAR(midpoint.y, sign > 0.0F ? 1.5F : 2.5980762114F, 0.00001F);
            EXPECT_TRUE(midpoint.z == 0.0F || midpoint.z == 1.0F);
            EXPECT_NEAR(std::hypot(midpoint.x, midpoint.y), 3.0F, 0.00001F);
        }
        qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
    }
}

TEST(QdrDocumentOperationBridgeEdgesTests, CurvedBridgeKeepsUnsupportedTangentFrames) {
    for (const bool antiparallel : {true, false}) {
        SCOPED_TRACE(antiparallel);
        // Antiparallel forward tangents and a skewed chord retain their prior
        // unit-rail Hermite magnitudes rather than an invented circular join.
        AngledGapFixture fixture = make_curved_gap_fixture(
            {}, {2.0F, 0.0F, 0.0F},
            antiparallel ? quader::QVec3{0.0F, 1.0F, 0.0F} : quader::QVec3{1.0F, 0.0F, 0.0F},
            antiparallel ? quader::QVec3{0.0F, 1.0F, 0.0F} : quader::QVec3{0.0F, -1.0F, 0.0F});
        const std::size_t original_vertex_count = fixture.document.vertices.size();
        Selection selection = qdr_test::edge_selection({fixture.left_edge, fixture.right_edge});
        const OperationResult result = bridge_selected_edges(fixture.document, selection, 2);
        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(fixture.document.vertices.size(), original_vertex_count + 2U);
        for (std::size_t index = original_vertex_count; index < fixture.document.vertices.size(); ++index) {
            const auto midpoint = fixture.document.vertices[index].position;
            EXPECT_FLOAT_EQ(midpoint.x, antiparallel ? 1.0F : 1.25F);
            EXPECT_FLOAT_EQ(midpoint.y, antiparallel ? 0.5F : -0.25F);
            EXPECT_TRUE(midpoint.z == 0.0F || midpoint.z == 1.0F);
        }
        qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
    }
}

TEST(QdrDocumentOperationBridgeEdgesTests, CurvedBridgeKeepsSharedDegenerateRailEndpoint) {
    Document document = make_cube({});
    const Face removed_face = document.faces.front();
    document.faces.erase(document.faces.begin());
    const ElementId shared = removed_face.vertices[1];
    const quader::QVec3 original_position = find_vertex(document, shared)->position;
    const std::size_t original_vertex_count = document.vertices.size();
    Selection selection = qdr_test::edge_selection({
        make_edge(removed_face.vertices[0], shared),
        make_edge(shared, removed_face.vertices[2])});

    const OperationResult result = bridge_selected_edges(document, selection, 3);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(selection.faces.size(), 3U);
    EXPECT_EQ(document.vertices.size(), original_vertex_count + 2U);
    const Vertex* endpoint = find_vertex(document, shared);
    ASSERT_NE(endpoint, nullptr);
    EXPECT_FLOAT_EQ(endpoint->position.x, original_position.x);
    EXPECT_FLOAT_EQ(endpoint->position.y, original_position.y);
    EXPECT_FLOAT_EQ(endpoint->position.z, original_position.z);
    for (const ElementId face_id : selection.faces) {
        const Face* face = find_face(document, face_id);
        ASSERT_NE(face, nullptr);
        EXPECT_EQ(face->vertices.size(), 3U);
        EXPECT_NE(std::ranges::find(face->vertices, shared), face->vertices.end());
    }
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}
