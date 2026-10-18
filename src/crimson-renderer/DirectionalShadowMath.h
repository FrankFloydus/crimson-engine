#pragma once
#include "PointShadowMath.h"
#include "resources/StreamingModel.h"
#include <array>
#include <span>

namespace crimson::directional_shadow {
inline constexpr unsigned CascadeCount=4,MaxLights=4,WorldCameraCount=2;
struct AtlasLayout {
    point_shadow::AtlasLayout points;
    std::uint16_t resolution{},pitch{},width{},height{},directionalCapacity{},cameraCount{},directionalY{},blockColumns{};
    std::size_t bytes() const {return std::size_t(width)*height*sizeof(float);}
};
AtlasLayout atlasLayout(unsigned resolution,unsigned pointCapacity,unsigned directionalCapacity,unsigned cameraCount);
point_shadow::TileRect blockRect(const AtlasLayout&,unsigned lightSlot,unsigned cameraContext);
point_shadow::TileRect tileRect(const AtlasLayout&,unsigned lightSlot,unsigned cameraContext,unsigned cascade);
// Maps a full projection clip square to a tile of the light's contiguous view
// rectangle. Scene, Game and thumbnail blocks each contain four 2x2 tiles.
Vec4 casterTile(unsigned cascade);
struct Cascade {
    Mat4 view{},projection{},receiverViewProjection{};
    Vec3 right{},up{},forward{},center{};
    float radius{},texelWorld{},nearDepth{},farDepth{},splitNear{},splitFar{};
};
struct Cascades {
    std::array<Cascade,CascadeCount> values;
    Vec4 splits{};
    float distance{},tanHalfAngularDiameter{},maxPenumbra{};
    bool valid{};
    unsigned projectionCount{};
};
// Shared four-cascade PSSM (.8 logarithmic mix), stable sphere fit and texel snapping.
// Angular softness expands every inner projection by the same bounded world halo.
// Caster bounds extend the upstream depth range, independently of camera culling.
Cascades build(const DirectionalLight&,const ShadowCamera&,unsigned resolution,std::span<const Aabb> casterBounds={});
bool intersects(const Aabb&,const Cascade&);
// LOD error is projected with a linear orthographic footprint, not distance to a light.
std::uint32_t chooseLod(std::span<const streaming::ModelLod>,const Mat4&,float texelWorld,float allowedPixels=1.f);
} // namespace crimson::directional_shadow
