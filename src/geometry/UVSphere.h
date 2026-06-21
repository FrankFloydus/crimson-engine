#pragma once

#include "assets/MeshAsset.h"

#include <cstdint>

namespace quader {

struct UVSphereOptions {
    std::uint32_t segments = 32;
    std::uint32_t rings = 16;
    float radius = 1.0f;
};

MeshAsset generateUVSphere(const UVSphereOptions& options = {});

} // namespace quader
