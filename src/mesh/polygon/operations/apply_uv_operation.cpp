////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.h>
#include <mesh/polygon/poly_operation_descriptor.h>

#include <mesh/polygon/internal/quader_poly_document_constants.h>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.h>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Apply UV Operation modeling operation for the polygon document and mesh editing core.
 */
class ApplyUvOperation final : public PolyOperation {
public:
    explicit ApplyUvOperation(std::string_view operation, float shift_world_amount = 1.0F);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ApplyUv).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ApplyUv).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    std::string operation_;
    float shift_world_amount_ = 1.0F;
};

OperationResult apply_uv_to_selection(
    Document& document,
    const Selection& selection,
    std::string_view operation,
    float shift_world_amount)
{
    const bool valid_operation = operation == "align_grid" ||
        operation == "align_face" ||
        operation == "fit_both" ||
        operation == "fit_x" ||
        operation == "fit_y" ||
        operation == "justify_left" ||
        operation == "justify_right" ||
        operation == "justify_top" ||
        operation == "justify_bottom" ||
        operation == "justify_center" ||
        operation == "shift_left" ||
        operation == "shift_right" ||
        operation == "shift_up" ||
        operation == "shift_down";
    if (!valid_operation) {
        return { false, "Unknown UV operation." };
    }

    const std::vector<ElementId> face_ids = uv_target_face_ids(document, selection);
    if (face_ids.empty()) {
        return { false, "Select a face, edge, vertex, or object first." };
    }

    const float safe_shift_world_amount = shift_world_amount > epsilon ? shift_world_amount : 1.0F;
    const float shift_uv_amount = safe_shift_world_amount * generated_uv_tiles_per_world_unit;
    bool changed = false;
    for (const ElementId face_id : face_ids) {
        Face* face = find_face(document, face_id);
        if (face == nullptr || face->vertices.size() < 3) {
            continue;
        }
        const std::vector<quader::QVec2> before = face->uvs;
        bool operation_applied = false;

        if (operation == "align_grid") {
            face->uvs.clear();
            operation_applied = true;
        } else if (operation == "align_face") {
            operation_applied = assign_face_local_uvs(document, *face);
        } else {
            if (!ensure_face_loop_uvs(document, *face)) {
                continue;
            }
            if (operation == "fit_both") {
                operation_applied = fit_face_uvs(*face, true, true);
            } else if (operation == "fit_x") {
                operation_applied = fit_face_uvs(*face, true, false);
            } else if (operation == "fit_y") {
                operation_applied = fit_face_uvs(*face, false, true);
            } else if (operation == "shift_left" || operation == "shift_right" || operation == "shift_up" || operation == "shift_down") {
                operation_applied = shift_face_uvs_for_operation(*face, operation, shift_uv_amount);
            } else {
                operation_applied = justify_face_uvs(*face, operation);
            }
        }

        if (operation_applied && !face_uvs_equal(before, face->uvs)) {
            changed = true;
        }
    }

    return { changed, changed ? std::string {} : "Selected UVs already match that command." };
}

ApplyUvOperation::ApplyUvOperation(std::string_view operation, float shift_world_amount)
    : operation_(operation)
    , shift_world_amount_(shift_world_amount)
{
}

OperationResult ApplyUvOperation::apply(Document& document, Selection& selection) const
{
    return apply_uv_to_selection(document, selection, operation_, shift_world_amount_);
}

} // namespace

OperationResult apply_uv_operation(Document& document, const Selection& selection, std::string_view operation, float shift_world_amount)
{
    Selection operation_selection = selection;
    return ApplyUvOperation(operation, shift_world_amount).apply(document, operation_selection);
}

} // namespace quader_poly
