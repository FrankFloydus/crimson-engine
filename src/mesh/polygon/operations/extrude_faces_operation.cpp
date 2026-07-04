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

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Extrude Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class ExtrudeFacesOperation final : public PolyOperation {
public:
    explicit ExtrudeFacesOperation(float distance = 0.45F);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    float distance_ = 0.45F;
};

ExtrudeFacesOperation::ExtrudeFacesOperation(float distance)
    : distance_(distance)
{
}

OperationResult extrude_selected_faces_impl(Document& document, Selection& selection, float distance)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, "Select one or more faces to extrude."};
  }

  if (std::abs(distance) <= kEpsilon) {
    return {false, "Extrude distance must be non-zero."};
  }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> faces = selected_face_copies(document, selection);
    if (faces.empty()) {
        return { false, "No selected faces were found." };
    }

    std::vector<ElementId> selected_faces;
    selected_faces.reserve(faces.size());
    OperationResult result;
    bool changed = false;
    for (const Face& original_face : faces) {
        Face* face = find_face(document, original_face.id);
        if (face == nullptr || original_face.vertices.size() < 3) {
            continue;
        }

        const quader::QVec3 offset = face_normal(document, original_face) * distance;
        std::vector<ElementId> cap_vertices;
        cap_vertices.reserve(original_face.vertices.size());
        bool valid_face = true;
        for (const ElementId vertex_id : original_face.vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex == nullptr) {
                valid_face = false;
                break;
            }

            cap_vertices.push_back(add_vertex(document, vertex->position + offset));
        }

        if (!valid_face || cap_vertices.size() != original_face.vertices.size()) {
            continue;
        }

        result.created.vertices.insert(
            result.created.vertices.end(), cap_vertices.begin(), cap_vertices.end());
        face->vertices = cap_vertices;
        for (std::size_t index = 0; index < original_face.vertices.size(); ++index) {
            const std::array side {
                original_face.vertices[index],
                original_face.vertices[(index + 1U) % original_face.vertices.size()],
                cap_vertices[(index + 1U) % cap_vertices.size()],
                cap_vertices[index],
            };
            const ElementId side_face_id = add_face(document, side, original_face.material_slot);
            if (side_face_id != kInvalidElementId) {
                result.created.faces.push_back(side_face_id);
            }
        }

        selected_faces.push_back(original_face.id);
        result.affected.faces.push_back(original_face.id);
        changed = true;
    }

    if (!changed) {
        return { false, "No selected faces were extruded." };
    }

    prune_invalid_faces(document);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(selected_faces);
    activate_face_or_last_selection(selection, active_face_id);
    result.changed = true;
    return result;
}

OperationResult ExtrudeFacesOperation::apply(Document& document, Selection& selection) const
{
    return extrude_selected_faces_impl(document, selection, distance_);
}

} // namespace

OperationResult extrude_selected_faces(Document& document, Selection& selection, float distance)
{
    return ExtrudeFacesOperation(distance).apply(document, selection);
}

} // namespace quader_poly
