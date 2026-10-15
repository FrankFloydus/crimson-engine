#pragma once

#include "RenderTypes.h"
#include <cstddef>
#include <cstdint>

namespace crimson::point_shadow {
inline constexpr unsigned FaceCount=6,MaxLights=16,GuardTexels=2;
struct AtlasLayout {
    std::uint16_t resolution{},pitch{},columns{},rows{},width{},height{},capacity{};
    std::size_t bytes() const {return std::size_t(width)*height*sizeof(float);}
};
struct TileRect {std::uint16_t x{},y{},width{},height{};};
// Each light occupies a 3x2 block. Capacity rounds up to 1,2,4,8,16.
// Empty layouts reject invalid counts, zero resolution, and uint16 dimensions.
AtlasLayout atlasLayout(unsigned resolution,unsigned capacity);
TileRect tileRect(const AtlasLayout&,unsigned lightSlot,unsigned face);
float nearPlane(float radius);
float faceTanHalfFov(unsigned resolution);
// Face order +X,-X,+Y,-Y,+Z,-Z; equal magnitudes prefer X, then Y.
unsigned faceIndex(const Vec3& direction);
// Canonical 90-degree face UV, top-left origin. Invalid/zero direction is center.
Vec2 faceCoordinates(const Vec3& direction);
Camera faceCamera(Vec3 position,float radius,unsigned face,std::uint16_t resolution);
// Standard D3D [0,1] perspective depth from positive face-axis distance.
float projectedDepth(float faceDistance,float nearValue,float farValue);
// Inverse of a stored face depth, measured radially along the supplied ray.
// Ray length is arbitrary; its largest absolute component selects face depth.
// Clear depth (1) maps to the far face plane; callers exclude it as no blocker.
float radialDistanceFromDepth(float depth,Vec3 direction,float nearValue,float farValue);
// Similar-triangle emitter penumbra on a receiver beyond an average blocker.
// Invalid inputs and blockers on/behind the receiver produce a hard shadow.
float penumbraRadius(float sourceRadius,float receiverDistance,float blockerDistance);
// Perspective-depth change per inner-face texel on a planar receiver. The
// point is relative to the light, after normal/radial bias; normal is the actual
// triangle plane, not a smooth shading normal. Degenerate planes return zero.
Vec2 receiverPlaneGradient(Vec3 relativePoint,Vec3 planeNormal,unsigned face,
    unsigned resolution,float nearValue,float farValue);
// Conservative intersection, including invalid bounds so missing metadata never
// removes a possible caster. The frustum includes the guard texels.
bool intersectsFace(const Aabb&,const Camera&);
bool intersectsLight(const Aabb&,const Vec3& position,float radius);
// Conservative world-space pixel footprint of the supplied bounds. Aggregate
// models must include each primitive's bounds before combining LOD demand.
// An enclosing light or missing bounds requests original geometry/native mips.
float casterTexelWorld(const Aabb&,const Vec3& position,unsigned resolution);
} // namespace crimson::point_shadow
