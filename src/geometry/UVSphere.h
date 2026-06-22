////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/MeshAsset.h"

#include <cstdint>

namespace quader {

/**
 * Stores the UV Sphere Options data contract used by the geometry predicate and projection layer.
 */
struct UVSphereOptions {
    std::uint32_t segments = 32;
    std::uint32_t rings = 16;
    float radius = 1.0f;
};

MeshAsset generateUVSphere(const UVSphereOptions& options = {});

} // namespace quader
