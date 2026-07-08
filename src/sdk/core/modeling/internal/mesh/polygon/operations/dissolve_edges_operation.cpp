////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

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
 * Implements the Dissolve Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class DissolveEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::DissolveSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::DissolveSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult DissolveEdgesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
    return {false, "Select one or more edges to dissolve."};
  }

    Document candidate = document;
    std::vector<ElementId> merged_face_ids;
    std::vector<Edge> processed_edges;
    bool changed = false;

    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b || contains_edge(processed_edges, edge)) {
          continue;
        }

        std::vector<std::size_t> adjacent_faces;
        adjacent_faces.reserve(2);
        for (std::size_t face_index = 0; face_index < candidate.faces.size(); ++face_index) {
            if (oriented_edge_in_face(candidate.faces[face_index], edge).has_value()) {
                adjacent_faces.push_back(face_index);
            }
        }

        if (adjacent_faces.size() != 2) {
            return { false, "Dissolve needs selected interior edges with exactly two adjacent faces." };
        }

        const std::size_t first_index = adjacent_faces[0];
        const std::size_t second_index = adjacent_faces[1];
        const Face first_face = candidate.faces[first_index];
        Face second_face = candidate.faces[second_index];
        const std::optional<std::pair<ElementId, ElementId>> first_edge = oriented_edge_in_face(first_face, edge);
        if (!first_edge.has_value()) {
            return { false, "Selected edge was not found." };
        }

        const ElementId from_id = first_edge->first;
        const ElementId to_id = first_edge->second;
        if (!directed_face_edge_index(second_face, to_id, from_id).has_value()) {
            if (!directed_face_edge_index(second_face, from_id, to_id).has_value()) {
                return { false, "Selected edge adjacency is invalid." };
            }
            std::ranges::reverse(second_face.vertices);
            if (second_face.uvs.size() == second_face.vertices.size()) {
                std::ranges::reverse(second_face.uvs);
            } else {
                second_face.uvs.clear();
            }
        }

        std::vector<ElementId> merged_loop = merged_face_loop_for_dissolved_edge(first_face, second_face, from_id, to_id);
        if (merged_loop.size() < 3) {
            return { false, "Dissolve would create invalid face geometry." };
        }

        Face merged_face = first_face;
        merged_face.vertices = std::move(merged_loop);
        merged_face.uvs.clear();
        orient_face_toward_normal(candidate, merged_face, face_normal(candidate, first_face) + face_normal(candidate, second_face));

        Document next_candidate = candidate;
        next_candidate.faces[first_index] = std::move(merged_face);
        next_candidate.faces.erase(next_candidate.faces.begin() + static_cast<std::ptrdiff_t>(second_index));
        [[maybe_unused]] const bool removed_first_endpoint = remove_redundant_vertex_from_all_face_loops(next_candidate, edge.a);
        [[maybe_unused]] const bool removed_second_endpoint = remove_redundant_vertex_from_all_face_loops(next_candidate, edge.b);
        prune_invalid_faces(next_candidate);
        prune_unused_vertices(next_candidate);
        if (!every_face_triangulates(next_candidate)) {
            return { false, "Dissolve would create invalid face geometry." };
        }

        add_unique_id(merged_face_ids, first_face.id);
        add_unique_edge(processed_edges, edge);
        candidate = std::move(next_candidate);
        changed = true;
    }

    if (!changed || merged_face_ids.empty()) {
        return { false, "No dissolvable selected edges were found." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(merged_face_ids);
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

OperationResult dissolve_selected_edges(Document& document, Selection& selection)
{
    return DissolveEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
