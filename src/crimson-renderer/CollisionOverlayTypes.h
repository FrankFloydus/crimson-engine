#pragma once
#include "math/Math.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace crimson {
struct CollisionOverlayEdge {
    std::uint32_t a{},b{};
    Vec3 normalA{},normalB{};
    bool silhouetteOnly{};
    bool concave{}; // Derived from adjacent faces; analytic primitives are convex.
    bool planeFallback{}; // Nonmanifold edges have no reliable two-face wedge.
};
// Immutable CPU geometry. Topology is shared across color, pose and camera
// changes; the renderer owns its cached GPU buffers, never an editor document.
struct CollisionOverlayGeometry {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> triangles;
    std::vector<Vec3> wireVertices; // Empty means edges address vertices.
    std::vector<CollisionOverlayEdge> edges;
};
struct RenderCollisionOverlay {
    std::shared_ptr<const CollisionOverlayGeometry> geometry;
    Mat4 world{};
    Vec4 color{}; // Display-referred sRGB and authored/effective wire alpha.
    float fillAlphaScale{1.f/16.f};
};
} // namespace crimson
