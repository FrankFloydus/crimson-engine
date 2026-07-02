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
 * Implements the Extract Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class ExtractFacesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ExtractSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ExtractSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
    [[nodiscard]] ExtractFacesResult execute(Document& document, Selection& selection) const;
};

OperationResult ExtractFacesOperation::apply(Document& document, Selection& selection) const
{
    const ExtractFacesResult result = execute(document, selection);
    return { result.changed, result.message };
}

ExtractFacesResult extract_selected_faces_impl(Document& document, Selection& selection)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, {}, {}, "Select one or more faces to extract."};
  }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    if (selected_faces.empty()) {
        return { false, {}, {}, "No selected faces were found." };
    }

    Document extracted_document;
    Selection extracted_selection;
    extracted_selection.mode = SelectionMode::Face;
    std::map<ElementId, ElementId> vertex_map;
    std::map<ElementId, ElementId> face_map;
    for (const Face& face : selected_faces) {
        if (!append_copied_face(document, extracted_document, face, vertex_map, face_map, extracted_selection)) {
            return { false, {}, {}, "Extract could not copy selected face geometry." };
        }
    }

    if (extracted_document.faces.empty() || !every_face_triangulates(extracted_document)) {
        return { false, {}, {}, "Extract would create invalid face geometry." };
    }

    std::set<ElementId> selected_face_ids;
    for (const Face& face : selected_faces) {
        selected_face_ids.insert(face.id);
    }

    Document candidate = document;
    std::erase_if(candidate.faces, [&selected_face_ids](const Face& face) {
        return selected_face_ids.contains(face.id);
    });
    prune_unused_vertices(candidate);
    if (!candidate.faces.empty() && !every_face_triangulates(candidate)) {
        return { false, {}, {}, "Extract would leave invalid source face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;

    const auto active_face = face_map.find(active_face_id);
    if (active_face != face_map.end()) {
        activate_face_selection(extracted_selection, active_face->second);
    } else {
        activate_last_selection(extracted_selection);
    }

    return { true, std::move(extracted_document), std::move(extracted_selection), {} };
}

ExtractFacesResult ExtractFacesOperation::execute(Document& document, Selection& selection) const
{
    return extract_selected_faces_impl(document, selection);
}

} // namespace

ExtractFacesResult extract_selected_faces(Document& document, Selection& selection)
{
    return ExtractFacesOperation().execute(document, selection);
}

} // namespace quader_poly
