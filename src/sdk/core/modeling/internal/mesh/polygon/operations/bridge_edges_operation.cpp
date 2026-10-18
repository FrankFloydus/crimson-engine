////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <array>
#include <cmath>

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {

OperationResult bridge_selected_edges_impl(Document& document, Selection& selection, BridgeOptions options);

/**
 * Implements the Bridge Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class BridgeEdgesOperation final : public PolyOperation {
public:
    explicit BridgeEdgesOperation(BridgeOptions options = {});

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BridgeSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BridgeSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    BridgeOptions options_;
};

OperationResult bridge_selected_edges_impl(Document& document, Selection& selection, BridgeOptions options)
{
  if (selection.mode != SelectionMode::Edge || selection.edges.size() != 2) {
    return OperationResult::failure("Bridge needs exactly two selected edges.");
  }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.size() != 2 || selected_edges[0] == selected_edges[1]) {
        return OperationResult::failure("Bridge needs exactly two valid selected edges.");
    }

    const std::vector<std::pair<Edge, Edge>> edge_pairs {
        { selected_edges[0], selected_edges[1] },
    };
    return bridge_edge_pairs(document, selection, edge_pairs, options);
}

BridgeEdgesOperation::BridgeEdgesOperation(BridgeOptions options) : options_(options) {}

OperationResult BridgeEdgesOperation::apply(Document& document, Selection& selection) const
{
    return bridge_selected_edges_impl(document, selection, options_);
}

} // namespace

OperationResult bridge_selected_edges(Document &document, Selection &selection,
                                      BridgeOptions options) {
  const auto normalized = normalized_bridge_options(options);
  if (!normalized.has_value())
    return OperationResult::failure("Bridge tensions must be finite.");
  options = *normalized;
  return BridgeEdgesOperation{options}.apply(document, selection);
}
OperationResult bridge_selected_edges(Document &document, Selection &selection,
                                      int steps) {
  return bridge_selected_edges(document, selection,
                               BridgeOptions{.steps = steps});
}
} // namespace quader_poly
