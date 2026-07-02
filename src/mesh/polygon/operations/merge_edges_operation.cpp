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
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Merge Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class MergeEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::MergeSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::MergeSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

std::optional<std::string> add_endpoint_target(std::map<ElementId, ElementId>& endpoint_targets, ElementId source, ElementId survivor)
{
    if (source == survivor) {
        return std::nullopt;
    }

    const auto existing = endpoint_targets.find(source);
    if (existing == endpoint_targets.end()) {
        endpoint_targets[source] = survivor;
        return std::nullopt;
    }
    if (existing->second != survivor) {
        return std::string("Merge Edges has conflicting selected-edge endpoint targets.");
    }
    return std::nullopt;
}

OperationResult merge_endpoint_group(Document& working, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id)
{
    if (merge_vertex_ids.empty()) {
        return { true, {} };
    }

    const Vertex* survivor_vertex = find_vertex(working, survivor_vertex_id);
    if (survivor_vertex == nullptr) {
        return { false, "Merge Edges survivor vertex was not found." };
    }

    Document candidate;
    const OperationResult candidate_result = build_vertex_merge_candidate(
        working,
        candidate,
        merge_vertex_ids,
        survivor_vertex_id,
        survivor_vertex->position);
    if (!candidate_result.changed) {
        return candidate_result;
    }

    if (find_vertex(candidate, survivor_vertex_id) == nullptr) {
        return { false, "Merge Edges would remove the survivor vertex." };
    }

    working = std::move(candidate);
    prune_unused_vertices(working);
    return { true, {} };
}

OperationResult MergeEdgesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Edge) {
    return {false, "Merge Edges needs edge selection mode."};
  }

    std::vector<Edge> selected_edges;
    selected_edges.reserve(selection.edges.size());
    for (Edge edge : selected_valid_edges(document, selection)) {
        if (!edge_exists(document, edge)) {
            continue;
        }
        selected_edges.push_back(edge);
    }
    if (selected_edges.size() < 2) {
        return { false, "Select at least two valid edges to merge." };
    }

    Edge target_edge = selected_edges.front();
    if (selection.has_active && selection.active_kind == ElementKind::Edge) {
      const Edge active_edge =
          make_edge(selection.active_edge.a, selection.active_edge.b);
      if (contains_edge(selected_edges, active_edge)) {
        target_edge = active_edge;
      }
    }

    const Vertex* target_a = find_vertex(document, target_edge.a);
    const Vertex* target_b = find_vertex(document, target_edge.b);
    if (target_a == nullptr || target_b == nullptr) {
        return { false, "Merge Edges survivor edge vertices were not found." };
    }

    std::map<ElementId, ElementId> endpoint_targets;
    bool has_other_edge = false;
    for (Edge edge : selected_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge == target_edge) {
            continue;
        }

        const Vertex* source_a = find_vertex(document, edge.a);
        const Vertex* source_b = find_vertex(document, edge.b);
        if (source_a == nullptr || source_b == nullptr) {
            continue;
        }

        has_other_edge = true;
        const float direct_cost =
            length_squared(source_a->position - target_a->position) +
            length_squared(source_b->position - target_b->position);
        const float flipped_cost =
            length_squared(source_a->position - target_b->position) +
            length_squared(source_b->position - target_a->position);
        const ElementId survivor_a = direct_cost <= flipped_cost ? target_edge.a : target_edge.b;
        const ElementId survivor_b = direct_cost <= flipped_cost ? target_edge.b : target_edge.a;

        if (edge.a != target_edge.a && edge.a != target_edge.b) {
            if (std::optional<std::string> error = add_endpoint_target(endpoint_targets, edge.a, survivor_a)) {
                return { false, *error };
            }
        }
        if (edge.b != target_edge.a && edge.b != target_edge.b) {
            if (std::optional<std::string> error = add_endpoint_target(endpoint_targets, edge.b, survivor_b)) {
                return { false, *error };
            }
        }
    }

    if (!has_other_edge || endpoint_targets.empty()) {
        return { false, "Select at least one other edge to merge." };
    }

    std::set<ElementId> merge_to_a;
    std::set<ElementId> merge_to_b;
    for (const auto& entry : endpoint_targets) {
        if (entry.second == target_edge.a) {
            merge_to_a.insert(entry.first);
        } else if (entry.second == target_edge.b) {
            merge_to_b.insert(entry.first);
        } else {
            return { false, "Merge Edges endpoint target was invalid." };
        }
    }

    Document working = document;
    OperationResult result = merge_endpoint_group(working, merge_to_a, target_edge.a);
    if (!result.changed) {
        return result;
    }
    result = merge_endpoint_group(working, merge_to_b, target_edge.b);
    if (!result.changed) {
        return result;
    }

    document = std::move(working);

    selection.clear();
    const Edge survivor_edge = make_edge(target_edge.a, target_edge.b);
    if (edge_exists(document, survivor_edge)) {
      selection.mode = SelectionMode::Edge;
      selection.edges.push_back(survivor_edge);
      activate_edge_selection(selection, survivor_edge);
    } else {
      selection.mode = SelectionMode::Vertex;
      if (find_vertex(document, survivor_edge.a) != nullptr) {
        selection.vertices.push_back(survivor_edge.a);
      }
        if (find_vertex(document, survivor_edge.b) != nullptr) {
            selection.vertices.push_back(survivor_edge.b);
        }
        if (!selection.vertices.empty()) {
            activate_vertex_selection(selection, selection.vertices.front());
        }
    }

    return { true, {} };
}

} // namespace

OperationResult merge_selected_edges(Document& document, Selection& selection)
{
    return MergeEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
