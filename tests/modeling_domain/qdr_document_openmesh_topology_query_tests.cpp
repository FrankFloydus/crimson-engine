////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_openmesh_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_backend.hpp>

#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {

using quader_poly::add_face;
using quader_poly::add_vertex;
using quader_poly::CubePrimitiveSpec;
using quader_poly::Document;
using quader_poly::document_edges;
using quader_poly::Edge;
using quader_poly::ElementId;
using quader_poly::Face;
using quader_poly::kInvalidElementId;
using quader_poly::make_cube;
using quader_poly::make_edge;
using quader_poly::Vertex;
using quader_poly::document_internal::build_openmesh_topology_query;
using quader_poly::document_internal::build_topology_query;
using quader_poly::document_internal::DocumentTopologyQuery;

std::pair<ElementId, ElementId> edge_key(Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    return { edge.a, edge.b };
}

bool contains_edge(const std::vector<Edge>& edges, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    return std::ranges::find(edges, edge) != edges.end();
}

void require_topology_queries_match(const DocumentTopologyQuery& neutral, const DocumentTopologyQuery& shim)
{
    EXPECT_TRUE(shim.edge_incidence_counts == neutral.edge_incidence_counts);
    EXPECT_TRUE(shim.face_indices_by_edge == neutral.face_indices_by_edge);
    EXPECT_TRUE(shim.adjacent_vertices_by_vertex == neutral.adjacent_vertices_by_vertex);
    EXPECT_TRUE(shim.face_indices_by_vertex == neutral.face_indices_by_vertex);
    EXPECT_TRUE(shim.boundary_edges == neutral.boundary_edges);
    EXPECT_TRUE(shim.closed_edges == neutral.closed_edges);
    EXPECT_TRUE(shim.nonmanifold_edges == neutral.nonmanifold_edges);
}

DocumentTopologyQuery require_topology_query(const Document& document)
{
    DocumentTopologyQuery neutral_query;
    std::string neutral_message;
    const bool neutral_ok = build_topology_query(document, neutral_query, neutral_message);
    SCOPED_TRACE(::testing::Message() << neutral_message);
    EXPECT_TRUE(neutral_ok);

    DocumentTopologyQuery shim_query;
    std::string shim_message;
    const bool shim_ok = build_openmesh_topology_query(document, shim_query, shim_message);
    SCOPED_TRACE(::testing::Message() << shim_message);
    EXPECT_TRUE(shim_ok);
    EXPECT_TRUE(shim_message == neutral_message);
    require_topology_queries_match(neutral_query, shim_query);

    return neutral_query;
}

Document make_single_quad_document()
{
    Document document;
    const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
    const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
    const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
    const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
    EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                       bottom_left, bottom_right, top_right,
                                       top_left}) != kInvalidElementId);
    return document;
}

Document make_three_faces_sharing_one_edge()
{
    Document document;
    const ElementId shared_a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId shared_b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId top = add_vertex(document, { 0.5F, 1.0F, 0.0F });
    const ElementId bottom = add_vertex(document, { 0.5F, -1.0F, 0.0F });
    const ElementId raised = add_vertex(document, { 0.5F, 0.0F, 1.0F });

    EXPECT_TRUE(
        add_face(document, std::array<ElementId, 3>{shared_a, shared_b, top}) !=
        kInvalidElementId);
    EXPECT_TRUE(add_face(document, std::array<ElementId, 3>{shared_b, shared_a,
                                                            bottom}) !=
                kInvalidElementId);
    EXPECT_TRUE(add_face(document, std::array<ElementId, 3>{shared_a, shared_b,
                                                            raised}) !=
                kInvalidElementId);
    return document;
}

void require_edge_count(const DocumentTopologyQuery& query, Edge edge, int expected)
{
    const auto key = edge_key(edge);
    const auto found = query.edge_incidence_counts.find(key);
    EXPECT_TRUE(found != query.edge_incidence_counts.end());
    EXPECT_TRUE(found->second == expected);
}

} // namespace

TEST(QdrDocumentOpenmeshTopologyQueryTests, NeutralTopologyQueryCharacterizesAClosedCubeManifold) {
    const Document document = make_cube(CubePrimitiveSpec {});
    const std::vector<Edge> edges = document_edges(document);
    EXPECT_TRUE(edges.size() == 12U);

    const DocumentTopologyQuery query = require_topology_query(document);

    EXPECT_TRUE(query.edge_incidence_counts.size() == edges.size());
    EXPECT_TRUE(query.face_indices_by_edge.size() == edges.size());
    EXPECT_TRUE(query.boundary_edges.empty());
    EXPECT_TRUE(query.closed_edges.size() == edges.size());
    EXPECT_TRUE(query.nonmanifold_edges.empty());

    for (const Edge edge : edges) {
        require_edge_count(query, edge, 2);
        EXPECT_TRUE(contains_edge(query.closed_edges, edge));

        const auto faces = query.face_indices_by_edge.find(edge_key(edge));
        EXPECT_TRUE(faces != query.face_indices_by_edge.end());
        EXPECT_TRUE(faces->second.size() == 2U);
    }

    for (const Vertex& vertex : document.vertices) {
        const auto adjacent = query.adjacent_vertices_by_vertex.find(vertex.id);
        EXPECT_TRUE(adjacent != query.adjacent_vertices_by_vertex.end());
        EXPECT_TRUE(adjacent->second.size() == 3U);

        const auto faces = query.face_indices_by_vertex.find(vertex.id);
        EXPECT_TRUE(faces != query.face_indices_by_vertex.end());
        EXPECT_TRUE(faces->second.size() == 3U);
    }
}

TEST(QdrDocumentOpenmeshTopologyQueryTests, NeutralTopologyQueryCharacterizesAnOpenQuadBoundary) {
    const Document document = make_single_quad_document();
    const std::vector<Edge> edges = document_edges(document);
    EXPECT_TRUE(edges.size() == 4U);

    const DocumentTopologyQuery query = require_topology_query(document);

    EXPECT_TRUE(query.edge_incidence_counts.size() == edges.size());
    EXPECT_TRUE(query.face_indices_by_edge.size() == edges.size());
    EXPECT_TRUE(query.boundary_edges.size() == edges.size());
    EXPECT_TRUE(query.closed_edges.empty());
    EXPECT_TRUE(query.nonmanifold_edges.empty());

    for (const Edge edge : edges) {
        require_edge_count(query, edge, 1);
        EXPECT_TRUE(contains_edge(query.boundary_edges, edge));

        const auto faces = query.face_indices_by_edge.find(edge_key(edge));
        EXPECT_TRUE(faces != query.face_indices_by_edge.end());
        EXPECT_TRUE(faces->second.size() == 1U);
        EXPECT_TRUE(faces->second.front() == 0U);
    }

    for (const Vertex& vertex : document.vertices) {
        const auto adjacent = query.adjacent_vertices_by_vertex.find(vertex.id);
        EXPECT_TRUE(adjacent != query.adjacent_vertices_by_vertex.end());
        EXPECT_TRUE(adjacent->second.size() == 2U);

        const auto faces = query.face_indices_by_vertex.find(vertex.id);
        EXPECT_TRUE(faces != query.face_indices_by_vertex.end());
        EXPECT_TRUE(faces->second.size() == 1U);
        EXPECT_TRUE(faces->second.front() == 0U);
    }
}

TEST(QdrDocumentOpenmeshTopologyQueryTests, NeutralTopologyQueryRejectsASharedEdgeWithThreeIncidentFaces) {
    const Document document = make_three_faces_sharing_one_edge();

    DocumentTopologyQuery query;
    std::string message;
    const bool ok = build_topology_query(document, query, message);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(message.empty());
    EXPECT_TRUE(query.edge_incidence_counts.empty());
    EXPECT_TRUE(query.face_indices_by_edge.empty());
    EXPECT_TRUE(query.boundary_edges.empty());
    EXPECT_TRUE(query.closed_edges.empty());
    EXPECT_TRUE(query.nonmanifold_edges.empty());

    DocumentTopologyQuery shim_query;
    std::string shim_message;
    const bool shim_ok = build_openmesh_topology_query(document, shim_query, shim_message);

    EXPECT_FALSE(shim_ok);
    EXPECT_TRUE(shim_message == message);
    require_topology_queries_match(query, shim_query);
}
