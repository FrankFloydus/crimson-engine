////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/ColorSpace.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayCameraProjector.h"
#include "render/overlay/OverlayMeshTypes.h"

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Builds Overlay Point Mesh Builder data for the render extraction and Filament resource layer.
 */
class OverlayPointMeshBuilder {
public:
    static OverlayPointMesh build(
            const std::vector<const ViewportOverlayPoint*>& points,
            const ViewportOverlayCamera& camera,
            float sizePixels,
            float depthBias,
            std::uint32_t overrideColor,
            bool useOverrideColor);

    static bool appendPointQuad(
            OverlayPointMesh& mesh,
            QVec3 position,
            std::uint32_t color,
            const OverlayCameraProjector& projector,
            float sizePixels,
            float depthBias);
};

} // namespace quader
