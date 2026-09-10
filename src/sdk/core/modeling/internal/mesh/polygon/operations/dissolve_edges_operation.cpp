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
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

using EdgeKey = std::pair<ElementId, ElementId>;
using VertexNeighbors = std::map<ElementId, std::vector<ElementId>>;

/// Canonical keys let the original edge tags survive the face joins.
EdgeKey edge_key(ElementId first, ElementId second)
{
    const Edge edge = make_edge(first, second);
    return {edge.a, edge.b};
}

/// Derive vertex degree from authored edges, not face-corner multiplicity.
VertexNeighbors vertex_neighbors(const Document& document)
{
    VertexNeighbors neighbors;
    for (const Edge edge : document_edges(document)) {
        neighbors[edge.a].push_back(edge.b);
        neighbors[edge.b].push_back(edge.a);
    }
    return neighbors;
}

/// Follow a degree-two chain and optionally extend the original dissolve tags.
std::optional<ElementId> chain_endpoint(
    const VertexNeighbors& neighbors, ElementId previous, ElementId current,
    std::set<EdgeKey>* tagged_edges = nullptr)
{
    std::set<ElementId> visited;
    while (true) {
        const auto found = neighbors.find(current);
        if (found == neighbors.end() || found->second.size() != 2U) {
            return current;
        }
        if (!visited.insert(current).second) {
            return std::nullopt;
        }
        const ElementId next = found->second[0] == previous ? found->second[1] : found->second[0];
        if (tagged_edges != nullptr) {
            tagged_edges->insert(edge_key(current, next));
        }
        previous = current;
        current = next;
    }
}

/// Capture Blender's endpoint eligibility before any selected edge is removed.
std::set<ElementId> dissolve_vertex_candidates(
    const Document& document, std::span<const Edge> edges,
    const std::map<EdgeKey, std::vector<std::size_t>>& adjacent_faces)
{
    const VertexNeighbors neighbors = vertex_neighbors(document);
    std::set<EdgeKey> tagged_edges;
    for (const Edge edge : edges) {
        tagged_edges.insert(edge_key(edge.a, edge.b));
    }
    for (const Edge edge : edges) {
        chain_endpoint(neighbors, edge.b, edge.a, &tagged_edges);
        chain_endpoint(neighbors, edge.a, edge.b, &tagged_edges);
    }

    std::set<ElementId> candidates;
    const auto tagged_count = [&](ElementId vertex_id) {
        const auto found = neighbors.find(vertex_id);
        return found == neighbors.end() ? 0U : static_cast<unsigned>(std::ranges::count_if(
            found->second, [&](ElementId neighbor) { return tagged_edges.contains(edge_key(vertex_id, neighbor)); }));
    };
    for (const Edge edge : edges) {
        const auto& faces = adjacent_faces.at(edge_key(edge.a, edge.b));
        for (const ElementId endpoint : std::array{edge.a, edge.b}) {
            if (document.faces[faces[0]].vertices.size() == 3U &&
                document.faces[faces[1]].vertices.size() == 3U && tagged_count(endpoint) == 1U) {
                continue;
            }
            const auto vertex_id = chain_endpoint(neighbors, endpoint == edge.a ? edge.b : edge.a, endpoint);
            if (!vertex_id.has_value() || candidates.contains(*vertex_id)) {
                continue;
            }
            bool incidental_contact = false;
            if (tagged_count(*vertex_id) != 1U) {
                for (const Face& face : document.faces) {
                    const auto corner = loop_vertex_index(face.vertices, *vertex_id);
                    if (corner.has_value() &&
                        !tagged_edges.contains(edge_key(*vertex_id, face.vertices[(*corner + face.vertices.size() - 1U) % face.vertices.size()])) &&
                        !tagged_edges.contains(edge_key(*vertex_id, face.vertices[(*corner + 1U) % face.vertices.size()]))) {
                        incidental_contact = true;
                        break;
                    }
                }
            }
            if (!incidental_contact) {
                candidates.insert(*vertex_id);
            }
        }
    }
    return candidates;
}

/// A face pair can share a whole subdivided edge chain after earlier joins.
bool shared_chain_endpoints(
    const Document& document, const Face& first, const Face& second, Edge selected,
    ElementId& from_id, ElementId& to_id, std::vector<Edge>& removed_edges)
{
    const auto oriented = oriented_edge_in_face(first, selected);
    if (!oriented.has_value()) {
        return false;
    }
    const std::size_t count = first.vertices.size();
    const auto edge_at = [&](std::size_t index) {
        return make_edge(first.vertices[index], first.vertices[(index + 1U) % count]);
    };
    const auto shared = [&](std::size_t index) { return oriented_edge_in_face(second, edge_at(index)).has_value(); };
    std::size_t start = *directed_face_edge_index(first, oriented->first, oriented->second);
    std::size_t length = 1U;
    while (length < count && shared((start + count - 1U) % count)) {
        start = (start + count - 1U) % count;
        ++length;
    }
    while (length < count && shared((start + length) % count)) {
        ++length;
    }
    if (length == count) {
        return false;
    }
    for (std::size_t offset = length; offset < count; ++offset) {
        if (shared((start + offset) % count)) {
            return false;
        }
    }
    const VertexNeighbors neighbors = vertex_neighbors(document);
    for (std::size_t offset = 1U; offset < length; ++offset) {
        if (neighbors.at(first.vertices[(start + offset) % count]).size() != 2U) {
            return false;
        }
    }
    const auto adjacency = face_indices_by_edge(document);
    for (std::size_t offset = 0; offset < length; ++offset) {
        const Edge edge = edge_at((start + offset) % count);
        if (adjacency.at(edge_key(edge.a, edge.b)).size() != 2U) {
            return false;
        }
        removed_edges.push_back(edge);
    }
    from_id = first.vertices[start];
    to_id = first.vertices[(start + length) % count];
    return true;
}

/// Collapse only the captured degree-two vertices, retaining surviving corner data.
bool dissolve_edge_endpoints(Document& document, const std::set<ElementId>& candidates)
{
    const VertexNeighbors joined_neighbors = vertex_neighbors(document);
    std::vector<ElementId> eligible;
    for (const Vertex& vertex : document.vertices) {
        const auto found = joined_neighbors.find(vertex.id);
        if (candidates.contains(vertex.id) && found != joined_neighbors.end() && found->second.size() == 2U) {
            eligible.push_back(vertex.id);
        }
    }
    for (const ElementId vertex_id : eligible) {
        std::vector<ElementId> neighbors;
        bool preserves_faces = true;
        for (const Face& face : document.faces) {
            if (contains_id(face.vertices, vertex_id)) {
                preserves_faces &= face.vertices.size() > 3U;
                append_loop_neighbors_for_vertex(face.vertices, vertex_id, neighbors);
            }
        }
        // Polygon documents cannot retain a face collapsed to a wire edge.
        if (neighbors.size() != 2U || !preserves_faces) {
            continue;
        }
        const bool hard = contains_edge(document.hard_normal_edges, make_edge(vertex_id, neighbors[0])) ||
                          contains_edge(document.hard_normal_edges, make_edge(vertex_id, neighbors[1]));
        for (Face& face : document.faces) {
            const auto corner = loop_vertex_index(face.vertices, vertex_id);
            if (!corner.has_value()) {
                continue;
            }
            const Face source = face;
            face.vertices.erase(face.vertices.begin() + static_cast<std::ptrdiff_t>(*corner));
            if (source.uvs.size() == source.vertices.size()) {
                face.uvs.erase(face.uvs.begin() + static_cast<std::ptrdiff_t>(*corner));
            } else if (!preserve_face_uvs(document, source, document, face)) {
                return false;
            }
        }
        if (hard) {
            add_unique_edge(document.hard_normal_edges, make_edge(neighbors[0], neighbors[1]));
        }
    }
    return true;
}

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
    return OperationResult::failure("Select one or more edges to dissolve.");
  }

    std::vector<Edge> selected_edges;
    const auto original_adjacency = face_indices_by_edge(document);
    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId || edge.a == edge.b ||
            contains_edge(selected_edges, edge)) {
            continue;
        }
        const auto adjacent = original_adjacency.find(edge_key(edge.a, edge.b));
        if (adjacent == original_adjacency.end() || adjacent->second.size() != 2U) {
            return OperationResult::failure("Dissolve needs selected interior edges with exactly two adjacent faces.");
        }
        selected_edges.push_back(edge);
    }
    const std::set<ElementId> vertex_candidates = dissolve_vertex_candidates(document, selected_edges, original_adjacency);
    Document candidate = document;
    std::vector<ElementId> merged_face_ids;
    std::vector<Edge> processed_edges;
    std::set<ElementId> affected_vertices;
    bool changed = false;

    for (Edge edge : selected_edges) {
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
            return OperationResult::failure("Dissolve needs selected interior edges with exactly two adjacent faces.");
        }

        const std::size_t first_index = adjacent_faces[0];
        const std::size_t second_index = adjacent_faces[1];
        const Face first_face = candidate.faces[first_index];
        Face second_face = candidate.faces[second_index];
        const std::optional<std::pair<ElementId, ElementId>> first_edge = oriented_edge_in_face(first_face, edge);
        if (!first_edge.has_value()) {
            return OperationResult::failure("Selected edge was not found.");
        }

        ElementId from_id = first_edge->first;
        ElementId to_id = first_edge->second;
        if (!directed_face_edge_index(second_face, to_id, from_id).has_value()) {
            if (!directed_face_edge_index(second_face, from_id, to_id).has_value()) {
                return OperationResult::failure("Selected edge adjacency is invalid.");
            }
            reverse_face_winding(second_face);
        }

        std::vector<Edge> removed_edges;
        if (!shared_chain_endpoints(candidate, first_face, second_face, edge, from_id, to_id, removed_edges)) {
            return OperationResult::failure("Dissolve would create invalid face geometry.");
        }

        std::vector<ElementId> merged_loop = merged_face_loop_for_dissolved_edge(first_face, second_face, from_id, to_id);
        if (merged_loop.size() < 3) {
            return OperationResult::failure("Dissolve would create invalid face geometry.");
        }

        Face merged_face = first_face;
        merged_face.vertices = std::move(merged_loop);
        merged_face.uvs.clear();
        orient_face_toward_normal(candidate, merged_face, face_normal(candidate, first_face) + face_normal(candidate, second_face));
        // Joining faces retains boundary corners before the separate vertex cleanup.
        if (!assign_merged_face_texture_boundary(
                candidate, std::array<ElementId, 2>{first_face.id, second_face.id}, merged_face)) {
            return OperationResult::failure("Dissolve could not preserve the source texture mapping.");
        }

        affected_vertices.insert(first_face.vertices.begin(), first_face.vertices.end());
        affected_vertices.insert(second_face.vertices.begin(), second_face.vertices.end());
        candidate.faces[first_index] = std::move(merged_face);
        candidate.faces.erase(candidate.faces.begin() + static_cast<std::ptrdiff_t>(second_index));
        // Removing the edge also removes its interior UV seam. Retain the stored
        // boundary corners while allowing the merged face's interpolation to change.
        if (!every_face_triangulates(candidate)) {
            return OperationResult::failure("Dissolve would create invalid face geometry.");
        }

        add_unique_id(merged_face_ids, first_face.id);
        for (const Edge removed_edge : removed_edges) {
            add_unique_edge(processed_edges, removed_edge);
        }
        changed = true;
    }

    if (!changed || merged_face_ids.empty()) {
        return OperationResult::failure("No dissolvable selected edges were found.");
    }

    if (!dissolve_edge_endpoints(candidate, vertex_candidates) || !every_face_triangulates(candidate)) {
        return OperationResult::failure("Dissolve would create invalid face geometry.");
    }
    std::set<ElementId> used_vertices;
    for (const Face& face : candidate.faces) {
        used_vertices.insert(face.vertices.begin(), face.vertices.end());
    }
    std::erase_if(candidate.vertices, [&](const Vertex& vertex) {
        return affected_vertices.contains(vertex.id) && !used_vertices.contains(vertex.id);
    });
    std::erase_if(candidate.hard_normal_edges, [&](Edge edge) { return !edge_exists(candidate, edge); });
    std::erase_if(merged_face_ids, [&](ElementId face_id) { return find_face(candidate, face_id) == nullptr; });

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
