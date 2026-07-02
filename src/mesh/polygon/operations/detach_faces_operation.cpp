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

#include <map>
#include <set>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Detach Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class DetachFacesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::DetachSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::DetachSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult detach_selected_faces_impl(Document& document, Selection& selection)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, "Select one or more faces to detach."};
  }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    if (selected_faces.empty()) {
        return { false, "No selected faces were found." };
    }

    std::set<ElementId> selected_face_ids;
    for (const Face& face : selected_faces) {
        selected_face_ids.insert(face.id);
    }

    std::set<ElementId> unselected_vertex_ids;
    for (const Face& face : document.faces) {
        if (selected_face_ids.contains(face.id)) {
            continue;
        }
        unselected_vertex_ids.insert(face.vertices.begin(), face.vertices.end());
    }

    std::map<ElementId, ElementId> duplicate_vertices;
    std::vector<ElementId> detached_face_ids;
    detached_face_ids.reserve(selected_faces.size());
    Document candidate = document;
    bool changed = false;
    for (Face& face : candidate.faces) {
        if (!selected_face_ids.contains(face.id)) {
            continue;
        }

        bool face_changed = false;
        for (ElementId& vertex_id : face.vertices) {
            if (!unselected_vertex_ids.contains(vertex_id)) {
                continue;
            }

            const ElementId duplicate_id = copied_vertex_id(document, candidate, duplicate_vertices, vertex_id);
            if (duplicate_id == kInvalidElementId) {
              return {false,
                      "Detach could not duplicate a selected face vertex."};
            }
            vertex_id = duplicate_id;
            face_changed = true;
        }

        if (face_changed) {
            changed = true;
        }
        add_unique_id(detached_face_ids, face.id);
    }

    if (!changed) {
        return { false, "Selected faces are already detached from unselected faces." };
    }

    if (!every_face_triangulates(candidate)) {
        return { false, "Detach would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(detached_face_ids);
    activate_face_or_last_selection(selection, active_face_id);
    return { true, {} };
}

OperationResult DetachFacesOperation::apply(Document& document, Selection& selection) const
{
    return detach_selected_faces_impl(document, selection);
}

} // namespace

OperationResult detach_selected_faces(Document& document, Selection& selection)
{
    return DetachFacesOperation().apply(document, selection);
}

} // namespace quader_poly
