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
 * Implements the Bevel Vertices Operation modeling operation for the polygon document and mesh editing core.
 */
class BevelVerticesOperation final : public PolyOperation {
public:
    explicit BevelVerticesOperation(float distance);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BevelSelectedVertices).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BevelSelectedVertices).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    float distance_ = 0.0F;
};

BevelVerticesOperation::BevelVerticesOperation(float distance)
    : distance_(distance)
{
}

OperationResult BevelVerticesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex) {
    return {false, "Bevel needs vertex selection mode."};
  }

    std::set<ElementId> selected_vertices;
    for (const ElementId vertex_id : selection.vertices) {
      if (vertex_id != kInvalidElementId &&
          find_vertex(document, vertex_id) != nullptr) {
        selected_vertices.insert(vertex_id);
      }
    }
    if (selected_vertices.empty()) {
        return { false, "Select one or more vertices to bevel." };
    }

    Document candidate = document;
    const float bevel_distance = std::max(distance_, kMinPrimitiveDimension);
    const std::vector<Face> original_faces = candidate.faces;
    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    std::map<ElementId, std::map<ElementId, ElementId>> split_by_vertex_neighbor;
    std::map<ElementId, std::vector<std::pair<ElementId, ElementId>>> neighbor_pairs_by_vertex;
    std::map<ElementId, std::uint32_t> cap_material_by_vertex;
    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(original_faces.size() + selected_vertices.size());
    bool changed = false;

    for (const Face& face : original_faces) {
        if (face.vertices.size() < 3) {
            rebuilt_faces.push_back(face);
            continue;
        }

        std::vector<ElementId> rebuilt_loop;
        rebuilt_loop.reserve(face.vertices.size() * 2U);
        bool face_changed = false;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId vertex_id = face.vertices[index];
            if (!selected_vertices.contains(vertex_id)) {
                rebuilt_loop.push_back(vertex_id);
                continue;
            }

            const ElementId prev_id = face.vertices[(index + face.vertices.size() - 1U) % face.vertices.size()];
            const ElementId next_id = face.vertices[(index + 1U) % face.vertices.size()];
            const ElementId prev_split = split_vertex_near_endpoint(candidate, split_vertices, vertex_id, prev_id, bevel_distance);
            const ElementId next_split = split_vertex_near_endpoint(candidate, split_vertices, vertex_id, next_id, bevel_distance);
            if (prev_split == kInvalidElementId ||
                next_split == kInvalidElementId) {
              rebuilt_loop.push_back(vertex_id);
              continue;
            }

            split_by_vertex_neighbor[vertex_id][prev_id] = prev_split;
            split_by_vertex_neighbor[vertex_id][next_id] = next_split;
            neighbor_pairs_by_vertex[vertex_id].push_back({ prev_id, next_id });
            cap_material_by_vertex.try_emplace(vertex_id, face.material_slot);
            rebuilt_loop.push_back(prev_split);
            rebuilt_loop.push_back(next_split);
            face_changed = true;
        }

        rebuilt_loop = unique_valid_face_loop(std::move(rebuilt_loop));
        if (rebuilt_loop.size() < 3 && face_changed) {
            return { false, "Bevel would create invalid face geometry." };
        }
        if (rebuilt_loop.size() < 3) {
            rebuilt_faces.push_back(face);
            continue;
        }

        Face rebuilt_face = face;
        rebuilt_face.vertices = std::move(rebuilt_loop);
        if (face_changed) {
            rebuilt_face.uvs.clear();
        }
        rebuilt_faces.push_back(std::move(rebuilt_face));
        changed = changed || face_changed;
    }

    std::vector<ElementId> new_selected_vertices;
    for (const ElementId vertex_id : selected_vertices) {
        const auto found_splits = split_by_vertex_neighbor.find(vertex_id);
        if (found_splits == split_by_vertex_neighbor.end() || found_splits->second.size() < 2) {
            continue;
        }

        std::vector<ElementId> neighbor_order;
        const auto found_pairs = neighbor_pairs_by_vertex.find(vertex_id);
        if (found_pairs != neighbor_pairs_by_vertex.end() && !found_pairs->second.empty()) {
            std::map<ElementId, ElementId> next_by_prev;
            for (const auto& pair : found_pairs->second) {
                next_by_prev[pair.first] = pair.second;
            }
            ElementId current = found_pairs->second.front().first;
            std::set<ElementId> visited_neighbors;
            while (current != kInvalidElementId &&
                   !visited_neighbors.contains(current)) {
              visited_neighbors.insert(current);
              neighbor_order.push_back(current);
              const auto next = next_by_prev.find(current);
              if (next == next_by_prev.end()) {
                break;
              }
              current = next->second;
            }
        }
        for (const auto& entry : found_splits->second) {
            if (!contains_id(neighbor_order, entry.first)) {
                neighbor_order.push_back(entry.first);
            }
        }

        std::vector<ElementId> cap_vertices;
        cap_vertices.reserve(neighbor_order.size());
        for (const ElementId neighbor_id : neighbor_order) {
            const auto found_split = found_splits->second.find(neighbor_id);
            if (found_split != found_splits->second.end()) {
                cap_vertices.push_back(found_split->second);
                add_unique_id(new_selected_vertices, found_split->second);
            }
        }
        cap_vertices = unique_valid_face_loop(std::move(cap_vertices));
        if (cap_vertices.size() < 3) {
            continue;
        }

        Face cap_face;
        cap_face.id = candidate.next_face_id++;
        cap_face.vertices = std::move(cap_vertices);
        const auto material = cap_material_by_vertex.find(vertex_id);
        cap_face.material_slot = material != cap_material_by_vertex.end() ? material->second : 0U;
        const Vertex* removed_vertex = find_vertex(candidate, vertex_id);
        if (removed_vertex != nullptr) {
            orient_face_toward_normal(candidate, cap_face, removed_vertex->position - face_centroid(candidate, cap_face));
        }
        rebuilt_faces.push_back(std::move(cap_face));
    }

    if (!changed || new_selected_vertices.empty()) {
        return { false, "No bevelable selected vertices were found." };
    }

    candidate.faces = std::move(rebuilt_faces);
    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Bevel would create invalid face geometry." };
    }

    document = std::move(candidate);

    selection.clear();
    selection.mode = SelectionMode::Vertex;
    selection.vertices = std::move(new_selected_vertices);
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

OperationResult bevel_selected_vertices(Document& document, Selection& selection, float distance)
{
    return BevelVerticesOperation(distance).apply(document, selection);
}

} // namespace quader_poly
