////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
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

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {
/**
 * Implements the Collapse Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class CollapseFacesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::CollapseSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::CollapseSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult collapse_selected_faces_impl(Document& document, Selection& selection)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, "Collapse Faces needs one or more selected faces."};
  }

    Selection vertex_selection;
    vertex_selection.mode = SelectionMode::Vertex;
    for (const ElementId vertex_id : selected_vertex_ids(document, selection)) {
        add_unique_id(vertex_selection.vertices, vertex_id);
    }
    if (vertex_selection.vertices.size() < 2U) {
        return { false, "Collapse Faces needs at least two valid face vertices." };
    }
    activate_vertex_selection(vertex_selection, vertex_selection.vertices.front());

    OperationResult result = merge_selected_vertices_to_center(document, vertex_selection);
    if (!result.changed) {
        return result;
    }
    selection = std::move(vertex_selection);
    return { true, {} };
}



OperationResult CollapseFacesOperation::apply(Document& document, Selection& selection) const
{
    return collapse_selected_faces_impl(document, selection);
}

} // namespace

OperationResult collapse_selected_faces(Document& document, Selection& selection)
{
    return CollapseFacesOperation {}.apply(document, selection);
}
} // namespace quader_poly