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
#include <array>
#include <cstddef>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Inset Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class InsetFacesOperation final : public PolyOperation {
public:
    explicit InsetFacesOperation(float amount = 0.18F);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::InsetSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::InsetSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    float amount_ = 0.18F;
};

InsetFacesOperation::InsetFacesOperation(float amount)
    : amount_(amount)
{
}

OperationResult inset_selected_faces_impl(Document& document, Selection& selection, float amount)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, "Select one or more faces to inset."};
  }

    amount = std::clamp(amount, 0.01F, 0.95F);
    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> faces = selected_face_copies(document, selection);
    if (faces.empty()) {
        return { false, "No selected faces were found." };
    }

    std::vector<ElementId> selected_faces;
    selected_faces.reserve(faces.size());
    bool changed = false;
    for (const Face& original_face : faces) {
        Face* face = find_face(document, original_face.id);
        if (face == nullptr || original_face.vertices.size() < 3) {
            continue;
        }

        const quader::QVec3 centroid = face_centroid(document, original_face);
        std::vector<ElementId> inner_vertices;
        inner_vertices.reserve(original_face.vertices.size());
        bool valid_face = true;
        for (const ElementId vertex_id : original_face.vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex == nullptr) {
                valid_face = false;
                break;
            }

            inner_vertices.push_back(add_vertex(document, vertex->position + ((centroid - vertex->position) * amount)));
        }

        if (!valid_face || inner_vertices.size() != original_face.vertices.size()) {
            continue;
        }

        face->vertices = inner_vertices;
        for (std::size_t index = 0; index < original_face.vertices.size(); ++index) {
            const std::array rim {
                original_face.vertices[index],
                original_face.vertices[(index + 1U) % original_face.vertices.size()],
                inner_vertices[(index + 1U) % inner_vertices.size()],
                inner_vertices[index],
            };
            [[maybe_unused]] const ElementId rim_face_id = add_face(document, rim, original_face.material_slot);
        }

        selected_faces.push_back(original_face.id);
        changed = true;
    }

    if (!changed) {
        return { false, "No selected faces were inset." };
    }

    prune_invalid_faces(document);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(selected_faces);
    activate_face_or_last_selection(selection, active_face_id);
    return { true, {} };
}

OperationResult InsetFacesOperation::apply(Document& document, Selection& selection) const
{
    return inset_selected_faces_impl(document, selection, amount_);
}

} // namespace

OperationResult inset_selected_faces(Document& document, Selection& selection, float amount)
{
    return InsetFacesOperation(amount).apply(document, selection);
}

} // namespace quader_poly
