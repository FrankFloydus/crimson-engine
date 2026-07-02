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
 * Implements the Merge Vertices To Center Operation modeling operation for the polygon document and mesh editing core.
 */
class MergeVerticesToCenterOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::MergeSelectedVerticesToCenter).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::MergeSelectedVerticesToCenter).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult MergeVerticesToCenterOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex) {
    return {false, "Merge needs vertex selection mode."};
  }

    std::vector<ElementId> selected_vertices;
    selected_vertices.reserve(selection.vertices.size());
    quader::QVec3 center;
    for (const ElementId vertex_id : selection.vertices) {
      if (vertex_id == kInvalidElementId ||
          contains_id(selected_vertices, vertex_id)) {
        continue;
      }

        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            continue;
        }

        selected_vertices.push_back(vertex_id);
        center += vertex->position;
    }

    if (selected_vertices.size() < 2) {
        return { false, "Select at least two vertices to merge." };
    }

    center = center / static_cast<float>(selected_vertices.size());

    ElementId survivor_vertex_id = kInvalidElementId;
    if (selection.has_active && selection.active_kind == ElementKind::Vertex &&
        contains_id(selected_vertices, selection.active_vertex) &&
        find_vertex(document, selection.active_vertex) != nullptr) {
      survivor_vertex_id = selection.active_vertex;
    } else {
      survivor_vertex_id = selected_vertices.front();
    }

    std::set<ElementId> merge_vertex_ids;
    for (const ElementId vertex_id : selected_vertices) {
        if (vertex_id != survivor_vertex_id) {
            merge_vertex_ids.insert(vertex_id);
        }
    }
    if (merge_vertex_ids.empty()) {
        return { false, "Select at least one other vertex to merge." };
    }

    Document candidate;
    const OperationResult candidate_result = build_vertex_merge_candidate(document, candidate, merge_vertex_ids, survivor_vertex_id, center);
    if (!candidate_result.changed) {
        return candidate_result;
    }

    if (find_vertex(candidate, survivor_vertex_id) == nullptr) {
        return { false, "Merge would remove the center vertex." };
    }

    document = std::move(candidate);
    prune_unused_vertices(document);

    selection.clear();
    selection.mode = SelectionMode::Vertex;
    selection.vertices.push_back(survivor_vertex_id);
    activate_vertex_selection(selection, survivor_vertex_id);

    return { true, {} };
}

} // namespace

OperationResult merge_selected_vertices_to_center(Document& document, Selection& selection)
{
    return MergeVerticesToCenterOperation().apply(document, selection);
}

} // namespace quader_poly
