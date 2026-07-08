////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/document_topology.hpp>
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
 * Implements the Fill Hole From Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class FillHoleFromEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::FillHoleFromSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::FillHoleFromSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

bool edges_share_vertex(Edge first, Edge second)
{
    return first.a == second.a || first.a == second.b ||
           first.b == second.a || first.b == second.b;
}

bool edge_connects_to_component(const Edge& edge, std::span<const Edge> component)
{
    return std::ranges::any_of(component, [&edge](const Edge& component_edge) {
        return edges_share_vertex(edge, component_edge);
    });
}

std::vector<Edge> selected_boundary_edges(const Document& document, std::span<const Edge> selected_edges)
{
    std::vector<Edge> boundary_edges;
    boundary_edges.reserve(selected_edges.size());
    for (const Edge& edge : selected_edges) {
        if (adjacent_face_indices_for_edge(document, edge).size() == 1U) {
            boundary_edges.push_back(edge);
        }
    }
    return boundary_edges;
}

std::vector<Edge> open_boundary_edges(const Document& document)
{
    std::vector<Edge> boundary_edges;
    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts =
        edge_incidence_counts(document);
    for (const Edge& edge : document_edges(document)) {
        const auto found = incidence_counts.find(edge_key(edge));
        if (found != incidence_counts.end() && found->second == 1) {
            boundary_edges.push_back(edge);
        }
    }
    return boundary_edges;
}

std::set<std::pair<ElementId, ElementId>> edge_key_set(std::span<const Edge> edges)
{
    std::set<std::pair<ElementId, ElementId>> keys;
    for (const Edge& edge : edges) {
        keys.insert(edge_key(edge));
    }
    return keys;
}

std::size_t selected_edge_count(
    std::span<const Edge> component,
    const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys)
{
    std::size_t count = 0;
    for (const Edge& edge : component) {
        if (selected_edge_keys.contains(edge_key(edge))) {
            ++count;
        }
    }
    return count;
}

void append_unique_edge_component(
    std::vector<std::vector<Edge>>& components,
    std::vector<Edge> component)
{
    const std::set<std::pair<ElementId, ElementId>> component_keys =
        edge_key_set(component);
    const auto existing = std::ranges::find_if(components, [&component_keys](const std::vector<Edge>& existing_component) {
        return edge_key_set(existing_component) == component_keys;
    });
    if (existing == components.end()) {
        components.push_back(std::move(component));
    }
}

std::vector<std::vector<Edge>> connected_edge_components(std::vector<Edge> edges)
{
    std::vector<std::vector<Edge>> components;
    while (!edges.empty()) {
        std::vector<Edge> component;
        component.push_back(edges.back());
        edges.pop_back();

        bool grew = true;
        while (grew) {
            grew = false;
            for (auto edge = edges.begin(); edge != edges.end();) {
                if (edge_connects_to_component(*edge, component)) {
                    component.push_back(*edge);
                    edge = edges.erase(edge);
                    grew = true;
                } else {
                    ++edge;
                }
            }
        }

        components.push_back(std::move(component));
    }
    return components;
}

OperationResult FillHoleFromEdgesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Edge || selection.edges.size() < 3) {
    return {false, "Fill Hole needs at least three selected edges."};
  }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.size() < 3) {
        return { false, "Fill Hole needs at least three valid selected edges." };
    }

    const std::vector<Edge> boundary_edges = selected_boundary_edges(document, selected_edges);
    if (boundary_edges.size() < 3) {
        return { false, "Fill Hole needs at least three selected open boundary edges." };
    }

    std::vector<std::vector<Edge>> fill_components =
        connected_edge_components(boundary_edges);
    const std::set<std::pair<ElementId, ElementId>> selected_boundary_keys =
        edge_key_set(boundary_edges);
    for (std::vector<Edge>& component :
         connected_edge_components(open_boundary_edges(document))) {
        if (selected_edge_count(component, selected_boundary_keys) >= 3U) {
            append_unique_edge_component(fill_components, std::move(component));
        }
    }

    Document candidate = document;
    std::vector<ElementId> filled_faces;
    bool found_closed_loop = false;
    for (const std::vector<Edge>& component : fill_components) {
        std::optional<std::vector<ElementId>> loop = closed_edge_loop_from_edges(component);
        if (!loop.has_value()) {
            continue;
        }
        found_closed_loop = true;

        Document trial = candidate;
        Face fill_face;
        fill_face.id = trial.next_face_id++;
        const ElementId fill_face_id = fill_face.id;
        fill_face.vertices = std::move(*loop);
        fill_face.material_slot = material_slot_for_open_edge(document, component.front());
        if (!orient_face_against_adjacent_winding(document, fill_face)) {
            orient_face_toward_normal(trial, fill_face, face_centroid(trial, fill_face) - document_vertex_centroid(trial));
        }

        trial.faces.push_back(fill_face);
        prune_invalid_faces(trial);
        if (find_face(trial, fill_face_id) == nullptr || !every_face_triangulates(trial)) {
            continue;
        }

        candidate = std::move(trial);
        filled_faces.push_back(fill_face_id);
    }
    if (!found_closed_loop) {
        return { false, "Fill Hole needs selected open boundary edges forming at least one closed loop." };
    }
    if (filled_faces.empty()) {
        return { false, "Fill Hole would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(filled_faces);
    activate_face_selection(selection, selection.faces.back());
    return { true, {} };
}

} // namespace

OperationResult fill_hole_from_selected_edges(Document& document, Selection& selection)
{
    return FillHoleFromEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
