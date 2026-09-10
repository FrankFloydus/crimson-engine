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

using EdgeKey = std::pair<ElementId, ElementId>;

EdgeKey edge_key(Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    return {edge.a, edge.b};
}

std::map<EdgeKey, std::set<EdgeKey>> collapsed_edge_donors(
    const Document& source,
    const std::set<ElementId>& removed_vertices)
{
    std::map<EdgeKey, std::set<EdgeKey>> donors_by_replacement;
    for (const Face& face : source.faces) {
        for (std::size_t start = 0; start < face.vertices.size(); ++start) {
            if (removed_vertices.contains(face.vertices[start])) {
                continue;
            }
            std::vector<Edge> path;
            std::size_t index = start;
            for (std::size_t step = 0; step < face.vertices.size(); ++step) {
                const std::size_t next =
                    (index + 1U) % face.vertices.size();
                path.push_back(
                    make_edge(face.vertices[index], face.vertices[next]));
                if (!removed_vertices.contains(face.vertices[next])) {
                    if (path.size() > 1U && next != start) {
                        auto& donors = donors_by_replacement[edge_key(
                            make_edge(face.vertices[start],
                                      face.vertices[next]))];
                        for (const Edge donor : path) {
                            donors.insert(edge_key(donor));
                        }
                    }
                    break;
                }
                index = next;
            }
        }
    }
    return donors_by_replacement;
}

void remap_edge_normal_modes_after_vertex_dissolve(
    const Document& source, Document& candidate,
    const std::set<ElementId>& removed_vertices)
{
    const std::vector<Edge> source_edges = document_edges(source);
    const std::vector<Edge> candidate_edges = document_edges(candidate);
    const EdgeNormalModeLookup source_modes(source);
    const auto donors_by_replacement =
        collapsed_edge_donors(source, removed_vertices);
    std::set<EdgeKey> source_edge_keys;
    for (const Edge edge : source_edges) {
        source_edge_keys.insert(edge_key(edge));
    }
    candidate.hard_normal_edges.clear();
    candidate.soft_normal_edges.clear();

    std::vector<EdgeNormalModeAssignment> assignments;
    assignments.reserve(candidate_edges.size());
    for (const Edge target_edge : candidate_edges) {
        EdgeNormalMode target_mode = EdgeNormalMode::Default;
        const EdgeKey target_key = edge_key(target_edge);
        if (source_edge_keys.contains(target_key)) {
            target_mode = source_modes.mode(target_edge);
        } else if (const auto donors =
                       donors_by_replacement.find(target_key);
                   donors != donors_by_replacement.end()) {
            for (const Edge donor : source_edges) {
                if (donors->second.contains(edge_key(donor))) {
                    target_mode = source_modes.mode(donor);
                }
            }
        }
        assignments.push_back({target_edge, target_mode});
    }
    static_cast<void>(set_edge_normal_modes(candidate, assignments));
    prune_normal_edge_states(candidate);
}

/**
 * Implements the Dissolve Vertices Operation modeling operation for the polygon document and mesh editing core.
 */
class DissolveVerticesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::DissolveSelectedVertices).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::DissolveSelectedVertices).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult DissolveVerticesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex || selection.vertices.empty()) {
    return OperationResult::failure("Select one or more vertices to dissolve.");
  }

    const std::vector<ElementId> selected_vertices = selected_valid_vertices(document, selection);
    if (selected_vertices.empty()) {
        return OperationResult::failure("Select one or more valid vertices to dissolve.");
    }

    std::set<ElementId> selected_vertex_set(selected_vertices.begin(), selected_vertices.end());
    for (const ElementId vertex_id : selected_vertices) {
        bool used_by_face = false;
        for (const Face& face : document.faces) {
            if (!contains_id(face.vertices, vertex_id)) {
                continue;
            }

            used_by_face = true;
            if (!loop_vertex_is_redundant(document, face.vertices, vertex_id)) {
                return OperationResult::failure("Dissolve Vertex only removes redundant straight-line vertices.");
            }
        }

        if (!used_by_face) {
            return OperationResult::failure("Dissolve Vertex needs vertices that belong to faces.");
        }
    }

    Document candidate = document;
    std::vector<ElementId> neighbor_vertices;
    std::vector<ElementId> dissolved_vertices;
    bool changed = false;
    for (Face& face : candidate.faces) {
        if (std::ranges::none_of(face.vertices, [&selected_vertex_set](ElementId vertex_id) {
                return selected_vertex_set.contains(vertex_id);
            })) {
            continue;
        }

        const std::vector<ElementId> source_loop = face.vertices;
        std::vector<ElementId> removed_from_face;
        std::vector<ElementId> dissolved_loop = remove_redundant_vertices_from_loop(candidate, face.vertices, selected_vertex_set, removed_from_face);
        if (removed_from_face.empty()) {
            continue;
        }
        if (dissolved_loop.size() < 3U) {
            return OperationResult::failure("Dissolve Vertex would create invalid face geometry.");
        }

        for (const ElementId vertex_id : removed_from_face) {
            append_loop_neighbors_for_vertex(source_loop, vertex_id, neighbor_vertices);
            add_unique_id(dissolved_vertices, vertex_id);
        }
        const Face source_face = face;
        face.vertices = std::move(dissolved_loop);
        if (!preserve_face_uvs(document, source_face, candidate, face)) {
            face.uvs.clear();
        }
        changed = true;
    }

    if (!changed || dissolved_vertices.empty()) {
        return OperationResult::failure("No dissolvable selected vertices were found.");
    }

    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    remap_edge_normal_modes_after_vertex_dissolve(
        document, candidate,
        std::set<ElementId>(dissolved_vertices.begin(),
                            dissolved_vertices.end()));
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return OperationResult::failure("Dissolve Vertex would create invalid face geometry.");
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Vertex;
    for (const ElementId vertex_id : neighbor_vertices) {
        if (find_vertex(document, vertex_id) != nullptr) {
            add_unique_id(selection.vertices, vertex_id);
        }
    }
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

OperationResult dissolve_selected_vertices(Document& document, Selection& selection)
{
    return DissolveVerticesOperation().apply(document, selection);
}

} // namespace quader_poly
