////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "math/types.hpp"

#include <filament/Box.h>
#include <math/mat3.h>
#include <math/vec3.h>
#include <math/vec4.h>

namespace quader {

inline filament::math::float3 to_filament(QVec3 v) { return {v.x, v.y, v.z}; }

/// Encodes an orthonormal tangent frame while preserving handedness through
/// Filament's medium-precision tangent varying, including half-turn frames.
filament::math::float4 pack_filament_tangent_frame(
    const filament::math::mat3f &frame);

filament::Box compute_bounds(const MeshAsset &mesh);

} // namespace quader
