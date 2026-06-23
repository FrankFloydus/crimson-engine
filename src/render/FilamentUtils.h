////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/MeshAsset.h"
#include "math/Types.h"

#include <filament/Box.h>
#include <math/vec3.h>

namespace quader {

inline filament::math::float3 toFilament(QVec3 v) {
    return {v.x, v.y, v.z};
}

filament::Box computeBounds(const MeshAsset& mesh);

} // namespace quader
