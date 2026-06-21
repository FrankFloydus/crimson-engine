#pragma once

#include "assets/MeshAsset.h"
#include "math/Types.h"

#include <filament/Box.h>
#include <math/vec3.h>

namespace quader {

inline filament::math::float3 toFilament(Vec3 v) {
    return {v.x, v.y, v.z};
}

filament::Box computeBounds(const MeshAsset& mesh);

} // namespace quader
