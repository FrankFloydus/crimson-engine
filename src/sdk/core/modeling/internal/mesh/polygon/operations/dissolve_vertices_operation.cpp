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
 * Implements the Dissolve Vertices Operation modeling operation for the polygon document and mesh editing core.
 */
class DissolveVerticesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::DissolveSelectedVertices).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::DissolveSelectedVertices).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult DissolveVerticesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex || selection.vertices.empty()) {
    return {false, "Select one or more vertices to dissolve."};
  }

    const std::vector<ElementId> selected_vertices = selected_valid_vertices(document, selection);
    if (selected_vertices.empty()) {
        return { false, "Select one or more valid vertices to dissolve." };
    }

    std::set<ElementId> selected_vertex_set(selected_vertices.begin(), selected_vertices.end());
    for (const ElementId vertex_id : selected_vertices) {
        bool used_by_face = false;
        for (const Face& face : document.faces) {
            if (!contains_id(face.vertices, vertex_id)) {
                continue;
            }

            used_by_face = true;
            if (!loop_vertex_is_redundant(document, face.vertices, vertex_id)) {
                return { false, "Dissolve Vertex only removes redundant straight-line vertices." };
            }
        }

        if (!used_by_face) {
            return { false, "Dissolve Vertex needs vertices that belong to faces." };
        }
    }

    Document candidate = document;
    std::vector<ElementId> neighbor_vertices;
    std::vector<ElementId> dissolved_vertices;
    bool changed = false;
    for (Face& face : candidate.faces) {
        if (std::ranges::none_of(face.vertices, [&selected_vertex_set](ElementId vertex_id) {
                return selected_vertex_set.contains(vertex_id);
            })) {
            continue;
        }

        const std::vector<ElementId> source_loop = face.vertices;
        std::vector<ElementId> removed_from_face;
        std::vector<ElementId> dissolved_loop = remove_redundant_vertices_from_loop(candidate, face.vertices, selected_vertex_set, removed_from_face);
        if (removed_from_face.empty()) {
            continue;
        }
        if (dissolved_loop.size() < 3U) {
            return { false, "Dissolve Vertex would create invalid face geometry." };
        }

        for (const ElementId vertex_id : removed_from_face) {
            append_loop_neighbors_for_vertex(source_loop, vertex_id, neighbor_vertices);
            add_unique_id(dissolved_vertices, vertex_id);
        }
        face.vertices = std::move(dissolved_loop);
        face.uvs.clear();
        changed = true;
    }

    if (!changed || dissolved_vertices.empty()) {
        return { false, "No dissolvable selected vertices were found." };
    }

    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Dissolve Vertex would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Vertex;
    for (const ElementId vertex_id : neighbor_vertices) {
        if (find_vertex(document, vertex_id) != nullptr) {
            add_unique_id(selection.vertices, vertex_id);
        }
    }
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

OperationResult dissolve_selected_vertices(Document& document, Selection& selection)
{
    return DissolveVerticesOperation().apply(document, selection);
}

} // namespace quader_poly
