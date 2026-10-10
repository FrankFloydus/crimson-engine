////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

OperationResult connect_edge_on_first_valid_face(
    const Document& source, Document& candidate, Edge edge,
    std::span<const ElementId> selected_vertices)
{
    if (edge_exists(candidate, edge)) {
        return {};
    }
    OperationResult failure = OperationResult::failure(
        "Connect Vertices needs chords inside the authored face boundary.");
    for (std::size_t face_index = 0U; face_index < candidate.faces.size(); ++face_index) {
        const Face& face = candidate.faces[face_index];
        const auto first_corner = loop_vertex_index(face.vertices, edge.a);
        const auto second_corner = loop_vertex_index(face.vertices, edge.b);
        if (!first_corner || !second_corner ||
            !authored_face_chord_is_valid(candidate, face, edge.a, edge.b)) {
            continue;
        }
        const std::size_t first = std::min(*first_corner, *second_corner);
        const std::size_t second = std::max(*first_corner, *second_corner);
        std::vector<ElementId> retained_loop = face_vertices_between(face, first, second);
        std::vector<ElementId> added_loop = face_vertices_between(face, second, first);
        const auto selected_count = [&](const std::vector<ElementId>& loop) {
            return std::ranges::count_if(loop, [&](ElementId vertex_id) {
                return contains_id(selected_vertices, vertex_id);
            });
        };
        const bool preserves_center = selected_count(face.vertices) > 2;
        if (preserves_center) {
            // Peeling consecutive selected corners keeps the center polygon's identity.
            if (selected_count(added_loop) > selected_count(retained_loop)) {
                retained_loop.swap(added_loop);
            }
            const Face* original_face = find_face(source, face.id);
            if (original_face != nullptr) {
                for (const ElementId vertex_id : original_face->vertices) {
                    const auto corner = std::ranges::find(retained_loop, vertex_id);
                    if (contains_id(selected_vertices, vertex_id) && corner != retained_loop.end()) {
                        std::rotate(retained_loop.begin(), corner, retained_loop.end());
                        break;
                    }
                }
            }
        }

        Document attempt = candidate;
        Face retained_face = face;
        Face added_face = face;
        retained_face.vertices = std::move(retained_loop);
        added_face.vertices = std::move(added_loop);
        added_face.id = attempt.next_face_id++;
        if (retained_face.vertices.size() < 3U || added_face.vertices.size() < 3U ||
            !preserve_face_uvs(candidate, face, attempt, retained_face) ||
            !preserve_face_uvs(candidate, face, attempt, added_face)) {
            failure = OperationResult::failure("Connect Vertices encountered invalid face data.");
            continue;
        }
        attempt.faces[face_index] = std::move(retained_face);
        std::size_t insertion_index = face_index + 1U;
        while (insertion_index < attempt.faces.size() &&
            find_face(source, attempt.faces[insertion_index].id) == nullptr) {
            ++insertion_index;
        }
        attempt.faces.insert(attempt.faces.begin() + static_cast<std::ptrdiff_t>(insertion_index),
            std::move(added_face));
        const OperationResult validation = validate_authored_operation_candidate(candidate, attempt);
        if (!validation.success) {
            failure = validation;
            continue;
        }
        candidate = std::move(attempt);
        return {true, {}};
    }
    return failure;
}

/**
 * Implements the Connect Vertices Operation modeling operation for the polygon document and mesh editing core.
 */
class ConnectVerticesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ConnectSelectedVertices).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ConnectSelectedVertices).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult ConnectVerticesOperation::apply(Document& document, Selection& selection) const
{
    if (selection.mode != SelectionMode::Vertex) {
        return OperationResult::failure("Connect needs vertex selection mode.");
    }
    const OperationResult source_validation = validate_authored_operation_candidate(document, document);
    if (!source_validation.success) {
        return source_validation;
    }
    const std::vector<ElementId> selected_vertices = selected_valid_vertices(document, selection);
    if (selected_vertices.size() < 2U) {
        return OperationResult::failure("Select at least two vertices to connect.");
    }

    std::vector<Edge> requested_chords;
    std::vector<Edge> connected_edges;
    bool common_face_found = false;
    for (const Face& face : document.faces) {
        std::vector<std::size_t> selected_indices;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            if (contains_id(selected_vertices, face.vertices[index])) {
                selected_indices.push_back(index);
            }
        }
        if (selected_indices.size() < 2U) {
            continue;
        }
        common_face_found = true;
        std::vector<Edge> face_connectors;
        const std::size_t connector_count = selected_indices.size() == 2U ? 1U : selected_indices.size();
        bool has_new_chord = false;
        for (std::size_t index = 0; index < connector_count; ++index) {
            const std::size_t first = selected_indices[index];
            const std::size_t second = selected_indices[(index + 1U) % selected_indices.size()];
            const Edge connector = make_edge(face.vertices[first], face.vertices[second]);
            const bool adjacent = (first + 1U) % face.vertices.size() == second ||
                (second + 1U) % face.vertices.size() == first;
            if (!adjacent && !edge_exists(document, connector)) {
                add_unique_edge(requested_chords, connector);
                has_new_chord = true;
            }
            add_unique_edge(face_connectors, connector);
        }
        if (!has_new_chord) {
            continue;
        }
        for (const Edge edge : face_connectors) {
            add_unique_edge(connected_edges, edge);
        }
    }
    if (connected_edges.empty()) {
        return OperationResult::failure(common_face_found
            ? "Connect Vertices found only existing face edges."
            : "Connect needs selected vertices on the same authored face.");
    }
    Document candidate = document;
    for (const Edge edge : requested_chords) {
        const OperationResult result = connect_edge_on_first_valid_face(
            document, candidate, edge, selected_vertices);
        if (!result.success) {
            return result;
        }
    }
    prune_normal_edge_states(candidate);
    const OperationResult validation = validate_authored_operation_candidate(document, candidate);
    if (!validation.success) {
        return validation;
    }
    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Edge;
    selection.edges = std::move(connected_edges);
    activate_last_selection(selection);
    return {true, {}};
}
} // namespace

OperationResult connect_selected_vertices(Document& document, Selection& selection)
{
    return ConnectVerticesOperation().apply(document, selection);
}

} // namespace quader_poly
