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

class SplitEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return "split_selected_edges"; }
    [[nodiscard]] std::string_view label() const override { return "Split Selected Edges"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult SplitEdgesOperation::apply(Document& document, Selection& selection) const
{
    if (selection.mode != SelectionMode::edge || selection.edges.empty()) {
        return { false, "Select one or more edges to split." };
    }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.empty()) {
        return { false, "Select one or more valid edges to split." };
    }

    std::map<ElementId, std::vector<Edge>> split_edges_by_face_id;
    for (const Edge& edge : selected_edges) {
        const std::vector<std::size_t> adjacent_faces = adjacent_face_indices_for_edge(document, edge);
        if (adjacent_faces.size() != 2) {
            return { false, "Edge Split needs selected interior edges with exactly two adjacent faces." };
        }
        split_edges_by_face_id[document.faces[adjacent_faces.front()].id].push_back(edge);
    }

    Document candidate = document;
    std::map<std::pair<ElementId, ElementId>, ElementId> duplicate_vertices;
    std::vector<Edge> split_open_edges;
    bool changed = false;

    for (Face& face : candidate.faces) {
        const auto found_edges = split_edges_by_face_id.find(face.id);
        if (found_edges == split_edges_by_face_id.end()) {
            continue;
        }

        std::map<ElementId, ElementId> replacements;
        for (const Edge& edge : found_edges->second) {
            const ElementId duplicate_a = duplicate_vertex_for_face(candidate, duplicate_vertices, face.id, edge.a);
            const ElementId duplicate_b = duplicate_vertex_for_face(candidate, duplicate_vertices, face.id, edge.b);
            if (duplicate_a == invalid_element_id || duplicate_b == invalid_element_id) {
                return { false, "Edge Split could not duplicate selected edge vertices." };
            }

            replacements[edge.a] = duplicate_a;
            replacements[edge.b] = duplicate_b;
            add_unique_edge(split_open_edges, edge);
            add_unique_edge(split_open_edges, make_edge(duplicate_a, duplicate_b));
        }

        bool face_changed = false;
        for (ElementId& vertex_id : face.vertices) {
            const auto replacement = replacements.find(vertex_id);
            if (replacement == replacements.end()) {
                continue;
            }
            vertex_id = replacement->second;
            face_changed = true;
        }
        if (face_changed) {
            face.uvs.clear();
            changed = true;
        }
    }

    if (!changed || split_open_edges.empty()) {
        return { false, "No splittable selected edges were found." };
    }

    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Edge Split would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::edge;
    selection.edges = std::move(split_open_edges);
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

OperationResult split_selected_edges(Document& document, Selection& selection)
{
    return SplitEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
