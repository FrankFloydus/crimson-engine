#include <quader_poly/poly_operation.h>

#include "../internal/quader_poly_document_bridge_surface_helpers.h"
#include "../internal/quader_poly_document_hull_plane_helpers.h"
#include "../internal/quader_poly_document_knife_helpers.h"
#include "../internal/quader_poly_document_topology_backend.h"
#include "../internal/quader_poly_document_topology_helpers.h"

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

class MergeVerticesToActiveOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return "merge_selected_vertices_to_active"; }
    [[nodiscard]] std::string_view label() const override { return "Merge Selected Vertices To Active"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult MergeVerticesToActiveOperation::apply(Document& document, Selection& selection) const
{
    if (selection.mode != SelectionMode::vertex) {
        return { false, "Merge needs vertex selection mode." };
    }
    if (selection.vertices.size() < 2) {
        return { false, "Select at least two vertices to merge." };
    }
    if (!selection.has_active ||
        selection.active_kind != ElementKind::vertex ||
        selection.active_vertex == invalid_element_id ||
        !contains_id(selection.vertices, selection.active_vertex)) {
        return { false, "Merge needs an active selected vertex." };
    }

    const ElementId active_vertex_id = selection.active_vertex;
    if (find_vertex(document, active_vertex_id) == nullptr) {
        return { false, "Active vertex was not found." };
    }

    std::set<ElementId> merge_vertex_ids;
    for (const ElementId vertex_id : selection.vertices) {
        if (vertex_id == active_vertex_id || vertex_id == invalid_element_id || find_vertex(document, vertex_id) == nullptr) {
            continue;
        }
        merge_vertex_ids.insert(vertex_id);
    }
    if (merge_vertex_ids.empty()) {
        return { false, "Select at least one other vertex to merge." };
    }

#if defined(QUADER_POLY_USE_OPENMESH)
    return merge_selected_vertices_to_active_with_topology_backend(document, selection, merge_vertex_ids, active_vertex_id);
#else
    Document candidate;
    const Vertex* active_vertex = find_vertex(document, active_vertex_id);
    if (active_vertex == nullptr) {
        return { false, "Active vertex was not found." };
    }
    const OperationResult candidate_result = build_vertex_merge_candidate(document, candidate, merge_vertex_ids, active_vertex_id, active_vertex->position);
    if (!candidate_result.changed) {
        return candidate_result;
    }

    if (find_vertex(candidate, active_vertex_id) == nullptr) {
        return { false, "Merge would remove the active vertex." };
    }
    document = std::move(candidate);
    prune_unused_vertices(document);

    selection.clear();
    selection.mode = SelectionMode::vertex;
    selection.vertices.push_back(active_vertex_id);
    activate_vertex_selection(selection, active_vertex_id);

    return { true, {} };
#endif
}

} // namespace

OperationResult merge_selected_vertices_to_active(Document& document, Selection& selection)
{
    return MergeVerticesToActiveOperation().apply(document, selection);
}

} // namespace quader_poly
