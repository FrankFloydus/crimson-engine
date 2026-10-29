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
    std::span<const Face> boundary_faces,
    const std::set<ElementId>& removed_vertices)
{
    std::map<EdgeKey, std::set<EdgeKey>> donors_by_replacement;
    for (const Face& face : boundary_faces) {
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
    std::span<const Face> boundary_faces,
    const std::set<ElementId>& removed_vertices)
{
    const std::vector<Edge> source_edges = document_edges(source);
    const std::vector<Edge> candidate_edges = document_edges(candidate);
    const EdgeNormalModeLookup source_modes(source);
    const auto donors_by_replacement =
        collapsed_edge_donors(boundary_faces, removed_vertices);
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

/// Junctions nominate incident interior seams; original degree-two vertices
/// only collapse their corners and must not join otherwise independent faces.
std::vector<std::vector<std::size_t>> vertex_dissolve_face_regions(
    const Document& document, const std::set<ElementId>& selected_vertices)
{
    std::map<ElementId, std::size_t> degrees;
    for (const Edge edge : document_edges(document)) {
        ++degrees[edge.a];
        ++degrees[edge.b];
    }
    std::vector<std::vector<std::size_t>> adjacency(document.faces.size());
    for (const auto& [edge, faces] : face_indices_by_edge(document)) {
        const bool selected_junction =
            (selected_vertices.contains(edge.first) && degrees.at(edge.first) > 2U) ||
            (selected_vertices.contains(edge.second) && degrees.at(edge.second) > 2U);
        if (selected_junction && faces.size() == 2U) {
            adjacency[faces[0]].push_back(faces[1]);
            adjacency[faces[1]].push_back(faces[0]);
        }
    }

    std::vector<std::vector<std::size_t>> regions;
    std::vector<bool> visited(document.faces.size(), false);
    for (std::size_t seed = 0U; seed < document.faces.size(); ++seed) {
        if (visited[seed]) {
            continue;
        }
        std::vector<std::size_t> pending{seed};
        visited[seed] = true;
        std::vector<std::size_t> region;
        while (!pending.empty()) {
            const std::size_t index = pending.back();
            pending.pop_back();
            region.push_back(index);
            for (const std::size_t neighbor : adjacency[index]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    pending.push_back(neighbor);
                }
            }
        }
        std::ranges::sort(region);
        regions.push_back(std::move(region));
    }
    return regions;
}

/// Removes selected corners without imposing automatic simplification's
/// straightness predicate. Surviving corners keep their authored UV donors.
bool dissolve_face_corners(const Document& document, Face& face,
                           const std::set<ElementId>& selected_vertices)
{
    const Face source_face = face;
    std::erase_if(face.vertices, [&](ElementId vertex_id) {
        return selected_vertices.contains(vertex_id);
    });
    if (face.vertices.size() < 3U) {
        return false;
    }
    return preserve_face_uvs(document, source_face, document, face);
}

/// Dissolves selected vertices through validated face regions and boundary
/// corner collapse, retaining the existing document and selection on failure.
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

    const OperationResult source_validation = validate_authored_operation_candidate(document, document);
    if (!source_validation.success) {
        return source_validation;
    }

    const std::vector<ElementId> selected_vertices = selected_valid_vertices(document, selection);
    if (selected_vertices.empty()) {
        return OperationResult::failure("Select one or more valid vertices to dissolve.");
    }

    const std::set<ElementId> selected_vertex_set(selected_vertices.begin(), selected_vertices.end());
    std::set<ElementId> used_selected_vertices;
    std::vector<ElementId> neighbor_vertices;
    for (const Face& face : document.faces) {
        for (const ElementId vertex_id : face.vertices) {
            if (selected_vertex_set.contains(vertex_id)) {
                used_selected_vertices.insert(vertex_id);
                append_loop_neighbors_for_vertex(face.vertices, vertex_id, neighbor_vertices);
            }
        }
    }
    if (used_selected_vertices != selected_vertex_set) {
        return OperationResult::failure("Dissolve Vertex needs vertices that belong to faces.");
    }

    Document candidate = document;
    candidate.faces.clear();
    candidate.faces.reserve(document.faces.size());
    std::vector<Face> collapse_boundaries;
    for (const auto& region : vertex_dissolve_face_regions(document, selected_vertex_set)) {
        Face face = document.faces[region.front()];
        if (region.size() > 1U) {
            std::vector<Face> sources;
            std::vector<ElementId> source_ids;
            sources.reserve(region.size());
            source_ids.reserve(region.size());
            for (const std::size_t index : region) {
                sources.push_back(document.faces[index]);
                source_ids.push_back(document.faces[index].id);
            }
            const auto boundaries = selected_face_island_boundaries(document, sources);
            if (boundaries.size() != 1U || boundaries.front().vertices.size() < 3U) {
                return OperationResult::failure("Dissolve Vertex needs a single face boundary around each selected junction.");
            }
            face.vertices = boundaries.front().vertices;
            face.uvs.clear();
            orient_face_toward_normal(document, face, boundaries.front().normal);
            // Inherit the existing boundary before collapsing selected corners;
            // the removed seams need not retain their interior interpolation.
            if (!assign_merged_face_texture_boundary(document, source_ids, face)) {
                face.uvs.clear();
            }
        }
        if (std::ranges::any_of(face.vertices, [&](ElementId vertex_id) {
                return selected_vertex_set.contains(vertex_id);
            })) {
            collapse_boundaries.push_back(face);
            if (!dissolve_face_corners(document, face, selected_vertex_set)) {
                return OperationResult::failure("Dissolve Vertex would create invalid face geometry.");
            }
        }
        candidate.faces.push_back(std::move(face));
    }

    std::erase_if(candidate.vertices, [&](const Vertex& vertex) {
        return selected_vertex_set.contains(vertex.id);
    });
    remap_edge_normal_modes_after_vertex_dissolve(
        document, candidate, collapse_boundaries, selected_vertex_set);
    restore_source_face_orientation(document, candidate);
    const OperationResult validation = validate_authored_operation_candidate(document, candidate);
    if (!validation.success) {
        return validation;
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
