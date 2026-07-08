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
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <array>
#include <cmath>

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {

OperationResult bridge_selected_edges_impl(Document& document, Selection& selection, int steps);

/**
 * Implements the Bridge Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class BridgeEdgesOperation final : public PolyOperation {
public:
    explicit BridgeEdgesOperation(int steps = 1);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BridgeSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BridgeSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    int steps_ = 1;
};

OperationResult bridge_selected_edges_impl(Document& document, Selection& selection, int steps)
{
  if (selection.mode != SelectionMode::Edge || selection.edges.size() != 2) {
    return {false, "Bridge needs exactly two selected edges."};
  }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.size() != 2 || selected_edges[0] == selected_edges[1]) {
        return { false, "Bridge needs exactly two valid selected edges." };
    }

    const std::vector<std::pair<Edge, Edge>> edge_pairs {
        { selected_edges[0], selected_edges[1] },
    };
    return bridge_edge_pairs(document, selection, edge_pairs, steps);
}

BridgeEdgesOperation::BridgeEdgesOperation(int steps) : steps_(steps) {}

OperationResult BridgeEdgesOperation::apply(Document& document, Selection& selection) const
{
    return bridge_selected_edges_impl(document, selection, steps_);
}

} // namespace

OperationResult bridge_selected_edges(Document& document, Selection& selection, int steps)
{
    return BridgeEdgesOperation { steps }.apply(document, selection);
}
} // namespace quader_poly
