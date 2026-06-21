#pragma once

#include "core/ColorSpace.h"
#include "render/ViewportOverlayFrame.h"
#include "render/ViewportOverlayPolicy.h"
#include "render/overlay/OverlayCameraProjector.h"
#include "render/overlay/OverlayMeshTypes.h"

#include <vector>

namespace quader {

class OverlayLineMeshBuilder {
public:
    static OverlayLineMesh build(
            const std::vector<const ViewportOverlayLine*>& lines,
            const ViewportOverlayCamera& camera,
            float widthPixels,
            float depthBias);

    static bool appendLineQuad(
            OverlayLineMesh& mesh,
            const ViewportOverlayLine& line,
            const OverlayCameraProjector& projector,
            float widthPixels,
            float depthBias);
};

} // namespace quader
