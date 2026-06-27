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
 * Implements the Flip Face Normals Operation modeling operation for the polygon document and mesh editing core.
 */
class FlipFaceNormalsOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::FlipSelectedFaceNormals).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::FlipSelectedFaceNormals).label; }
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