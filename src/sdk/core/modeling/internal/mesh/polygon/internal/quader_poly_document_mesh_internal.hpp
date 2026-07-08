////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace quader_poly::document_internal {

/**
 * Represents a Triangle value used by the polygon document and mesh editing core.
 */
struct Triangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

/**
 * Represents a Face UV Basis value used by the polygon document and mesh editing core.
 */
struct FaceUvBasis {
    quader::QVec3 origin;
    quader::QVec3 u_axis { 1.0F, 0.0F, 0.0F };
    quader::QVec3 v_axis { 0.0F, 1.0F, 0.0F };
    bool valid = false;
};

/**
 * Represents a Face UV Projection Assignment value used by the polygon document and mesh editing core.
 */
struct FaceUvProjectionAssignment {
    std::uint32_t material_slot = 0;
    FaceUvBasis basis;
    ElementId source_face_id = kInvalidElementId;
    bool inherited_loop_basis = false;
};

/**
 * Represents a Merge Face UV Assignment value used by the polygon document and mesh editing core.
 */
struct MergeFaceUvAssignment {
  ElementId face_id = kInvalidElementId;
  FaceUvProjectionAssignment assignment;
};

[[nodiscard]] int dropped_axis_for_normal(const quader::QVec3& normal);
[[nodiscard]] std::vector<Triangle> triangulate_face_local_indices(const Document& document, const Face& face);

[[nodiscard]] FaceUvBasis generated_face_uv_basis(const Document& document, const Face& face, const quader::QVec3& normal);
[[nodiscard]] FaceUvBasis project_uv_basis_to_face_plane(const FaceUvBasis& generated_basis, const quader::QVec3& normal, const quader::QVec3& origin);
[[nodiscard]] FaceUvBasis face_local_uv_basis(const Document& document, const Face& face, const quader::QVec3& normal);
[[nodiscard]] quader::QVec2 generated_face_uv(const quader::QVec3& position, const FaceUvBasis& basis);
[[nodiscard]] quader::QVec2 generated_uv_for_position(const quader::QVec3& position, const FaceUvBasis& basis, int dropped_axis);
[[nodiscard]] bool face_has_loop_uvs(const Face& face);
[[nodiscard]] bool assign_generated_face_uvs(Document& document, Face& face);
[[nodiscard]] bool assign_face_uvs_from_basis(Document& document, Face& face, const FaceUvBasis& basis);
[[nodiscard]] bool assign_face_local_uvs(Document& document, Face& face);
[[nodiscard]] quader::QVec2 translation_uv_delta(const Document& document, const Face& face, const quader::QVec3& delta);
[[nodiscard]] bool ensure_face_loop_uvs(Document& document, Face& face);

} // namespace quader_poly::document_internal
