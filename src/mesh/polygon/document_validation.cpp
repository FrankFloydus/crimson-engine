////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document_validation.hpp>

#include <mesh/polygon/internal/quader_poly_document_constants.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <ranges>
#include <set>
#include <string>
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

bool face_loop_is_degenerate(const Document& document, const Face& face)
{
    if (face.vertices.size() < 3U) {
        return true;
    }

    quader::QVec3 area_vector;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Vertex* current = find_vertex(document, face.vertices[index]);
        const Vertex* next = find_vertex(document, face.vertices[(index + 1U) % face.vertices.size()]);
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

void validate_faces(const Document& document, const std::set<ElementId>& vertex_ids, PolygonDocumentValidationReport& report)
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
        if (resolved && face_loop_is_degenerate(document, face)) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::DegenerateFaceLoop,
                PolygonDocumentDiagnosticSeverity::Error,
                "Face " + std::to_string(face.id) + " has a degenerate loop.",
                kInvalidElementId,
                face.id,
                {},
                face.vertices);
        }

        if (resolved && face.vertices.size() >= 3U &&
            triangulate_face_local_indices(document, face).empty()) {
            add_diagnostic(
                report,
                PolygonDocumentDiagnosticCode::TriangulationFailure,
                PolygonDocumentDiagnosticSeverity::Error,
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

void validate_hard_normal_edges(
    const Document& document,
    const std::set<ElementId>& vertex_ids,
    const std::map<EdgeIdPair, int>& edge_counts,
    PolygonDocumentValidationReport& report)
{
    static_cast<void>(document);
    for (const Edge& hard_edge : document.hard_normal_edges) {
        const Edge normalized = make_edge(hard_edge.a, hard_edge.b);
        const bool valid_edge =
            normalized.a != normalized.b &&
            valid_vertex_id(normalized.a, vertex_ids) &&
            valid_vertex_id(normalized.b, vertex_ids) &&
            edge_counts.contains(edge_key(normalized));
        if (valid_edge) {
            continue;
        }

        add_diagnostic(
            report,
            PolygonDocumentDiagnosticCode::InvalidHardNormalEdge,
            PolygonDocumentDiagnosticSeverity::Error,
            "Hard normal edge " + std::to_string(hard_edge.a) + "-" + std::to_string(hard_edge.b) +
                " is not a valid document edge.",
            kInvalidElementId,
            kInvalidElementId,
            hard_edge,
            { hard_edge.a, hard_edge.b });
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

PolygonDocumentValidationReport validate_polygon_document(const Document& document)
{
    PolygonDocumentValidationReport report;
    const std::set<ElementId> vertex_ids = collect_unique_vertex_ids(document, report);
    validate_face_ids(document, report);
    validate_faces(document, vertex_ids, report);
    const std::map<EdgeIdPair, int> edge_counts = edge_incidence_counts_for_valid_faces(document, vertex_ids);
    validate_nonmanifold_edges(edge_counts, report);
    validate_hard_normal_edges(document, vertex_ids, edge_counts, report);
    validate_unreferenced_vertices(document, vertex_ids, report);
    return report;
}

} // namespace quader_poly
