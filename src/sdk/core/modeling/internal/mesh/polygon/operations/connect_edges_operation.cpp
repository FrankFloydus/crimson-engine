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
    return OperationResult::failure("Connect Edges needs edge selection mode.");
  }

    const OperationResult source_validation = validate_authored_operation_candidate(document, document);
    if (!source_validation.success) {
        return source_validation;
    }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.size() < 2) {
        return OperationResult::failure("Select at least two valid edges to connect.");
    }

    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> indices_by_edge = face_indices_by_edge(document);
    for (const Edge& edge : selected_edges) {
        const auto found = indices_by_edge.find({ edge.a, edge.b });
        if (found == indices_by_edge.end() || found->second.empty()) {
            return OperationResult::failure("Connect Edges needs selected edges that belong to faces.");
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
        return OperationResult::failure("Connect Edges needs selected edges on connected mesh faces.");
    }

    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    std::map<ElementId, Edge> source_edge_by_split_vertex;
    Document candidate = document;
    for (const auto& entry : cut_edges_by_face_id) {
        for (const Edge& edge : entry.second) {
            const ElementId split_vertex = split_vertex_for_edge(candidate, split_vertices, edge, 0.5F);
            if (split_vertex == kInvalidElementId) {
              return OperationResult::failure("Connect Edges could not split a selected edge.");
            }
            source_edge_by_split_vertex[split_vertex] = make_edge(edge.a, edge.b);
        }
    }

    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(document.faces.size() + split_vertices.size());
    std::vector<Edge> connected_edges;
    std::map<std::pair<ElementId, ElementId>, std::pair<Edge, Edge>>
        connector_donors;
    const auto add_connected_edge =
        [&](ElementId first_vertex, ElementId second_vertex) {
            const Edge connector = make_edge(first_vertex, second_vertex);
            add_unique_edge(connected_edges, connector);
            const auto first_donor =
                source_edge_by_split_vertex.find(first_vertex);
            const auto second_donor =
                source_edge_by_split_vertex.find(second_vertex);
            if (first_donor != source_edge_by_split_vertex.end() &&
                second_donor != source_edge_by_split_vertex.end()) {
                connector_donors[edge_key(connector)] =
                    {first_donor->second, second_donor->second};
            }
        };
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
            return OperationResult::failure("Connect Edges would create invalid face geometry.");
        }

        if (cut_indices.size() < 2) {
            Face expanded_face = face;
            expanded_face.vertices = std::move(expanded_loop);
            if (!preserve_face_uvs(document, face, candidate, expanded_face)) {
                return OperationResult::failure("Connect Edges encountered invalid mesh data.");
            }
            rebuilt_faces.push_back(std::move(expanded_face));
            changed = true;
            continue;
        }

        Face expanded_source = face;
        expanded_source.vertices = expanded_loop;
        const std::size_t chord_count = cut_indices.size() == 2U ? 1U : cut_indices.size();
        for (std::size_t index = 0; index < chord_count; ++index) {
            if (!authored_face_chord_is_valid(candidate, expanded_source,
                    expanded_loop[cut_indices[index]],
                    expanded_loop[cut_indices[(index + 1U) % cut_indices.size()]])) {
                return OperationResult::failure("Connect Edges needs chords inside the authored face boundary.");
            }
        }

        if (cut_indices.size() == 2) {
            std::vector<ElementId> first_loop = unique_valid_face_loop(loop_between_indices(expanded_loop, cut_indices[0], cut_indices[1]));
            std::vector<ElementId> second_loop = unique_valid_face_loop(loop_between_indices(expanded_loop, cut_indices[1], cut_indices[0]));
            if (first_loop.size() < 3 || second_loop.size() < 3) {
                return OperationResult::failure("Connect Edges would create invalid face geometry.");
            }

            Face first_face = face;
            first_face.vertices = std::move(first_loop);
            if (!preserve_face_uvs(document, face, candidate, first_face)) {
                return OperationResult::failure("Connect Edges encountered invalid mesh data.");
            }
            rebuilt_faces.push_back(std::move(first_face));

            Face second_face;
            second_face.id = candidate.next_face_id++;
            second_face.vertices = std::move(second_loop);
            second_face.material_slot = face.material_slot;
            second_face.normal_shading = face.normal_shading;
            second_face.texture_transform = face.texture_transform;
            if (!preserve_face_uvs(document, face, candidate, second_face)) {
                return OperationResult::failure("Connect Edges encountered invalid mesh data.");
            }
            rebuilt_faces.push_back(std::move(second_face));
            add_connected_edge(
                expanded_loop[cut_indices[0]], expanded_loop[cut_indices[1]]);
            changed = true;
            continue;
        }

        Face center_face;
        center_face.id = face.id;
        center_face.material_slot = face.material_slot;
        center_face.normal_shading = face.normal_shading;
        center_face.texture_transform = face.texture_transform;
        center_face.vertices.reserve(cut_indices.size());
        for (const std::size_t cut_index : cut_indices) {
            center_face.vertices.push_back(expanded_loop[cut_index]);
        }
        center_face.vertices = unique_valid_face_loop(std::move(center_face.vertices));
        if (center_face.vertices.size() < 3) {
            return OperationResult::failure("Connect Edges would create invalid face geometry.");
        }
        if (!preserve_face_uvs(document, face, candidate, center_face)) {
            return OperationResult::failure("Connect Edges encountered invalid mesh data.");
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
            side_face.normal_shading = face.normal_shading;
            side_face.texture_transform = face.texture_transform;
            if (!preserve_face_uvs(document, face, candidate, side_face)) {
                return OperationResult::failure("Connect Edges encountered invalid mesh data.");
            }
            rebuilt_faces.push_back(std::move(side_face));
            add_connected_edge(expanded_loop[start], expanded_loop[end]);
        }
        changed = true;
    }

    if (!changed || connected_edges.empty()) {
        return OperationResult::failure("No connectable selected edges were found.");
    }

    candidate.faces = std::move(rebuilt_faces);
    restore_source_face_orientation(document, candidate);
    remap_normal_edge_states_after_edge_splits(
        document, candidate, split_vertices);
    std::vector<EdgeNormalModeCopy> connector_normal_mode_copies;
    connector_normal_mode_copies.reserve(connected_edges.size() * 2U);
    for (const Edge connector : connected_edges) {
        const auto donors = connector_donors.find(edge_key(connector));
        if (donors != connector_donors.end()) {
            connector_normal_mode_copies.push_back(
                {donors->second.first, connector});
            connector_normal_mode_copies.push_back(
                {donors->second.second, connector});
        }
    }
    (void)copy_edge_normal_modes(
        document, candidate, connector_normal_mode_copies);
    const OperationResult validation = validate_authored_operation_candidate(document, candidate);
    if (!validation.success) {
        return validation;
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
