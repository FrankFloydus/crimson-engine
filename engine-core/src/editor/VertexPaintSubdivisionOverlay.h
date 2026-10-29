#pragma once
#include "crimson-renderer/CollisionOverlayTypes.h"
#include <crimson/renderer/forward/MeshPaint.h>
#include <stop_token>

namespace crimson {
// Detached, immutable preparation for the existing depth-tested wire pass.
// Logical cell boundaries deliberately omit the renderer's quad diagonals.
std::shared_ptr<const CollisionOverlayGeometry> buildVertexPaintSubdivisionOverlay(
    const MeshPaintGeometry&,std::stop_token={});
}
