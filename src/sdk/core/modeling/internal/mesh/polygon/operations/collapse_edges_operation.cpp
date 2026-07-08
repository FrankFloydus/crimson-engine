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
 * Implements the Collapse Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class CollapseEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::CollapseSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::CollapseSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult CollapseEdgesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
    return {false, "Select one or more edges to collapse."};
  }

    Selection vertex_selection;
    vertex_selection.mode = SelectionMode::Vertex;
    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          continue;
        }
        if (find_vertex(document, edge.a) != nullptr) {
            add_unique_id(vertex_selection.vertices, edge.a);
        }
        if (find_vertex(document, edge.b) != nullptr) {
            add_unique_id(vertex_selection.vertices, edge.b);
        }
    }

    if (vertex_selection.vertices.size() < 2) {
        return { false, "Collapse needs at least one valid selected edge." };
    }

    ElementId active_vertex_id = vertex_selection.vertices.front();
    if (selection.has_active && selection.active_kind == ElementKind::Edge) {
      const Edge active_edge =
          make_edge(selection.active_edge.a, selection.active_edge.b);
      if (contains_id(vertex_selection.vertices, active_edge.a)) {
        active_vertex_id = active_edge.a;
      } else if (contains_id(vertex_selection.vertices, active_edge.b)) {
        active_vertex_id = active_edge.b;
      }
    }
    activate_vertex_selection(vertex_selection, active_vertex_id);

    OperationResult result = merge_selected_vertices_to_center(document, vertex_selection);
    if (!result.changed) {
        return result;
    }

    selection = std::move(vertex_selection);
    return { true, {} };
}

} // namespace

OperationResult collapse_selected_edges(Document& document, Selection& selection)
{
    return CollapseEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
