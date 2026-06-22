////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/polygon/document.h>
#include <modeling/polygon/poly_operation.h>

#include <modeling/polygon/internal/quader_poly_document_bridge_surface_helpers.h>
#include <modeling/polygon/internal/quader_poly_document_hull_plane_helpers.h>
#include <modeling/polygon/internal/quader_poly_document_knife_helpers.h>
#include <modeling/polygon/internal/quader_poly_document_topology_backend.h>
#include <modeling/polygon/internal/quader_poly_document_topology_helpers.h>

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

/**
 * Implements the Recalculate Face Normals Operation modeling operation for the polygon document and mesh editing core.
 */
class RecalculateFaceNormalsOperation final : public PolyOperation {
public:
    explicit RecalculateFaceNormalsOperation(bool outside);

    [[nodiscard]] std::string_view id() const override { return "recalculate_selected_face_normals"; }
    [[nodiscard]] std::string_view label() const override { return "Recalculate Selected Face Normals"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    bool outside_ = true;
};

using namespace document_internal;

namespace {
OperationResult recalculate_selected_face_normals_impl(Document& document, const Selection& selection, bool outside)
{
    if (selection.faces.empty()) {
        return { false, "No modeler faces are selected." };
    }

    Document candidate = document;
    const Vec3 center = document_vertex_centroid(candidate);
    bool changed = false;
    bool found_face = false;
    for (const ElementId face_id : selection.faces) {
        Face* face = find_face(candidate, face_id);
        if (face == nullptr || face->vertices.size() < 3) {
            continue;
        }

        found_face = true;
        Vec3 expected_normal = face_centroid(candidate, *face) - center;
        if (!outside) {
            expected_normal = expected_normal * -1.0F;
        }
        if (length_squared(expected_normal) <= epsilon) {
            continue;
        }

        const Vec3 before = face_normal(candidate, *face);
        orient_face_toward_normal(candidate, *face, expected_normal);
        const Vec3 after = face_normal(candidate, *face);
        if (length_squared(before) > epsilon && length_squared(after) > epsilon && dot(before, after) < 0.0F) {
            changed = true;
        }
    }

    if (!found_face) {
        return { false, "No selected faces were found." };
    }
    if (!changed) {
        return { false, outside ? "Selected face normals already point outside." : "Selected face normals already point inside." };
    }
    if (!every_face_triangulates(candidate)) {
        return { false, "Recalculate normals would create invalid face geometry." };
    }

    document = std::move(candidate);
    return { true, {} };
}



} // namespace

RecalculateFaceNormalsOperation::RecalculateFaceNormalsOperation(bool outside) : outside_(outside) {}

OperationResult RecalculateFaceNormalsOperation::apply(Document& document, Selection& selection) const
{
    return recalculate_selected_face_normals_impl(document, selection, outside_);
}

OperationResult recalculate_selected_face_normals(Document& document, const Selection& selection, bool outside)
{
    Selection selection_copy = selection;
    return RecalculateFaceNormalsOperation { outside }.apply(document, selection_copy);
}
} // namespace quader_poly