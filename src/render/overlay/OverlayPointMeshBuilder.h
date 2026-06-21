#pragma once

#include "core/ColorSpace.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayCameraProjector.h"
#include "render/overlay/OverlayMeshTypes.h"

#include <cstdint>
#include <vector>

namespace quader {

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
            Vec3 position,
            std::uint32_t color,
            const OverlayCameraProjector& projector,
            float sizePixels,
            float depthBias);
};

} // namespace quader
