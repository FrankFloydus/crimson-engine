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
 * Implements the Combine Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class CombineFacesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::CombineSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::CombineSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult combine_selected_faces_impl(Document& document, Selection& selection)
{
  if (selection.mode != SelectionMode::Face || selection.faces.size() < 2) {
    return {false, "Combine Faces needs at least two selected faces."};
  }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    if (selected_faces.size() < 2U) {
        return { false, "Combine Faces needs at least two valid selected faces." };
    }

    const std::vector<FaceIslandBoundary> islands = selected_face_island_boundaries(document, selected_faces);
    std::set<ElementId> combined_source_face_ids;
    std::set<ElementId> vertices_to_prune;
    std::vector<Face> merged_faces;
    std::vector<ElementId> merged_face_ids;
    ElementId preferred_active_face_id = kInvalidElementId;

    for (const FaceIslandBoundary& island : islands) {
        if (island.face_ids.size() < 2U) {
            continue;
        }

        std::vector<ElementId> merged_loop = unique_valid_face_loop(island.vertices);
        if (merged_loop.size() < 3U) {
            return { false, "Combine Faces would create invalid face geometry." };
        }

        Face merged_face;
        merged_face.id = island.face_ids.front();
        merged_face.vertices = std::move(merged_loop);
        merged_face.material_slot = island.material_slot;
        orient_face_toward_normal(document, merged_face, island.normal);

        for (const ElementId face_id : island.face_ids) {
            combined_source_face_ids.insert(face_id);
            if (face_id == active_face_id) {
                preferred_active_face_id = merged_face.id;
            }
            const auto selected_face = std::ranges::find_if(selected_faces, [face_id](const Face& face) {
                return face.id == face_id;
            });
            if (selected_face == selected_faces.end()) {
                continue;
            }
            for (const ElementId vertex_id : selected_face->vertices) {
                vertices_to_prune.insert(vertex_id);
            }
        }

        add_unique_id(merged_face_ids, merged_face.id);
        merged_faces.push_back(std::move(merged_face));
    }

    if (merged_faces.empty()) {
        return { false, "Combine Faces needs adjacent faces with shared interior edges." };
    }

    Document candidate = document;
    std::erase_if(candidate.faces, [&combined_source_face_ids](const Face& face) {
        return combined_source_face_ids.contains(face.id);
    });
    candidate.faces.insert(candidate.faces.end(), merged_faces.begin(), merged_faces.end());
    for (const ElementId vertex_id : vertices_to_prune) {
        [[maybe_unused]] const bool removed = remove_redundant_vertex_from_all_face_loops(candidate, vertex_id);
    }

    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Combine Faces would create invalid face geometry." };
    }

    std::erase_if(merged_face_ids, [&candidate](ElementId face_id) {
        return find_face(candidate, face_id) == nullptr;
    });
    if (merged_face_ids.empty()) {
        return { false, "Combine Faces would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(merged_face_ids);
    activate_face_or_last_selection(selection, preferred_active_face_id);
    return { true, {} };
}



OperationResult CombineFacesOperation::apply(Document& document, Selection& selection) const
{
    return combine_selected_faces_impl(document, selection);
}

} // namespace

OperationResult combine_selected_faces(Document& document, Selection& selection)
{
    return CombineFacesOperation {}.apply(document, selection);
}
} // namespace quader_poly