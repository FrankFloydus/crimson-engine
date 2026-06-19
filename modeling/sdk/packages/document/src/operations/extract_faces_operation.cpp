#include <quader_poly/poly_operation.h>

#include "../internal/quader_poly_document_bridge_surface_helpers.h"
#include "../internal/quader_poly_document_hull_plane_helpers.h"
#include "../internal/quader_poly_document_topology_helpers.h"

#include <map>
#include <set>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

class ExtractFacesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return "extract_selected_faces"; }
    [[nodiscard]] std::string_view label() const override { return "Extract Selected Faces"; }
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
    if (selection.mode != SelectionMode::face || selection.faces.empty()) {
        return { false, {}, {}, "Select one or more faces to extract." };
    }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    if (selected_faces.empty()) {
        return { false, {}, {}, "No selected faces were found." };
    }

    Document extracted_document;
    Selection extracted_selection;
    extracted_selection.mode = SelectionMode::face;
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
    selection.mode = SelectionMode::face;

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
