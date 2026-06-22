////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/polygon/poly_operation.h>

#include <modeling/polygon/internal/quader_poly_document_bridge_surface_helpers.h>
#include <modeling/polygon/internal/quader_poly_document_hull_plane_helpers.h>
#include <modeling/polygon/internal/quader_poly_document_knife_helpers.h>
#include <modeling/polygon/internal/quader_poly_document_topology_backend.h>
#include <modeling/polygon/internal/quader_poly_document_topology_helpers.h>

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
    [[nodiscard]] std::string_view id() const override { return "fill_hole_from_selected_edges"; }
    [[nodiscard]] std::string_view label() const override { return "Fill Hole From Selected Edges"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult FillHoleFromEdgesOperation::apply(Document& document, Selection& selection) const
{
    if (selection.mode != SelectionMode::edge || selection.edges.size() < 3) {
        return { false, "Fill Hole needs at least three selected edges." };
    }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.size() < 3) {
        return { false, "Fill Hole needs at least three valid selected edges." };
    }

    for (const Edge& edge : selected_edges) {
        if (adjacent_face_indices_for_edge(document, edge).size() != 1U) {
            return { false, "Fill Hole needs selected edges on one open boundary loop." };
        }
    }

    std::optional<std::vector<ElementId>> loop = closed_edge_loop_from_edges(selected_edges);
    if (!loop.has_value()) {
        return { false, "Fill Hole needs selected edges forming one connected closed loop." };
    }

    Document candidate = document;
    Face fill_face;
    fill_face.id = candidate.next_face_id++;
    fill_face.vertices = std::move(*loop);
    fill_face.material_slot = material_slot_for_open_edge(document, selected_edges.front());
    if (!orient_face_against_adjacent_winding(document, fill_face)) {
        orient_face_toward_normal(candidate, fill_face, face_centroid(candidate, fill_face) - document_vertex_centroid(candidate));
    }

    candidate.faces.push_back(fill_face);
    prune_invalid_faces(candidate);
    if (find_face(candidate, fill_face.id) == nullptr || !every_face_triangulates(candidate)) {
        return { false, "Fill Hole would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::face;
    selection.faces.push_back(fill_face.id);
    activate_face_selection(selection, fill_face.id);
    return { true, {} };
}

} // namespace

OperationResult fill_hole_from_selected_edges(Document& document, Selection& selection)
{
    return FillHoleFromEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
