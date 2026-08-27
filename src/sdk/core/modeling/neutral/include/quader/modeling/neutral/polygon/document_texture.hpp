////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace quader_poly {

[[nodiscard]] bool
valid_face_texture_transform(const FaceTextureTransform &transform) noexcept;
[[nodiscard]] quader::QVec2 apply_face_texture_transform(
    quader::QVec2 base_uv, const FaceTextureTransform &transform);
[[nodiscard]] quader::QVec2 unapply_face_texture_transform(
    quader::QVec2 uv, const FaceTextureTransform &transform);

/** Canonical generated projection, including normal-dependent signed axes. */
[[nodiscard]] quader::QVec2 generated_texture_uv(
    quader::QVec3 position, quader::QVec3 geometric_normal);

/**
 * Fits the affine part of authored UVs without changing their corner residuals.
 * Empty authored UVs use the identity carrier. Degenerate fits use zero.
 */
[[nodiscard]] std::array<float, 4> face_texture_carrier(
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> base_uvs,
    quader::QVec3 geometric_normal, const FaceTextureTransform &transform);
[[nodiscard]] std::array<float, 4> face_texture_carrier(
    const Document &document, const Face &face, quader::QVec3 geometric_normal);

/**
 * Evaluates an occurrence's placement correction without baking geometry or
 * losing authored residuals. Identity correction returns uv0 exactly.
 */
[[nodiscard]] quader::QVec2 evaluate_texture_mapping(
    quader::QVec2 uv0, quader::QVec3 position, quader::QVec3 geometric_normal,
    const std::array<float, 4> &carrier,
    const Transform3 &sampling_from_local);

/**
 * Solves authored base UVs for desired effective corner UVs while retaining
 * both face settings and occurrence compensation. Carrier fitting and final
 * float evaluation use the same rules as rendering. Ill-conditioned or
 * non-finite mappings return no value without changing the input geometry.
 */
[[nodiscard]] std::optional<std::vector<quader::QVec2>>
solve_face_texture_base_uvs(
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> effective_uvs,
    quader::QVec3 geometric_normal, const FaceTextureTransform &transform,
    const Transform3 &sampling_from_local);

/**
 * Bakes occurrence compensation into base UVs while retaining face settings.
 * Returns whether any base UV changed. Invalid/non-finite results throw
 * std::invalid_argument before publishing any face updates.
 */
[[nodiscard]] bool bake_document_texture_mapping(
    Document &document, const Transform3 &sampling_from_local);

} // namespace quader_poly
