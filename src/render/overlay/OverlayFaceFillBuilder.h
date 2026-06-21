#pragma once

#include "core/ColorSpace.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayMeshTypes.h"

namespace quader {

class OverlayFaceFillBuilder {
public:
    static OverlayFaceFillMesh buildLayer(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer);

    static void appendTriangle(OverlayFaceFillMesh& mesh, const ViewportOverlayTriangle& triangle);
};

} // namespace quader
