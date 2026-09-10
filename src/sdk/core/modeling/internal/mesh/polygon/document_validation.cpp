////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document_validation.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

using namespace document_internal;

using EdgeIdPair = std::pair<ElementId, ElementId>;

bool valid_vertex_id(ElementId id, const std::set<ElementId>& vertex_ids)
{
    return id != kInvalidElementId && vertex_ids.contains(id);
}

EdgeIdPair edge_key(Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    return { edge.a, edge.b };
}

void add_diagnostic(
    PolygonDocumentValidationReport& report,
    PolygonDocumentDiagnosticCode code,
    PolygonDocumentDiagnosticSeverity severity,
    std::string message,
    ElementId vertex_id = kInvalidElementId,
    ElementId face_id = kInvalidElementId,
    Edge edge = {},
    std::vector<ElementId> vertex_ids = {})
{
    report.diagnostics.push_back({
        code,
        severity,
        vertex_id,
        face_id,
        edge,
        std::move(vertex_ids),
        std::move(message),
    });
}

std::set<ElementId> collect_unique_vertex_ids(const Document& document, PolygonDocumentValidationReport& report)
{
    std::set<ElementId> ids;
    for (const Vertex& vertex : document.vertices) {
        if (!ids.insert(vertex.id).second) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::DuplicateVertexId,
                PolygonDocumentDiagnosticSeverity::Error,
                "Duplicate vertex id " + std::to_string(vertex.id) + ".",
                vertex.id);
        }
    }
    return ids;
}

void validate_face_ids(const Document& document, PolygonDocumentValidationReport& report)
{
    std::set<ElementId> face_ids;
    for (const Face& face : document.faces) {
        if (!valid_face_texture_transform(face.texture_transform)) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::InvalidFaceTextureTransform,
                PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) +
                    " has invalid texture transform settings.",
                kInvalidElementId, face.id);
        }
        if (!face_ids.insert(face.id).second) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::DuplicateFaceId,
                PolygonDocumentDiagnosticSeverity::Error,
                "Duplicate face id " + std::to_string(face.id) + ".",
                kInvalidElementId,
                face.id);
        }
    }
}

std::vector<ElementId> missing_face_vertex_ids(const Face& face, const std::set<ElementId>& vertex_ids)
{
    std::vector<ElementId> missing_ids;
    for (const ElementId vertex_id : face.vertices) {
        if (!valid_vertex_id(vertex_id, vertex_ids) &&
            std::ranges::find(missing_ids, vertex_id) == missing_ids.end()) {
            missing_ids.push_back(vertex_id);
        }
    }
    return missing_ids;
}

std::vector<ElementId> repeated_face_vertex_ids(const Face& face)
{
    std::set<ElementId> seen;
    std::vector<ElementId> repeated_ids;
    for (const ElementId vertex_id : face.vertices) {
        if (!seen.insert(vertex_id).second &&
            std::ranges::find(repeated_ids, vertex_id) == repeated_ids.end()) {
            repeated_ids.push_back(vertex_id);
        }
    }
    return repeated_ids;
}

bool face_vertices_are_resolved(const Face& face, const std::set<ElementId>& vertex_ids)
{
    return std::ranges::all_of(face.vertices, [&vertex_ids](ElementId vertex_id) {
        return valid_vertex_id(vertex_id, vertex_ids);
    });
}

bool face_loop_is_degenerate(
    const Face& face,
    const VertexLookupIndex& vertex_lookup)
{
    if (face.vertices.size() < 3U) {
        return true;
    }

    quader::QVec3 area_vector;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Vertex* current = vertex_lookup.find(face.vertices[index]);
        const Vertex* next =
            vertex_lookup.find(
                face.vertices[(index + 1U) % face.vertices.size()]);
        if (current == nullptr || next == nullptr) {
            return false;
        }
        area_vector += quader::cross(current->position, next->position);
    }
    return quader::length_squared(area_vector) <= kFaceAreaScoreEpsilon;
}

std::map<EdgeIdPair, int> edge_incidence_counts_for_valid_faces(const Document& document, const std::set<ElementId>& vertex_ids)
{
    std::map<EdgeIdPair, int> counts;
    for (const Face& face : document.faces) {
        if (face.vertices.size() < 2U) {
            continue;
        }

        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            if (edge.a == edge.b ||
                !valid_vertex_id(edge.a, vertex_ids) ||
                !valid_vertex_id(edge.b, vertex_ids)) {
                continue;
            }
            ++counts[edge_key(edge)];
        }
    }
    return counts;
}

void validate_faces(
    const Document& document,
    const std::set<ElementId>& vertex_ids,
    const VertexLookupIndex& vertex_lookup,
    PolygonDocumentValidationReport& report,
    PolygonDocumentValidationPolicy policy)
{
    for (const Face& face : document.faces) {
        const std::vector<ElementId> missing_ids = missing_face_vertex_ids(face, vertex_ids);
        for (const ElementId vertex_id : missing_ids) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::MissingVertexReference,
                PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) + " references missing vertex " + std::to_string(vertex_id) + ".",
                vertex_id,
                face.id,
                {},
                { vertex_id });
        }

        const std::vector<ElementId> repeated_ids = repeated_face_vertex_ids(face);
        for (const ElementId vertex_id : repeated_ids) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::RepeatedFaceVertex,
                PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) + " repeats vertex " + std::to_string(vertex_id) + ".",
                vertex_id,
                face.id,
                {},
                { vertex_id });
        }

        if (!face.uvs.empty() && !face_has_loop_uvs(face)) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::FaceUvCountMismatch,
                PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) + " has " + std::to_string(face.uvs.size()) +
                    " UVs for " + std::to_string(face.vertices.size()) + " vertices.",
                kInvalidElementId,
                face.id,
                {},
                face.vertices);
        }

        const bool resolved = face_vertices_are_resolved(face, vertex_ids);
        if (resolved && valid_face_texture_transform(face.texture_transform) &&
            face.texture_transform != FaceTextureTransform{}) {
            const auto normal = face_normal(document, face);
            for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
                const Vertex *vertex = vertex_lookup.find(face.vertices[corner]);
                const auto base = face_has_loop_uvs(face)
                    ? face.uvs[corner]
                    : generated_texture_uv(vertex->position, normal);
                const auto uv = apply_face_texture_transform(
                    base, face.texture_transform);
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
                    add_diagnostic(
                        report,
                        PolygonDocumentDiagnosticCode::InvalidFaceTextureTransform,
                        PolygonDocumentDiagnosticSeverity::Error,
                        "Face " + std::to_string(face.id) +
                            " texture settings produce non-finite UVs.",
                        kInvalidElementId, face.id);
                    break;
                }
            }
        }
        if (resolved && face_loop_is_degenerate(face, vertex_lookup)) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::DegenerateFaceLoop,
                policy == PolygonDocumentValidationPolicy::AuthoredTopology &&
                        face.vertices.size() >= 3U
                    ? PolygonDocumentDiagnosticSeverity::Warning
                    : PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) + " has a degenerate loop.",
                kInvalidElementId,
                face.id,
                {},
                face.vertices);
        }

        if (resolved && face.vertices.size() >= 3U &&
            triangulate_face_local_indices(
                document,
                face,
                vertex_lookup).empty()) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::TriangulationFailure,
                policy == PolygonDocumentValidationPolicy::AuthoredTopology
                    ? PolygonDocumentDiagnosticSeverity::Warning
                    : PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) + " cannot be triangulated.",
                kInvalidElementId,
                face.id,
                {},
                face.vertices);
        }
    }
}

void validate_nonmanifold_edges(
    const std::map<EdgeIdPair, int>& edge_counts,
    PolygonDocumentValidationReport& report)
{
    for (const auto& [edge, count] : edge_counts) {
        if (count <= 2) {
            continue;
        }
        add_diagnostic(
            report,
            PolygonDocumentDiagnosticCode::NonmanifoldEdge,
            PolygonDocumentDiagnosticSeverity::Error,
            "Edge " + std::to_string(edge.first) + "-" + std::to_string(edge.second) +
                " is referenced by " + std::to_string(count) + " faces.",
            kInvalidElementId,
            kInvalidElementId,
            { edge.first, edge.second },
            { edge.first, edge.second });
    }
}

void validate_normal_edges(
    std::span<const Edge> normal_edges,
    PolygonDocumentDiagnosticCode invalid_code,
    std::string_view label,
    const std::set<ElementId>& vertex_ids,
    const std::map<EdgeIdPair, int>& edge_counts,
    PolygonDocumentValidationReport& report)
{
    std::set<EdgeIdPair> seen;
    for (const Edge& authored_edge : normal_edges) {
        const Edge normalized = make_edge(authored_edge.a, authored_edge.b);
        const bool valid_edge =
            normalized.a != normalized.b &&
            valid_vertex_id(normalized.a, vertex_ids) &&
            valid_vertex_id(normalized.b, vertex_ids) &&
            edge_counts.contains(edge_key(normalized)) &&
            authored_edge == normalized &&
            seen.insert(edge_key(normalized)).second;
        if (valid_edge) {
            continue;
        }

        add_diagnostic(
            report,
            invalid_code,
            PolygonDocumentDiagnosticSeverity::Error,
            std::string(label) + " normal edge " +
                std::to_string(authored_edge.a) + "-" +
                std::to_string(authored_edge.b) +
                " is not a unique canonical live document edge.",
            kInvalidElementId,
            kInvalidElementId,
            authored_edge,
            { authored_edge.a, authored_edge.b });
    }
}

void validate_normal_edge_states(
    const Document& document,
    const std::set<ElementId>& vertex_ids,
    const std::map<EdgeIdPair, int>& edge_counts,
    PolygonDocumentValidationReport& report)
{
    validate_normal_edges(
        document.hard_normal_edges,
        PolygonDocumentDiagnosticCode::InvalidHardNormalEdge,
        "Hard",
        vertex_ids,
        edge_counts,
        report);
    validate_normal_edges(
        document.soft_normal_edges,
        PolygonDocumentDiagnosticCode::InvalidSoftNormalEdge,
        "Soft",
        vertex_ids,
        edge_counts,
        report);

    std::set<EdgeIdPair> hard_edges;
    for (const Edge edge : document.hard_normal_edges) {
        hard_edges.insert(edge_key(edge));
    }
    std::set<EdgeIdPair> reported_conflicts;
    for (const Edge edge : document.soft_normal_edges) {
        const Edge normalized = make_edge(edge.a, edge.b);
        const EdgeIdPair key = edge_key(normalized);
        if (!hard_edges.contains(key) || !reported_conflicts.insert(key).second) {
            continue;
        }
        add_diagnostic(
            report,
            PolygonDocumentDiagnosticCode::ConflictingNormalEdgeState,
            PolygonDocumentDiagnosticSeverity::Error,
            "Normal edge " + std::to_string(normalized.a) + "-" +
                std::to_string(normalized.b) +
                " cannot be both hard and soft.",
            kInvalidElementId,
            kInvalidElementId,
            normalized,
            { normalized.a, normalized.b });
    }
}

void validate_unreferenced_vertices(
    const Document& document,
    const std::set<ElementId>& vertex_ids,
    PolygonDocumentValidationReport& report)
{
    std::set<ElementId> referenced_ids;
    for (const Face& face : document.faces) {
        for (const ElementId vertex_id : face.vertices) {
            if (valid_vertex_id(vertex_id, vertex_ids)) {
                referenced_ids.insert(vertex_id);
            }
        }
    }

    for (const Vertex& vertex : document.vertices) {
        if (referenced_ids.contains(vertex.id)) {
            continue;
        }
        add_diagnostic(
            report,
            PolygonDocumentDiagnosticCode::UnreferencedVertex,
            PolygonDocumentDiagnosticSeverity::Warning,
            "Vertex " + std::to_string(vertex.id) + " is not referenced by any face.",
            vertex.id,
            kInvalidElementId,
            {},
            { vertex.id });
    }
}

} // namespace

bool PolygonDocumentValidationReport::has_errors() const
{
    return std::ranges::any_of(diagnostics, [](const PolygonDocumentDiagnostic& diagnostic) {
        return diagnostic.severity == PolygonDocumentDiagnosticSeverity::Error;
    });
}

bool PolygonDocumentValidationReport::ok() const
{
    return !has_errors();
}

PolygonDocumentValidationReport validate_polygon_document(
    const Document& document,
    PolygonDocumentValidationPolicy policy)
{
    PolygonDocumentValidationReport report;
    const VertexLookupIndex vertex_lookup(document);
    const std::set<ElementId> vertex_ids = collect_unique_vertex_ids(document, report);
    validate_face_ids(document, report);
    validate_faces(document, vertex_ids, vertex_lookup, report, policy);
    const std::map<EdgeIdPair, int> edge_counts = edge_incidence_counts_for_valid_faces(document, vertex_ids);
    validate_nonmanifold_edges(edge_counts, report);
    validate_normal_edge_states(document, vertex_ids, edge_counts, report);
    validate_unreferenced_vertices(document, vertex_ids, report);
    return report;
}

} // namespace quader_poly
