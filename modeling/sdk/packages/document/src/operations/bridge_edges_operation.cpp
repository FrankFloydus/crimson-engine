#include <quader_poly/document.h>
#include <quader_poly/poly_operation.h>

#include "../internal/quader_poly_document_bridge_surface_helpers.h"
#include "../internal/quader_poly_document_hull_plane_helpers.h"
#include "../internal/quader_poly_document_knife_helpers.h"
#include "../internal/quader_poly_document_mesh_internal.h"
#include "../internal/quader_poly_document_topology_backend.h"
#include "../internal/quader_poly_document_topology_helpers.h"
#include "../internal/quader_poly_document_uv_helpers.h"

#include <array>
#include <cmath>

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {

OperationResult bridge_selected_edges_impl(Document& document, Selection& selection, int steps);

class BridgeEdgesOperation final : public PolyOperation {
public:
    explicit BridgeEdgesOperation(int steps = 1);

    [[nodiscard]] std::string_view id() const override { return "bridge_selected_edges"; }
    [[nodiscard]] std::string_view label() const override { return "Bridge Selected Edges"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    int steps_ = 1;
};

OperationResult bridge_selected_edges_impl(Document& document, Selection& selection, int steps)
{
    if (selection.mode != SelectionMode::edge || selection.edges.size() != 2) {
        return { false, "Bridge needs exactly two selected edges." };
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
