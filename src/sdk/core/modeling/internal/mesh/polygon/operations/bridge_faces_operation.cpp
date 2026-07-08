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
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <set>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Bridge Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class BridgeFacesOperation final : public PolyOperation {
public:
    explicit BridgeFacesOperation(int steps = 1);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BridgeSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BridgeSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    int steps_ = 1;
};

BridgeFacesOperation::BridgeFacesOperation(int steps)
    : steps_(steps)
{
}

OperationResult bridge_selected_faces_impl(Document& document, Selection& selection, int steps)
{
  if (selection.mode != SelectionMode::Face || selection.faces.size() < 2) {
    return {false, "Bridge Faces needs two selected faces or face islands."};
  }

    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    if (selected_faces.size() < 2) {
        return { false, "Bridge Faces needs two valid selected faces or face islands." };
    }

    if (selected_faces.size() == 2U) {
        const Face& first_face = selected_faces[0];
        const Face& second_face = selected_faces[1];
        const std::vector<ElementId> first_vertices = unique_valid_face_loop(first_face.vertices);
        const std::vector<ElementId> second_vertices = unique_valid_face_loop(second_face.vertices);
        if (first_vertices.size() < 3 || second_vertices.size() < 3 || first_vertices.size() != second_vertices.size()) {
            return { false, "Bridge Faces needs the two selected faces to have the same edge count." };
        }

        std::vector<ElementId> aligned_second_vertices = aligned_loop_vertices(document, first_vertices, second_vertices);
        if (aligned_second_vertices.size() != first_vertices.size()) {
            return { false, "Bridge Faces could not align the selected faces." };
        }

        const FacePerimeterInfo first_perimeter = face_perimeter_info(document, first_face.id);
        const FacePerimeterInfo second_perimeter = face_perimeter_info(document, second_face.id);
        const bool both_faces_are_open = first_perimeter.has_only_open_edges() && second_perimeter.has_only_open_edges();

        Document candidate = document;
        if (!both_faces_are_open) {
            std::set<ElementId> selected_face_ids { first_face.id, second_face.id };
            std::erase_if(candidate.faces, [&selected_face_ids](const Face& face) {
                return selected_face_ids.contains(face.id);
            });
        }

        std::vector<ElementId> bridge_face_ids;
        bridge_face_ids.reserve(first_vertices.size() * static_cast<std::size_t>(std::clamp(steps, 1, 64)));
        if (!append_bridge_faces_between_loops(
                candidate,
                first_vertices,
                aligned_second_vertices,
                face_normal(document, first_face),
                face_normal(document, second_face),
                first_face.material_slot,
                steps,
                bridge_face_ids,
                true)) {
            return { false, "Bridge Faces could not create valid bridge geometry." };
        }

        prune_unused_vertices(candidate);
        if (bridge_face_ids.empty() || !every_face_triangulates(candidate)) {
            return { false, "Bridge Faces would create invalid face geometry." };
        }

        document = std::move(candidate);
        selection.clear();
        selection.mode = SelectionMode::Face;
        selection.faces = std::move(bridge_face_ids);
        activate_last_selection(selection);
        return { true, {} };
    }

    const std::vector<FaceIslandBoundary> islands = selected_face_island_boundaries(document, selected_faces);
    if (islands.size() != 2U) {
        return { false, "Bridge Faces needs exactly two selected faces or two connected face selections." };
    }

    const FaceIslandBoundary& first = islands[0];
    const FaceIslandBoundary& second = islands[1];
    if (first.vertices.size() < 3 || second.vertices.size() < 3 || first.edges.size() != second.edges.size()) {
        return { false, "Bridge Faces needs selected face perimeters with the same edge count." };
    }

    std::vector<ElementId> aligned_second_vertices = aligned_loop_vertices(document, first.vertices, second.vertices);
    if (aligned_second_vertices.size() != first.vertices.size()) {
        return { false, "Bridge Faces could not align the selected face perimeters." };
    }

    std::set<ElementId> selected_face_ids;
    for (const Face& face : selected_faces) {
        selected_face_ids.insert(face.id);
    }

    Document candidate = document;
    const bool both_islands_are_open = first.all_open && second.all_open;
    if (!both_islands_are_open) {
        std::erase_if(candidate.faces, [&selected_face_ids](const Face& face) {
            return selected_face_ids.contains(face.id);
        });
    }

    std::vector<ElementId> bridge_face_ids;
    bridge_face_ids.reserve(first.edges.size() * static_cast<std::size_t>(std::clamp(steps, 1, 64)));
    if (!append_bridge_faces_between_loops(
            candidate,
            first.vertices,
            aligned_second_vertices,
            first.normal,
            second.normal,
            first.material_slot,
            steps,
            bridge_face_ids,
            false)) {
        return { false, "Bridge Faces could not create valid bridge geometry." };
    }

    prune_unused_vertices(candidate);
    if (bridge_face_ids.empty() || !every_face_triangulates(candidate)) {
        return { false, "Bridge Faces would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(bridge_face_ids);
    activate_last_selection(selection);
    return { true, {} };
}

OperationResult BridgeFacesOperation::apply(Document& document, Selection& selection) const
{
    return bridge_selected_faces_impl(document, selection, steps_);
}

} // namespace

OperationResult bridge_selected_faces(Document& document, Selection& selection, int steps)
{
    return BridgeFacesOperation(steps).apply(document, selection);
}

} // namespace quader_poly
