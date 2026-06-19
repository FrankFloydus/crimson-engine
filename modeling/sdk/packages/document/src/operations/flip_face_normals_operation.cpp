#include <quader_poly/document.h>
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

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {
class FlipFaceNormalsOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return "flip_selected_face_normals"; }
    [[nodiscard]] std::string_view label() const override { return "Flip Selected Face Normals"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult flip_selected_face_normals_impl(Document& document, const Selection& selection)
{
    if (selection.faces.empty()) {
        return { false, "No modeler faces are selected." };
    }

    bool changed = false;
    for (const ElementId face_id : selection.faces) {
        Face* face = find_face(document, face_id);
        if (face == nullptr || face->vertices.size() < 3) {
            continue;
        }

        reverse_face_winding(*face);
        changed = true;
    }
    return { changed, changed ? std::string {} : "No selected faces were found." };
}



OperationResult FlipFaceNormalsOperation::apply(Document& document, Selection& selection) const
{
    return flip_selected_face_normals_impl(document, selection);
}

} // namespace

OperationResult flip_selected_face_normals(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return FlipFaceNormalsOperation {}.apply(document, selection_copy);
}
} // namespace quader_poly