#pragma once
#include <crimson/renderer/forward/MeshPaint.h>
#include <optional>

namespace crimson::decals {
// Remove paint-only tessellation from proven planar, convex source faces whose
// actual refined render corners all carry one identical normal. Other faces
// retain their exact refined triangles and shading aliases. The result is only
// a decal receiver proxy; it never replaces rendered or saved paint geometry.
// No value means no source face could be simplified.
std::optional<ModelPrimitive> simplifyDecalPaintPrimitive(const ModelPrimitive& source,
    const MeshPaintGeometry& refined,std::stop_token stop={});
}
