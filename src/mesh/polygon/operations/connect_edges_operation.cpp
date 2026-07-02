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
 * Implements the Connect Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class ConnectEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ConnectSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ConnectSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult ConnectEdgesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Edge) {
    return {false, "Connect Edges needs edge selection mode."};
  }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.size() < 2) {
        return { false, "Select at least two valid edges to connect." };
    }

    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> indices_by_edge = face_indices_by_edge(document);
    for (const Edge& edge : selected_edges) {
        const auto found = indices_by_edge.find({ edge.a, edge.b });
        if (found == indices_by_edge.end() || found->second.empty()) {
            return { false, "Connect Edges needs selected edges that belong to faces." };
        }
    }

    std::set<std::pair<ElementId, ElementId>> selected_edge_keys;
    for (const Edge& edge : selected_edges) {
        selected_edge_keys.insert(edge_key(edge));
    }

    std::map<ElementId, std::vector<Edge>> cut_edges_by_face_id;
    const std::vector<ConnectEdgeFaceRegion> regions = connect_edge_face_regions(document, indices_by_edge, selected_edge_keys);
    for (const ConnectEdgeFaceRegion& region : regions) {
        add_unordered_connect_region_paths(document, indices_by_edge, selected_edge_keys, region, cut_edges_by_face_id);
    }

    if (cut_edges_by_face_id.empty()) {
        return { false, "Connect Edges needs selected edges on connected mesh faces." };
    }

    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    Document candidate = document;
    for (const auto& entry : cut_edges_by_face_id) {
        for (const Edge& edge : entry.second) {
            const ElementId split_vertex = split_vertex_for_edge(candidate, split_vertices, edge, 0.5F);
            if (split_vertex == kInvalidElementId) {
              return {false, "Connect Edges could not split a selected edge."};
            }
        }
    }

    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(document.faces.size() + split_vertices.size());
    std::vector<Edge> connected_edges;
    bool changed = false;

    for (const Face& face : document.faces) {
        std::vector<ElementId> expanded_loop;
        expanded_loop.reserve(face.vertices.size() + split_vertices.size());
        std::vector<std::size_t> cut_indices;
        bool face_boundary_changed = false;
        const auto found_cut_edges = cut_edges_by_face_id.find(face.id);

        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            expanded_loop.push_back(face.vertices[vertex_index]);

            const Edge face_edge = make_edge(face.vertices[vertex_index], face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            const auto found_split = split_vertices.find(edge_key(face_edge));
            if (found_split == split_vertices.end()) {
                continue;
            }

            expanded_loop.push_back(found_split->second);
            face_boundary_changed = true;
            if (found_cut_edges != cut_edges_by_face_id.end() && contains_edge(found_cut_edges->second, face_edge)) {
                cut_indices.push_back(expanded_loop.size() - 1U);
            }
        }

        if (!face_boundary_changed) {
            rebuilt_faces.push_back(face);
            continue;
        }
        if (expanded_loop.size() < 3 || has_repeated_vertex(expanded_loop)) {
            return { false, "Connect Edges would create invalid face geometry." };
        }

        if (cut_indices.size() < 2) {
            Face expanded_face = face;
            expanded_face.vertices = std::move(expanded_loop);
            expanded_face.uvs.clear();
            rebuilt_faces.push_back(std::move(expanded_face));
            changed = true;
            continue;
        }

        if (cut_indices.size() == 2) {
            std::vector<ElementId> first_loop = unique_valid_face_loop(loop_between_indices(expanded_loop, cut_indices[0], cut_indices[1]));
            std::vector<ElementId> second_loop = unique_valid_face_loop(loop_between_indices(expanded_loop, cut_indices[1], cut_indices[0]));
            if (first_loop.size() < 3 || second_loop.size() < 3) {
                return { false, "Connect Edges would create invalid face geometry." };
            }

            Face first_face = face;
            first_face.vertices = std::move(first_loop);
            first_face.uvs.clear();
            rebuilt_faces.push_back(std::move(first_face));

            Face second_face;
            second_face.id = candidate.next_face_id++;
            second_face.vertices = std::move(second_loop);
            second_face.material_slot = face.material_slot;
            rebuilt_faces.push_back(std::move(second_face));
            add_unique_edge(connected_edges, make_edge(expanded_loop[cut_indices[0]], expanded_loop[cut_indices[1]]));
            changed = true;
            continue;
        }

        Face center_face;
        center_face.id = face.id;
        center_face.material_slot = face.material_slot;
        center_face.vertices.reserve(cut_indices.size());
        for (const std::size_t cut_index : cut_indices) {
            center_face.vertices.push_back(expanded_loop[cut_index]);
        }
        center_face.vertices = unique_valid_face_loop(std::move(center_face.vertices));
        if (center_face.vertices.size() < 3) {
            return { false, "Connect Edges would create invalid face geometry." };
        }
        rebuilt_faces.push_back(std::move(center_face));

        for (std::size_t cut_index = 0; cut_index < cut_indices.size(); ++cut_index) {
            const std::size_t start = cut_indices[cut_index];
            const std::size_t end = cut_indices[(cut_index + 1U) % cut_indices.size()];
            std::vector<ElementId> side_loop = unique_valid_face_loop(loop_between_indices(expanded_loop, start, end));
            if (side_loop.size() < 3) {
                continue;
            }

            Face side_face;
            side_face.id = candidate.next_face_id++;
            side_face.vertices = std::move(side_loop);
            side_face.material_slot = face.material_slot;
            rebuilt_faces.push_back(std::move(side_face));
            add_unique_edge(connected_edges, make_edge(expanded_loop[start], expanded_loop[end]));
        }
        changed = true;
    }

    if (!changed || connected_edges.empty()) {
        return { false, "No connectable selected edges were found." };
    }

    candidate.faces = std::move(rebuilt_faces);
    prune_invalid_faces(candidate);
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Connect Edges would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Edge;
    selection.edges = std::move(connected_edges);
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

OperationResult connect_selected_edges(Document& document, Selection& selection)
{
    return ConnectEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
