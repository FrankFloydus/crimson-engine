#pragma once
#include <crimson/renderer/forward/MeshPaint.h>
#include <optional>
#include "DecalSurface.h"

namespace crimson::decals {
// Remove paint-only tessellation from proven planar, convex source faces whose
// actual refined render corners all carry one identical normal. Other faces
// retain their exact refined triangles and shading aliases. The result supplies
// decal receivers and an exact contact-shadow BVH; it never replaces rendered
// or saved paint geometry.
// No value means no source face could be simplified.
std::optional<ModelPrimitive> simplifyDecalPaintPrimitive(const ModelPrimitive& source,
    const MeshPaintGeometry& refined,std::stop_token stop={},const SurfaceBoundaryLineage* fineBoundary=nullptr,
    std::shared_ptr<const SurfaceBoundaryLineage>* retainedBoundary=nullptr);
// Sparse provenance for the actual meshPaintRenderPrimitive triangle ordering.
// Only original logical-face perimeter points are represented; interior warp
// and every published fine triangle position remain unchanged.
std::shared_ptr<const SurfaceBoundaryLineage> meshPaintBoundaryLineage(const ModelPrimitive& source,
    const MeshPaintGeometry& refined,std::stop_token stop={});
}
