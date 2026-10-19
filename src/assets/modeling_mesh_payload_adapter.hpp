////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <quader/modeling/payloads.hpp>

#include <optional>
#include <string>

namespace quader {

/// Transfers the portable analytic field without introducing a second owner.
[[nodiscard]] inline quader_poly::CircularUvInterpolation
circular_uv_interpolation_from_modeling(const modeling::CircularUvInterpolation &field) {
  return {{field.center.x, field.center.y, field.center.z},
          {field.axis_u.x, field.axis_u.y, field.axis_u.z},
          {field.axis_v.x, field.axis_v.y, field.axis_v.z}, field.uv_from_polar};
}

[[nodiscard]] inline modeling::CircularUvInterpolation
circular_uv_interpolation_to_modeling(const quader_poly::CircularUvInterpolation &field) {
  return {{field.center.x, field.center.y, field.center.z},
          {field.axis_u.x, field.axis_u.y, field.axis_u.z},
          {field.axis_v.x, field.axis_v.y, field.axis_v.z}, field.uv_from_polar};
}

/**
 * Converts an SDK-authored mesh payload into native asset geometry.
 *
 * This keeps the SDK-to-native payload mapping asset-owned so render caches,
 * bake commands, and future exporters do not drift on vertex, UV, or material
 * slot semantics.
 */
[[nodiscard]] std::optional<MeshGeometryPayload>
mesh_geometry_payload_from_modeling_payload(
    const modeling::MeshPayload &source);

/**
 * Builds a normal native MeshAsset from an SDK mesh payload.
 */
[[nodiscard]] std::optional<MeshAsset>
mesh_asset_from_modeling_payload(const modeling::MeshPayload &source,
                                 std::string name);

} // namespace quader
