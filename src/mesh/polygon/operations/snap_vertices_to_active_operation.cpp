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
 * Implements the Snap Vertices To Active Operation modeling operation for the polygon document and mesh editing core.
 */
class SnapVerticesToActiveOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::SnapSelectedVerticesToActive).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::SnapSelectedVerticesToActive).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult SnapVerticesToActiveOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex) {
    return {false, "Snap to Vertex needs vertex selection mode."};
  }
  if (!selection.has_active || selection.active_kind != ElementKind::Vertex ||
      selection.active_vertex == kInvalidElementId ||
      !contains_id(selection.vertices, selection.active_vertex)) {
    return {false, "Snap to Vertex needs an active selected vertex."};
  }

    const Vertex* active_vertex = find_vertex(document, selection.active_vertex);
    if (active_vertex == nullptr) {
        return { false, "Active vertex was not found." };
    }

    const quader::QVec3 target_position = active_vertex->position;
    bool has_other_vertex = false;
    bool changed = false;
    std::set<ElementId> visited_vertex_ids;
    for (const ElementId vertex_id : selection.vertices) {
      if (vertex_id == kInvalidElementId ||
          vertex_id == selection.active_vertex ||
          !visited_vertex_ids.insert(vertex_id).second) {
        continue;
      }

        Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            continue;
        }

        has_other_vertex = true;
        if (length_squared(vertex->position - target_position) <= kEpsilon) {
          continue;
        }

        vertex->position = target_position;
        changed = true;
    }

    if (!has_other_vertex) {
        return { false, "Select at least one other vertex to snap." };
    }
    if (!changed) {
        return { false, "Selected vertices are already at the active vertex." };
    }
    return { true, {} };
}

} // namespace

OperationResult snap_selected_vertices_to_active(Document& document, Selection& selection)
{
    return SnapVerticesToActiveOperation().apply(document, selection);
}

} // namespace quader_poly
