////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>
#include <map>
#include <ranges>
#include <set>

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

namespace {

struct SplitGridFixture {
    Document document;
    std::vector<ElementId> vertices;
    std::size_t columns = 0U;

    ElementId vertex(std::size_t column, std::size_t row) const
    {
        return vertices[row * (columns + 1U) + column];
    }
};

SplitGridFixture make_split_grid(std::size_t columns, std::size_t rows)
{
    SplitGridFixture fixture;
    fixture.columns = columns;
    for (std::size_t row = 0U; row <= rows; ++row) {
        for (std::size_t column = 0U; column <= columns; ++column) {
            fixture.vertices.push_back(add_vertex(fixture.document,
                {static_cast<float>(column), static_cast<float>(row), 0.0F}));
        }
    }
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            const std::array<ElementId, 4> corners{
                fixture.vertex(column, row), fixture.vertex(column + 1U, row),
                fixture.vertex(column + 1U, row + 1U), fixture.vertex(column, row + 1U)};
            EXPECT_NE(add_face(fixture.document, corners), kInvalidElementId);
        }
    }
    return fixture;
}

std::vector<Edge> split_grid_chain(const SplitGridFixture& fixture,
                                  std::size_t column, std::size_t rows)
{
    std::vector<Edge> edges;
    for (std::size_t row = 0U; row < rows; ++row) {
        edges.push_back(make_edge(fixture.vertex(column, row),
                                  fixture.vertex(column, row + 1U)));
    }
    return edges;
}

Selection split_edge_selection(const std::vector<Edge>& edges, Edge active)
{
    Selection selection = qdr_test::edge_selection({}, active);
    selection.edges = edges;
    return selection;
}

void expect_split_corner_attributes_preserved(const Document& source,
                                               const Document& document)
{
    ASSERT_EQ(document.faces.size(), source.faces.size());
    for (const Vertex& vertex : source.vertices) {
        const Vertex* retained = find_vertex(document, vertex.id);
        ASSERT_NE(retained, nullptr);
        EXPECT_EQ(retained->position.x, vertex.position.x);
        EXPECT_EQ(retained->position.y, vertex.position.y);
        EXPECT_EQ(retained->position.z, vertex.position.z);
    }
    for (const Face& before : source.faces) {
        const Face* face = find_face(document, before.id);
        ASSERT_NE(face, nullptr);
        ASSERT_EQ(face->vertices.size(), before.vertices.size());
        ASSERT_EQ(face->uvs.size(), before.uvs.size());
        EXPECT_EQ(face->material_slot, before.material_slot);
        EXPECT_EQ(face->normal_shading, before.normal_shading);
        EXPECT_EQ(face->texture_transform, before.texture_transform);
        for (std::size_t corner = 0U; corner < face->vertices.size(); ++corner) {
            const Vertex* after = find_vertex(document, face->vertices[corner]);
            const Vertex* original = find_vertex(source, before.vertices[corner]);
            ASSERT_NE(after, nullptr);
            ASSERT_NE(original, nullptr);
            EXPECT_EQ(after->position.x, original->position.x);
            EXPECT_EQ(after->position.y, original->position.y);
            EXPECT_EQ(after->position.z, original->position.z);
            const std::size_t next = (corner + 1U) % face->vertices.size();
            EXPECT_EQ(qdr_test::edge_normal_mode(document,
                make_edge(face->vertices[corner], face->vertices[next])),
                qdr_test::edge_normal_mode(source,
                make_edge(before.vertices[corner], before.vertices[next])));
        }
        for (std::size_t corner = 0U; corner < face->uvs.size(); ++corner) {
            EXPECT_EQ(face->uvs[corner].x, before.uvs[corner].x);
            EXPECT_EQ(face->uvs[corner].y, before.uvs[corner].y);
        }
    }
}

void expect_split_incidence(const Document& source, const Document& document,
                            const std::vector<Edge>& selected,
                            const Selection& selection,
                            const std::vector<Edge>& terminal_edges = {})
{
    std::map<std::pair<ElementId, ElementId>, std::vector<Edge>> mapped_edges;
    for (const Face& before : source.faces) {
        const Face* after = find_face(document, before.id);
        ASSERT_NE(after, nullptr);
        ASSERT_EQ(after->vertices.size(), before.vertices.size());
        for (std::size_t corner = 0U; corner < before.vertices.size(); ++corner) {
            const std::size_t next = (corner + 1U) % before.vertices.size();
            const Edge original = make_edge(before.vertices[corner], before.vertices[next]);
            mapped_edges[{original.a, original.b}].push_back(
                make_edge(after->vertices[corner], after->vertices[next]));
        }
    }
    EXPECT_EQ(selection.edges.size(), selected.size() * 2U);
    for (const auto& [key, edges] : mapped_edges) {
        const Edge original = make_edge(key.first, key.second);
        if (qdr_test::contains_edge(selected, original) ||
            qdr_test::contains_edge(terminal_edges, original)) {
            ASSERT_EQ(edges.size(), 2U);
            EXPECT_NE(edges[0], edges[1]);
            for (const Edge edge : edges) {
                EXPECT_EQ(qdr_test::edge_incidence_count(document, edge), 1);
                if (qdr_test::contains_edge(selected, original)) {
                    EXPECT_TRUE(qdr_test::contains_edge(selection.edges, edge));
                }
            }
        } else {
            for (const Edge edge : edges) {
                EXPECT_EQ(edge, edges.front())
                    << "An unselected edge was torn inside a face fan.";
                EXPECT_EQ(qdr_test::edge_incidence_count(document, edge), edges.size());
            }
        }
    }
}

void expect_split_rejected_unchanged(Document& document, Selection& selection)
{
    const Document source = document;
    const Selection original_selection = selection;
    const OperationResult result = split_selected_edges(document, selection);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.created.empty());
    EXPECT_TRUE(result.deleted.empty());
    EXPECT_TRUE(result.affected.empty());
    ASSERT_EQ(document.vertices.size(), source.vertices.size());
    EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
    EXPECT_EQ(document.next_face_id, source.next_face_id);
    EXPECT_EQ(document.hard_normal_edges, source.hard_normal_edges);
    EXPECT_EQ(document.soft_normal_edges, source.soft_normal_edges);
    for (std::size_t index = 0U; index < document.vertices.size(); ++index) {
        EXPECT_EQ(document.vertices[index].id, source.vertices[index].id);
    }
    expect_split_corner_attributes_preserved(source, document);
    for (std::size_t index = 0U; index < document.faces.size(); ++index) {
        EXPECT_EQ(document.faces[index].vertices, source.faces[index].vertices);
    }
    EXPECT_EQ(selection.mode, original_selection.mode);
    EXPECT_EQ(selection.vertices, original_selection.vertices);
    EXPECT_EQ(selection.edges, original_selection.edges);
    EXPECT_EQ(selection.faces, original_selection.faces);
    EXPECT_EQ(selection.has_active, original_selection.has_active);
    EXPECT_EQ(selection.active_kind, original_selection.active_kind);
    EXPECT_EQ(selection.active_vertex, original_selection.active_vertex);
    EXPECT_EQ(selection.active_edge, original_selection.active_edge);
    EXPECT_EQ(selection.active_face, original_selection.active_face);
}

} // namespace

TEST(QdrDocumentOperationSplitEdgesTests, ConnectedChainRetainsEveryUnselectedFanEdge)
{
    SplitGridFixture fixture = make_split_grid(2U, 3U);
    for (std::size_t index = 0U; index < fixture.document.faces.size(); ++index) {
        Face& face = fixture.document.faces[index];
        face.material_slot = static_cast<std::uint32_t>(index + 4U);
        face.normal_shading = index % 2U == 0U ? SurfaceShading::Flat : SurfaceShading::Smooth;
        face.uvs = {{0.1F, 0.2F}, {3.0F, 0.4F}, {2.0F, 7.0F}, {-4.0F, 5.0F}};
        face.texture_transform = {{-2.0F, 3.0F}, {0.25F, -0.5F}, 37.0F};
    }
    const std::vector<Edge> all_edges = document_edges(fixture.document);
    for (std::size_t index = 0U; index < all_edges.size(); ++index) {
        if (index % 3U == 0U) {
            fixture.document.hard_normal_edges.push_back(all_edges[index]);
        } else if (index % 3U == 1U) {
            fixture.document.soft_normal_edges.push_back(all_edges[index]);
        }
    }
    const Document source = fixture.document;
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 3U);
    Selection selection = qdr_test::edge_selection({edges[2], edges[0], edges[1]}, edges[1]);

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.created.vertices.size(), 4U);
    EXPECT_EQ(result.created.edges.size(), 3U);
    EXPECT_EQ(result.affected.faces.size(), 3U);
    EXPECT_EQ(fixture.document.vertices.size(), source.vertices.size() + 4U);
    EXPECT_EQ(qdr_test::open_edge_count(fixture.document), 16U);
    expect_split_incidence(source, fixture.document, edges, selection);
    expect_split_corner_attributes_preserved(source, fixture.document);
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesTests, BoundaryEndpointCopiesTheCompleteIncidentSideFan)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    Face& original_face = fixture.document.faces.front();
    const Face before = original_face;
    original_face.vertices = {before.vertices[0], before.vertices[1], before.vertices[3]};
    const ElementId added_face = add_face(fixture.document,
        std::array<ElementId, 3>{before.vertices[1], before.vertices[2], before.vertices[3]});
    ASSERT_NE(added_face, kInvalidElementId);
    std::rotate(fixture.document.faces.begin() + 1,
                fixture.document.faces.end() - 1, fixture.document.faces.end());
    const Document source = fixture.document;
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 2U);
    Selection selection = split_edge_selection(edges, edges.front());

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.created.vertices.size(), 3U);
    EXPECT_EQ(result.affected.faces.size(), 3U);
    const Face* first_side = find_face(fixture.document, before.id);
    const Face* second_side = find_face(fixture.document, added_face);
    ASSERT_NE(first_side, nullptr);
    ASSERT_NE(second_side, nullptr);
    EXPECT_NE(first_side->vertices[1], before.vertices[1]);
    EXPECT_EQ(first_side->vertices[1], second_side->vertices[0]);
    expect_split_incidence(source, fixture.document, edges, selection);
    expect_split_corner_attributes_preserved(source, fixture.document);
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesTests, ClosedLoopCopiesCoherentFansAcrossSeveralFaces)
{
    SplitGridFixture fixture = make_split_grid(4U, 4U);
    const std::array<ElementId, 8> loop{
        fixture.vertex(1U, 1U), fixture.vertex(2U, 1U), fixture.vertex(3U, 1U),
        fixture.vertex(3U, 2U), fixture.vertex(3U, 3U), fixture.vertex(2U, 3U),
        fixture.vertex(1U, 3U), fixture.vertex(1U, 2U)};
    std::vector<Edge> edges;
    for (std::size_t index = 0U; index < loop.size(); ++index) {
        edges.push_back(make_edge(loop[index], loop[(index + 1U) % loop.size()]));
    }
    const Document source = fixture.document;
    Selection selection = split_edge_selection(edges, edges.front());

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.created.vertices.size(), loop.size());
    EXPECT_EQ(result.created.edges.size(), edges.size());
    EXPECT_EQ(qdr_test::open_edge_count(fixture.document), 32U);
    expect_split_incidence(source, fixture.document, edges, selection);
    expect_split_corner_attributes_preserved(source, fixture.document);
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesTests, InteriorChainEndpointsOpenOnlyTheirTerminalNeighborEdges)
{
    SplitGridFixture fixture = make_split_grid(3U, 4U);
    const std::vector<Edge> edges{
        make_edge(fixture.vertex(1U, 1U), fixture.vertex(1U, 2U)),
        make_edge(fixture.vertex(1U, 2U), fixture.vertex(1U, 3U))};
    const std::vector<Edge> terminals{
        make_edge(fixture.vertex(0U, 1U), fixture.vertex(1U, 1U)),
        make_edge(fixture.vertex(0U, 3U), fixture.vertex(1U, 3U))};
    const Document source = fixture.document;
    Selection selection = split_edge_selection(edges, edges.front());

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.created.vertices.size(), 3U);
    EXPECT_EQ(result.created.edges.size(), 2U);
    EXPECT_EQ(result.affected.faces.size(), 2U);
    EXPECT_EQ(qdr_test::open_edge_count(fixture.document), 22U);
    expect_split_incidence(source, fixture.document, edges, selection, terminals);
    expect_split_corner_attributes_preserved(source, fixture.document);
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesTests, MultipleIndependentChainsKeepTheirSidesConnected)
{
    SplitGridFixture fixture = make_split_grid(4U, 3U);
    std::vector<Edge> edges = split_grid_chain(fixture, 1U, 3U);
    const std::vector<Edge> second_chain = split_grid_chain(fixture, 3U, 3U);
    edges.insert(edges.end(), second_chain.begin(), second_chain.end());
    const Document source = fixture.document;
    Selection selection = split_edge_selection(edges, second_chain.back());

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.created.vertices.size(), 8U);
    EXPECT_EQ(result.created.edges.size(), 6U);
    expect_split_incidence(source, fixture.document, edges, selection);
    expect_split_corner_attributes_preserved(source, fixture.document);
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesTests, BranchedSelectionRejectsAllComponentsAtomically)
{
    SplitGridFixture fixture = make_split_grid(4U, 3U);
    std::vector<Edge> edges = split_grid_chain(fixture, 1U, 3U);
    const std::vector<Edge> second_chain = split_grid_chain(fixture, 3U, 3U);
    edges.insert(edges.end(), second_chain.begin(), second_chain.end());
    edges.push_back(make_edge(fixture.vertex(3U, 1U), fixture.vertex(2U, 1U)));
    Selection selection = split_edge_selection(edges, edges.front());

    expect_split_rejected_unchanged(fixture.document, selection);
}

TEST(QdrDocumentOperationSplitEdgesTests, NonmanifoldSelectedEdgeRejectsWithoutMutation)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 2U);
    const ElementId extra = add_vertex(fixture.document, {1.5F, 0.5F, 1.0F});
    EXPECT_NE(add_face(fixture.document, std::array<ElementId, 3>{edges[0].a, edges[0].b, extra}),
              kInvalidElementId);
    Selection selection = split_edge_selection(edges, edges.front());

    expect_split_rejected_unchanged(fixture.document, selection);
}

TEST(QdrDocumentOperationSplitEdgesTests, BoundaryAndMissingSelectedEdgesRejectWithoutMutation)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    const std::vector<Edge> interior = split_grid_chain(fixture, 1U, 2U);
    const std::array<Edge, 2> invalid{
        make_edge(fixture.vertex(0U, 0U), fixture.vertex(1U, 0U)),
        make_edge(fixture.vertex(0U, 0U), fixture.vertex(2U, 2U))};
    for (const Edge edge : invalid) {
        Selection selection = qdr_test::edge_selection({interior[0], edge}, interior[0]);
        expect_split_rejected_unchanged(fixture.document, selection);
    }
}

TEST(QdrDocumentOperationSplitEdgesTests, DisconnectedVertexFanRejectsWithoutMutation)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    const ElementId shared = fixture.vertex(1U, 1U);
    const ElementId first = add_vertex(fixture.document, {1.0F, 1.0F, 1.0F});
    const ElementId second = add_vertex(fixture.document, {2.0F, 1.0F, 1.0F});
    EXPECT_NE(add_face(fixture.document, std::array<ElementId, 3>{shared, first, second}),
              kInvalidElementId);
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 2U);
    Selection selection = split_edge_selection(edges, edges.front());

    expect_split_rejected_unchanged(fixture.document, selection);
}

TEST(QdrDocumentOperationSplitEdgesTests, IntermediateBoundaryFanWithThreeSectorsRejectsWithoutMutation)
{
    Document document;
    const ElementId center = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId first = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId second = add_vertex(document, {-0.5F, 1.0F, 0.0F});
    const ElementId third = add_vertex(document, {0.5F, 1.0F, 0.0F});
    const ElementId fourth = add_vertex(document, {1.0F, 0.0F, 0.0F});
    ASSERT_NE(add_face(document, std::array<ElementId, 3>{center, second, first}), kInvalidElementId);
    ASSERT_NE(add_face(document, std::array<ElementId, 3>{center, third, second}), kInvalidElementId);
    ASSERT_NE(add_face(document, std::array<ElementId, 3>{center, fourth, third}), kInvalidElementId);
    const Edge first_edge = make_edge(center, second);
    Selection selection = qdr_test::edge_selection(
        {first_edge, make_edge(center, third)}, first_edge);

    expect_split_rejected_unchanged(document, selection);
}

TEST(QdrDocumentOperationSplitEdgesTests, InconsistentIncidentWindingRejectsWithoutMutation)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    std::ranges::reverse(fixture.document.faces[1].vertices);
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 2U);
    Selection selection = split_edge_selection(edges, edges.front());

    expect_split_rejected_unchanged(fixture.document, selection);
}

TEST(QdrDocumentOperationSplitEdgesTests, CoincidentUnselectedVertexIdentitiesRemainIndependent)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    std::array<ElementId, 4> coincident;
    const Face source_face = fixture.document.faces.front();
    for (std::size_t index = 0U; index < coincident.size(); ++index) {
        coincident[index] = add_vertex(fixture.document,
            find_vertex(fixture.document, source_face.vertices[index])->position);
    }
    const ElementId coincident_face = add_face(fixture.document, coincident, 7U);
    const Document source = fixture.document;
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 2U);
    Selection selection = split_edge_selection(edges, edges.front());

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    const Face* retained = find_face(fixture.document, coincident_face);
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained->vertices, std::vector<ElementId>(coincident.begin(), coincident.end()));
    expect_split_incidence(source, fixture.document, edges, selection);
    expect_split_corner_attributes_preserved(source, fixture.document);
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationSplitEdgesTests, UnchangedImportedCollapsedFacesAndLooseVerticesSurvive)
{
    SplitGridFixture fixture = make_split_grid(2U, 2U);
    const ElementId first = add_vertex(fixture.document, {10.0F, 2.0F, 1.0F});
    const ElementId second = add_vertex(fixture.document, {10.0F, 2.0F, 1.0F});
    const ElementId third = add_vertex(fixture.document, {10.0F, 2.0F, 1.0F});
    const ElementId collapsed_face = add_face(fixture.document,
        std::array<ElementId, 3>{first, second, third}, 9U);
    EXPECT_NE(collapsed_face, kInvalidElementId);
    fixture.document.faces.back().uvs = {{1.0F, 2.0F}, {3.0F, 4.0F}, {5.0F, 6.0F}};
    const ElementId loose = add_vertex(fixture.document, {20.0F, 3.0F, 2.0F});
    const Document source = fixture.document;
    const std::vector<Edge> edges = split_grid_chain(fixture, 1U, 2U);
    Selection selection = split_edge_selection(edges, edges.front());

    const OperationResult result = split_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_NE(find_vertex(fixture.document, loose), nullptr);
    const Face* retained = find_face(fixture.document, collapsed_face);
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained->vertices, (std::vector<ElementId>{first, second, third}));
    expect_split_incidence(source, fixture.document, edges, selection);
    expect_split_corner_attributes_preserved(source, fixture.document);
    EXPECT_TRUE(validate_polygon_document(fixture.document,
        PolygonDocumentValidationPolicy::AuthoredTopology).ok());
}
