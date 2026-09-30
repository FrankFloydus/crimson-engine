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

    Document candidate = document;
    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(document.faces.size() + selected_vertices.size());
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
            rebuilt_faces.push_back(face);
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
            if (!adjacent) {
                if (!authored_face_chord_is_valid(document, face, connector.a, connector.b)) {
                    return OperationResult::failure("Connect Vertices needs chords inside the authored face boundary.");
                }
                has_new_chord = true;
            }
            add_unique_edge(face_connectors, connector);
        }
        if (!has_new_chord) {
            rebuilt_faces.push_back(face);
            continue;
        }
        const auto append_replacement = [&](std::vector<ElementId> loop, ElementId face_id) -> bool {
            Face replacement = face;
            replacement.id = face_id;
            replacement.vertices = std::move(loop);
            if (replacement.vertices.size() < 3U ||
                !preserve_face_uvs(document, face, candidate, replacement)) {
                return false;
            }
            rebuilt_faces.push_back(std::move(replacement));
            return true;
        };
        if (selected_indices.size() == 2U) {
            if (!append_replacement(face_vertices_between(face, selected_indices[0], selected_indices[1]), face.id) ||
                !append_replacement(face_vertices_between(face, selected_indices[1], selected_indices[0]), candidate.next_face_id++)) {
                return OperationResult::failure("Connect Vertices encountered invalid face data.");
            }
        } else {
            std::vector<ElementId> center_loop;
            for (const std::size_t index : selected_indices) {
                center_loop.push_back(face.vertices[index]);
            }
            if (!append_replacement(std::move(center_loop), face.id)) {
                return OperationResult::failure("Connect Vertices encountered invalid face data.");
            }
            for (std::size_t index = 0; index < selected_indices.size(); ++index) {
                const std::size_t first = selected_indices[index];
                const std::size_t second = selected_indices[(index + 1U) % selected_indices.size()];
                std::vector<ElementId> side_loop = face_vertices_between(face, first, second);
                if (side_loop.size() >= 3U && !append_replacement(std::move(side_loop), candidate.next_face_id++)) {
                    return OperationResult::failure("Connect Vertices encountered invalid face data.");
                }
            }
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
    candidate.faces = std::move(rebuilt_faces);
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
