////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>
#include <map>
#include <span>

using namespace quader_poly;

namespace {

/**
 * Stores the Circular Boundary Fixture data contract used by the automated test suite.
 */
struct CircularBoundaryFixture {
	Document document;
	std::array<Edge, 8> inner_edges {};
};

/**
 * Supplies one open boundary with consistently wound adjacent triangles.
 */
struct OpenBoundaryFixture {
    Document document;
    Selection selection;
};

OpenBoundaryFixture make_open_boundary_fixture(std::span<const quader::QVec3> positions)
{
    OpenBoundaryFixture fixture;
    std::vector<ElementId> boundary;
    quader::QVec3 center {};
    for (const quader::QVec3 position : positions) {
        boundary.push_back(add_vertex(fixture.document, position));
        center += position;
    }
    center = center / static_cast<float>(positions.size());
    float extent = 0.0F;
    for (const quader::QVec3 position : positions) {
        extent = std::max(extent, quader::length(position - center));
    }
    const ElementId apex = add_vertex(
        fixture.document, center + quader::QVec3 { 9.0F, 11.0F, 13.0F } * extent);
    fixture.selection.mode = SelectionMode::Edge;
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const ElementId first = boundary[index];
        const ElementId second = boundary[(index + 1U) % boundary.size()];
        EXPECT_NE(add_face(fixture.document, std::array { first, second, apex }), kInvalidElementId);
        fixture.selection.edges.push_back(make_edge(first, second));
    }
    fixture.selection.has_active = true;
    fixture.selection.active_kind = ElementKind::Edge;
    fixture.selection.active_edge = fixture.selection.edges.back();
    return fixture;
}

void expect_complete_compiled_cap(const Document& document, const Face& cap)
{
    Document cap_document = document;
    cap_document.faces = { cap };
    const CompiledMesh mesh = compile_document(cap_document);
    ASSERT_EQ(mesh.indices.size(), (cap.vertices.size() - 2U) * 3U);
    std::map<std::pair<ElementId, ElementId>, int> uses;
    std::map<std::pair<ElementId, ElementId>, int> winding;
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
        std::array<ElementId, 3> ids {};
        std::array<quader::QVec3, 3> points {};
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            ASSERT_LT(mesh.indices[index + corner], mesh.vertices.size());
            const CompiledVertex& vertex = mesh.vertices[mesh.indices[index + corner]];
            ids[corner] = vertex.source_vertex_id;
            points[corner] = vertex.position;
            EXPECT_NE(std::ranges::find(cap.vertices, ids[corner]), cap.vertices.end());
        }
        const std::array<double, 3> ab {
            static_cast<double>(points[1].x) - points[0].x,
            static_cast<double>(points[1].y) - points[0].y,
            static_cast<double>(points[1].z) - points[0].z,
        };
        const std::array<double, 3> ac {
            static_cast<double>(points[2].x) - points[0].x,
            static_cast<double>(points[2].y) - points[0].y,
            static_cast<double>(points[2].z) - points[0].z,
        };
        const quader::QVec3 normal = face_normal(document, cap);
        const double oriented_area =
            (ab[1] * ac[2] - ab[2] * ac[1]) * normal.x +
            (ab[2] * ac[0] - ab[0] * ac[2]) * normal.y +
            (ab[0] * ac[1] - ab[1] * ac[0]) * normal.z;
        EXPECT_GT(oriented_area, 0.0);
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const ElementId from = ids[corner];
            const ElementId to = ids[(corner + 1U) % 3U];
            const auto key = std::minmax(from, to);
            ++uses[key];
            winding[key] += from < to ? 1 : -1;
        }
    }
    for (std::size_t index = 0; index < cap.vertices.size(); ++index) {
        const ElementId from = cap.vertices[index];
        const ElementId to = cap.vertices[(index + 1U) % cap.vertices.size()];
        const auto key = std::minmax(from, to);
        EXPECT_EQ(uses[key], 1) << "missing authored boundary " << from << "," << to;
        EXPECT_EQ(winding[key], from < to ? 1 : -1);
        uses.erase(key);
        winding.erase(key);
    }
    for (const auto& [edge, count] : uses) {
        EXPECT_EQ(count, 2);
        EXPECT_EQ(winding[edge], 0);
    }
}

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
	return fixture;
}

} // namespace

TEST(QdrDocumentOperationFillHoleTests, FillHoleCreatesOneFaceFromASelectedOpenEdgeLoop) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	const quader::QVec3 removed_face_normal = face_normal(document, removed_face);
	document.faces.erase(document.faces.begin());
	Selection selection;
        selection.mode = SelectionMode::Edge;
        for (std::size_t index = 0; index < removed_face.vertices.size(); ++index) {
		selection.edges.push_back(make_edge(removed_face.vertices[index], removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}

	const OperationResult result = fill_hole_from_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 6U);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* fill_face = find_face(document, selection.faces.front());
	EXPECT_TRUE(fill_face != nullptr);
	EXPECT_TRUE(dot(face_normal(document, *fill_face), removed_face_normal) > 0.5F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleIgnoresExtraSelectedNonBoundaryEdgesWhenBoundaryLoopExists) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	const quader::QVec3 removed_face_normal = face_normal(document, removed_face);
	document.faces.erase(document.faces.begin());

	Selection selection;
	selection.mode = SelectionMode::Edge;
	for (std::size_t index = 0; index < removed_face.vertices.size(); ++index) {
		selection.edges.push_back(make_edge(removed_face.vertices[index], removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}
	const Edge hard_boundary = selection.edges[0];
	const Edge soft_boundary = selection.edges[2];
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, hard_boundary, EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, soft_boundary, EdgeNormalMode::Soft));

	Edge extra_non_boundary_edge {};
	bool found_extra_non_boundary_edge = false;
	for (const Face& face : document.faces) {
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
			if (!qdr_test::contains_edge(selection.edges, edge) &&
			    qdr_test::edge_incidence_count(document, edge) == 2) {
				extra_non_boundary_edge = edge;
				found_extra_non_boundary_edge = true;
				break;
			}
		}
		if (found_extra_non_boundary_edge) {
			break;
		}
	}
	ASSERT_TRUE(found_extra_non_boundary_edge);
	selection.edges.push_back(extra_non_boundary_edge);

	const OperationResult result = fill_hole_from_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 6U);
	EXPECT_TRUE(selection.mode == SelectionMode::Face);
	EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* fill_face = find_face(document, selection.faces.front());
	EXPECT_TRUE(fill_face != nullptr);
	EXPECT_TRUE(dot(face_normal(document, *fill_face), removed_face_normal) > 0.5F);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, extra_non_boundary_edge) == 2);
	EXPECT_EQ(qdr_test::edge_normal_mode(document, hard_boundary), EdgeNormalMode::Hard);
	EXPECT_EQ(qdr_test::edge_normal_mode(document, soft_boundary), EdgeNormalMode::Soft);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleExpandsSelectedBoundarySubsetToContainingOpenLoop) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	const quader::QVec3 removed_face_normal = face_normal(document, removed_face);
	document.faces.erase(document.faces.begin());

	Selection selection;
	selection.mode = SelectionMode::Edge;
	for (std::size_t index = 0; index + 1U < removed_face.vertices.size(); ++index) {
		selection.edges.push_back(make_edge(removed_face.vertices[index], removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}
	ASSERT_TRUE(selection.edges.size() == 3U);

	const OperationResult result = fill_hole_from_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 6U);
	EXPECT_TRUE(selection.mode == SelectionMode::Face);
	EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* fill_face = find_face(document, selection.faces.front());
	EXPECT_TRUE(fill_face != nullptr);
	EXPECT_TRUE(dot(face_normal(document, *fill_face), removed_face_normal) > 0.5F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleRejectsASelectedNonBoundaryEdgeLoop) {
	Document document = make_cube({});
	Face& source_face = document.faces.front();
	source_face.material_slot = 5U;
	Selection selection;
        selection.mode = SelectionMode::Edge;
        for (std::size_t index = 0; index < source_face.vertices.size(); ++index) {
		selection.edges.push_back(make_edge(source_face.vertices[index], source_face.vertices[(index + 1U) % source_face.vertices.size()]));
	}
	const Edge first_edge = selection.edges.front();
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, first_edge) == 2);
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = fill_hole_from_selected_edges(document, selection);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(document.faces.size() == face_count_before);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, first_edge) == 2);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleOrientsCapsFromAdjacentBoundaryWindingAfterCircularEdgeExtrusion) {
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

	EXPECT_TRUE(extrude_selected_elements(fixture.document, selection, quader::QVec3 { 0.0F, -0.5F, 0.0F }).changed);

	qdr_test::expect_valid_polygon_document(fixture.document, "successful topology operation");
	const Selection copied_boundary_selection = selection;

	const OperationResult result = fill_hole_from_selected_edges(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* fill_face = find_face(fixture.document, selection.faces.front());
	EXPECT_TRUE(fill_face != nullptr);
	EXPECT_TRUE(dot(face_normal(fixture.document, *fill_face), quader::QVec3 { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));

	Selection repeat_selection = copied_boundary_selection;
	const std::size_t face_count_after_fill = fixture.document.faces.size();
	const OperationResult repeat_result = fill_hole_from_selected_edges(fixture.document, repeat_selection);
	EXPECT_FALSE(repeat_result.changed);
	EXPECT_TRUE(fixture.document.faces.size() == face_count_after_fill);
}

TEST(QdrDocumentOperationFillHoleTests, FillHolePreservesEveryBoxOpeningCornerAcrossOrderingAndTransforms) {
    // The six boundary positions from Box in test_scene.qdr, in adjacent-face order.
    const std::array original_positions {
        quader::QVec3 { -3.0F, -0.5F, 3.5F },
        quader::QVec3 { -3.0F, 3.5F, 3.5F },
        quader::QVec3 { -3.0F, 3.5F, 2.5F },
        quader::QVec3 { -5.0F, 3.5F, 2.5F },
        quader::QVec3 { -5.0F, -0.5F, 0.5F },
        quader::QVec3 { -3.0F, -0.5F, 0.5F },
    };
    for (const float scale : { 0.01F, 1.0F, 1000.0F }) {
        for (const bool reverse : { false, true }) {
            for (std::size_t start = 0; start < original_positions.size(); ++start) {
                SCOPED_TRACE(::testing::Message() << scale << "," << reverse << "," << start);
                auto positions = original_positions;
                std::rotate(positions.begin(), positions.begin() + start, positions.end());
                if (reverse) {
                    std::ranges::reverse(positions);
                }
                for (quader::QVec3& position : positions) {
                    if (scale != 1.0F) {
                        const quader::QVec3 rotated {
                            0.6F * position.x - 0.8F * position.y,
                            0.8F * position.x + 0.6F * position.y,
                            position.z,
                        };
                        position = quader::QVec3 {
                            rotated.x,
                            0.6F * rotated.y - 0.8F * rotated.z,
                            0.8F * rotated.y + 0.6F * rotated.z,
                        };
                        position = (position + quader::QVec3 { 13.0F, -23.0F, 7.0F }) * scale;
                    }
                }
                OpenBoundaryFixture fixture = make_open_boundary_fixture(positions);
                const auto original_vertices = fixture.document.vertices;
                std::ranges::reverse(fixture.selection.edges);

                const OperationResult result = fill_hole_from_selected_edges(fixture.document, fixture.selection);

                ASSERT_TRUE(result.success) << result.message;
                ASSERT_TRUE(result.changed);
                ASSERT_EQ(fixture.selection.mode, SelectionMode::Face);
                ASSERT_EQ(fixture.selection.faces.size(), 1U);
                ASSERT_EQ(fixture.document.faces.size(), positions.size() + 1U);
                const Face* cap = find_face(fixture.document, fixture.selection.faces.front());
                ASSERT_NE(cap, nullptr);
                ASSERT_EQ(cap->vertices.size(), positions.size());
                expect_complete_compiled_cap(fixture.document, *cap);
                ASSERT_EQ(fixture.document.vertices.size(), original_vertices.size());
                for (std::size_t index = 0; index < original_vertices.size(); ++index) {
                    const Vertex& vertex = fixture.document.vertices[index];
                    const Vertex& original = original_vertices[index];
                    EXPECT_EQ(vertex.id, original.id);
                    EXPECT_FLOAT_EQ(vertex.position.x, original.position.x);
                    EXPECT_FLOAT_EQ(vertex.position.y, original.position.y);
                    EXPECT_FLOAT_EQ(vertex.position.z, original.position.z);
                }
                EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
                EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
                qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
            }
        }
    }
}

TEST(QdrDocumentOperationFillHoleTests, FillHolePreservesBoxOpeningBendsAfterStrongAnisotropicScaling) {
    // Box's two-unit X bends remain real corners when only Y and Z grow by 1e6.
    const std::array positions {
        quader::QVec3 { -3.0F, -500000.0F, 3500000.0F },
        quader::QVec3 { -3.0F, 3500000.0F, 3500000.0F },
        quader::QVec3 { -3.0F, 3500000.0F, 2500000.0F },
        quader::QVec3 { -5.0F, 3500000.0F, 2500000.0F },
        quader::QVec3 { -5.0F, -500000.0F, 500000.0F },
        quader::QVec3 { -3.0F, -500000.0F, 500000.0F },
    };
    OpenBoundaryFixture fixture = make_open_boundary_fixture(positions);

    const OperationResult result = fill_hole_from_selected_edges(fixture.document, fixture.selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(fixture.selection.faces.size(), 1U);
    const Face* cap = find_face(fixture.document, fixture.selection.faces.front());
    ASSERT_NE(cap, nullptr);
    ASSERT_EQ(cap->vertices.size(), positions.size());
    expect_complete_compiled_cap(fixture.document, *cap);
    EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
    EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationFillHoleTests, FillHolePreservesAConcaveNonplanarBoundary) {
    const std::array positions {
        quader::QVec3 { 0.0F, 0.0F, 0.0F },
        quader::QVec3 { 3.0F, 0.0F, 0.0F },
        quader::QVec3 { 3.0F, 3.0F, 0.25F },
        quader::QVec3 { 1.5F, 1.0F, -0.25F },
        quader::QVec3 { 0.0F, 3.0F, 0.0F },
    };
    OpenBoundaryFixture fixture = make_open_boundary_fixture(positions);

    const OperationResult result = fill_hole_from_selected_edges(fixture.document, fixture.selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(fixture.selection.faces.size(), 1U);
    const Face* cap = find_face(fixture.document, fixture.selection.faces.front());
    ASSERT_NE(cap, nullptr);
    ASSERT_EQ(cap->vertices.size(), positions.size());
    expect_complete_compiled_cap(fixture.document, *cap);
    EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
    EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleAcceptsACollinearBoundaryCornerWithoutChangingTopology) {
    const std::array positions {
        quader::QVec3 { 0.0F, 0.0F, 0.0F },
        quader::QVec3 { 1.0F, 0.0F, 0.0F },
        quader::QVec3 { 2.0F, 0.0F, 0.0F },
        quader::QVec3 { 2.0F, 2.0F, 0.0F },
        quader::QVec3 { 0.0F, 2.0F, 0.0F },
    };
    OpenBoundaryFixture fixture = make_open_boundary_fixture(positions);

    const OperationResult result = fill_hole_from_selected_edges(fixture.document, fixture.selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(fixture.selection.faces.size(), 1U);
    const Face* cap = find_face(fixture.document, fixture.selection.faces.front());
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->vertices.size(), positions.size());
    Document cap_document = fixture.document;
    cap_document.faces = { *cap };
    const CompiledMesh mesh = compile_document(cap_document);
    ASSERT_FALSE(mesh.indices.empty());
    float area = 0.0F;
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
        const quader::QVec3 a = mesh.vertices[mesh.indices[index]].position;
        const quader::QVec3 b = mesh.vertices[mesh.indices[index + 1U]].position;
        const quader::QVec3 c = mesh.vertices[mesh.indices[index + 2U]].position;
        area += 0.5F * quader::length(quader::cross(b - a, c - a));
    }
    EXPECT_FLOAT_EQ(area, 4.0F);
    EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
    EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationFillHoleTests, FillHolePreservesAShortBoundaryEdgeWithPositiveTriangleArea) {
    const std::array positions {
        quader::QVec3 { 0.0F, 0.0F, 0.0F },
        quader::QVec3 { 1.0e-7F, 0.0F, 0.0F },
        quader::QVec3 { 100.0F, 100.0F, 0.0F },
        quader::QVec3 { 0.0F, 200.0F, 0.0F },
        quader::QVec3 { -100.0F, 100.0F, 0.0F },
    };
    OpenBoundaryFixture fixture = make_open_boundary_fixture(positions);

    const OperationResult result = fill_hole_from_selected_edges(fixture.document, fixture.selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(fixture.selection.faces.size(), 1U);
    const Face* cap = find_face(fixture.document, fixture.selection.faces.front());
    ASSERT_NE(cap, nullptr);
    ASSERT_EQ(cap->vertices.size(), positions.size());
    expect_complete_compiled_cap(fixture.document, *cap);
    EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
    EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleRejectsSelfIntersectingBoundaryWithoutChangingState) {
    const std::array positions {
        quader::QVec3 { 0.0F, 0.0F, 0.0F },
        quader::QVec3 { 3.0F, 2.0F, 0.0F },
        quader::QVec3 { 0.0F, 2.0F, 0.0F },
        quader::QVec3 { 2.0F, 0.0F, 0.0F },
        quader::QVec3 { 0.0F, -1.0F, 0.0F },
    };
    OpenBoundaryFixture fixture = make_open_boundary_fixture(positions);
    const Document original = fixture.document;
    const Selection original_selection = fixture.selection;

    const OperationResult result = fill_hole_from_selected_edges(fixture.document, fixture.selection);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(fixture.document.next_vertex_id, original.next_vertex_id);
    EXPECT_EQ(fixture.document.next_face_id, original.next_face_id);
    ASSERT_EQ(fixture.document.vertices.size(), original.vertices.size());
    ASSERT_EQ(fixture.document.faces.size(), original.faces.size());
    for (std::size_t index = 0; index < original.vertices.size(); ++index) {
        EXPECT_EQ(fixture.document.vertices[index].id, original.vertices[index].id);
        EXPECT_FLOAT_EQ(fixture.document.vertices[index].position.x, original.vertices[index].position.x);
        EXPECT_FLOAT_EQ(fixture.document.vertices[index].position.y, original.vertices[index].position.y);
        EXPECT_FLOAT_EQ(fixture.document.vertices[index].position.z, original.vertices[index].position.z);
    }
    for (std::size_t index = 0; index < original.faces.size(); ++index) {
        EXPECT_EQ(fixture.document.faces[index].id, original.faces[index].id);
        EXPECT_EQ(fixture.document.faces[index].vertices, original.faces[index].vertices);
    }
    EXPECT_EQ(fixture.selection.mode, original_selection.mode);
    EXPECT_EQ(fixture.selection.edges, original_selection.edges);
    EXPECT_EQ(fixture.selection.has_active, original_selection.has_active);
    EXPECT_EQ(fixture.selection.active_kind, original_selection.active_kind);
    EXPECT_EQ(fixture.selection.active_edge, original_selection.active_edge);
}
