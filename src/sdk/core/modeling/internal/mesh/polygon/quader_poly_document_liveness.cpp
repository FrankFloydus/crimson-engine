////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <cstdint>

namespace quader_poly {
namespace {

constexpr std::uint64_t kRevisionOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kRevisionPrime = 1099511628211ULL;

bool edge_less(Edge left, Edge right)
{
    left = make_edge(left.a, left.b);
    right = make_edge(right.a, right.b);
    return left.a < right.a || (left.a == right.a && left.b < right.b);
}

void append_revision_value(std::uint64_t& revision, std::uint64_t value)
{
    for (int byte_index = 0; byte_index < 8; ++byte_index) {
        revision ^= (value >> (byte_index * 8)) & 0xffU;
        revision *= kRevisionPrime;
    }
}

void append_revision_edge(std::uint64_t& revision, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    append_revision_value(revision, edge.a);
    append_revision_value(revision, edge.b);
}

void sort_unique_ids(std::vector<ElementId>& ids)
{
    std::erase(ids, kInvalidElementId);
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
}

void sort_unique_edges(std::vector<Edge>& edges)
{
    for (Edge& edge : edges) {
        edge = make_edge(edge.a, edge.b);
    }
    std::erase_if(edges, [](Edge edge) {
        return edge.a == kInvalidElementId || edge.b == kInvalidElementId || edge.a == edge.b;
    });
    std::ranges::sort(edges, edge_less);
    edges.erase(std::ranges::unique(edges).begin(), edges.end());
}

void append_issue(
    PolygonSelectionLivenessReport& report,
    PolygonSelectionLivenessIssueCode code,
    ElementKind kind,
    ElementId element_id,
    Edge edge = {})
{
    report.issues.push_back({
        .code = code,
        .kind = kind,
        .element_id = element_id,
        .edge = make_edge(edge.a, edge.b),
    });
}

void append_missing_edge_endpoint_issues(
    PolygonSelectionLivenessReport& report,
    const PolygonDocumentLiveness& liveness,
    PolygonSelectionLivenessIssueCode code,
    Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (!liveness.contains_vertex(edge.a)) {
        append_issue(report, code, ElementKind::Edge, edge.a, edge);
    }
    if (edge.b != edge.a && !liveness.contains_vertex(edge.b)) {
        append_issue(report, code, ElementKind::Edge, edge.b, edge);
    }
}

bool edge_endpoints_are_live(const PolygonDocumentLiveness& liveness, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    return liveness.contains_vertex(edge.a) && liveness.contains_vertex(edge.b);
}

} // namespace

bool PolygonSelectionLivenessReport::ok() const
{
    return issues.empty();
}

bool PolygonDocumentLiveness::contains_vertex(ElementId id) const
{
    return id != kInvalidElementId && std::ranges::binary_search(live_vertices, id);
}

bool PolygonDocumentLiveness::contains_face(ElementId id) const
{
    return id != kInvalidElementId && std::ranges::binary_search(live_faces, id);
}

bool PolygonDocumentLiveness::contains_edge(Edge edge) const
{
    edge = make_edge(edge.a, edge.b);
    return edge.a != kInvalidElementId && edge.b != kInvalidElementId && edge.a != edge.b &&
           std::ranges::binary_search(live_edges, edge, edge_less);
}

PolygonDocumentLiveness build_polygon_document_liveness(const Document& document)
{
    PolygonDocumentLiveness liveness;
    liveness.live_vertices.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
        liveness.live_vertices.push_back(vertex.id);
    }
    sort_unique_ids(liveness.live_vertices);

    liveness.live_faces.reserve(document.faces.size());
    for (const Face& face : document.faces) {
        liveness.live_faces.push_back(face.id);
    }
    sort_unique_ids(liveness.live_faces);

    liveness.live_edges = document_edges(document);
    sort_unique_edges(liveness.live_edges);

    std::uint64_t revision = kRevisionOffsetBasis;
    append_revision_value(revision, document.next_vertex_id);
    append_revision_value(revision, document.next_face_id);
    for (const ElementId vertex_id : liveness.live_vertices) {
        append_revision_value(revision, vertex_id);
    }
    for (const Face& face : document.faces) {
        append_revision_value(revision, face.id);
        append_revision_value(revision, face.material_slot);
        append_revision_value(revision, static_cast<std::uint64_t>(face.normal_shading));
        for (const ElementId vertex_id : face.vertices) {
            append_revision_value(revision, vertex_id);
        }
    }
    for (Edge edge : document.hard_normal_edges) {
        append_revision_edge(revision, edge);
    }
    liveness.revision = revision;
    return liveness;
}

PolygonSelectionLivenessReport validate_polygon_selection_liveness(
    const Document& document,
    const Selection& selection)
{
    return validate_polygon_selection_liveness(build_polygon_document_liveness(document), selection);
}

PolygonSelectionLivenessReport validate_polygon_selection_liveness(
    const PolygonDocumentLiveness& liveness,
    const Selection& selection)
{
    PolygonSelectionLivenessReport report;

    for (const ElementId vertex_id : selection.vertices) {
        if (!liveness.contains_vertex(vertex_id)) {
            append_issue(
                report,
                PolygonSelectionLivenessIssueCode::MissingSelectedVertex,
                ElementKind::Vertex,
                vertex_id);
        }
    }

    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        append_missing_edge_endpoint_issues(
            report,
            liveness,
            PolygonSelectionLivenessIssueCode::MissingSelectedEdgeEndpoint,
            edge);
        if (edge_endpoints_are_live(liveness, edge) && !liveness.contains_edge(edge)) {
            append_issue(
                report,
                PolygonSelectionLivenessIssueCode::MissingSelectedEdge,
                ElementKind::Edge,
                kInvalidElementId,
                edge);
        }
    }

    for (const ElementId face_id : selection.faces) {
        if (!liveness.contains_face(face_id)) {
            append_issue(
                report,
                PolygonSelectionLivenessIssueCode::MissingSelectedFace,
                ElementKind::Face,
                face_id);
        }
    }

    if (!selection.has_active) {
        return report;
    }

    switch (selection.active_kind) {
    case ElementKind::Vertex:
        if (!liveness.contains_vertex(selection.active_vertex)) {
            append_issue(
                report,
                PolygonSelectionLivenessIssueCode::MissingActiveVertex,
                ElementKind::Vertex,
                selection.active_vertex);
        }
        break;
    case ElementKind::Edge:
        append_missing_edge_endpoint_issues(
            report,
            liveness,
            PolygonSelectionLivenessIssueCode::MissingActiveEdgeEndpoint,
            selection.active_edge);
        if (edge_endpoints_are_live(liveness, selection.active_edge) &&
            !liveness.contains_edge(selection.active_edge)) {
            append_issue(
                report,
                PolygonSelectionLivenessIssueCode::MissingActiveEdge,
                ElementKind::Edge,
                kInvalidElementId,
                selection.active_edge);
        }
        break;
    case ElementKind::Face:
        if (!liveness.contains_face(selection.active_face)) {
            append_issue(
                report,
                PolygonSelectionLivenessIssueCode::MissingActiveFace,
                ElementKind::Face,
                selection.active_face);
        }
        break;
    }

    return report;
}

} // namespace quader_poly
