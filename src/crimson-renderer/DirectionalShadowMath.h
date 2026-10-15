#pragma once
#include "PointShadowMath.h"
#include "resources/StreamingModel.h"
#include <array>
#include <optional>
#include <span>

namespace crimson::directional_shadow {
inline constexpr unsigned CascadeCount=4,MaxLights=4,WorldCameraCount=2;
struct AtlasLayout {
    point_shadow::AtlasLayout points;
    std::uint16_t resolution{},pitch{},width{},height{},directionalCapacity{},cameraCount{},directionalY{},blockColumns{};
    bool gameplayContext{},characterShadow{};
    point_shadow::TileRect characterTile{};
    std::array<std::array<point_shadow::TileRect,MaxLights>,WorldCameraCount> directionalBlocks{};
    std::size_t bytes() const {return std::size_t(width)*height*sizeof(float);}
};
AtlasLayout atlasLayout(unsigned resolution,unsigned pointCapacity,unsigned directionalCapacity,unsigned cameraCount,bool gameplayContext=false,bool characterShadow=false);
point_shadow::TileRect blockRect(const AtlasLayout&,unsigned lightSlot,unsigned cameraContext);
point_shadow::TileRect tileRect(const AtlasLayout&,unsigned lightSlot,unsigned cameraContext,unsigned cascade);
// Maps a full projection clip square to a tile of the light's contiguous view
// rectangle. Legacy/thumbnail blocks are 2x2; active Scene/Game blocks are 4x1/2x1.
Vec4 casterTile(unsigned cascade,unsigned columns=2,unsigned rows=2);
struct Cascade {
    Mat4 view{},projection{},receiverViewProjection{};
    Vec3 right{},up{},forward{},center{};
    float radius{},texelWorld{},nearDepth{},farDepth{},splitNear{},splitFar{};
};
enum class ProjectionPolicy { Cascaded, Gameplay };
inline constexpr unsigned NoCharacterProjection=CascadeCount;
struct Cascades {
    std::array<Cascade,CascadeCount> values;
    Vec4 splits{};
    float distance{};
    bool valid{};
    unsigned projectionCount{};
    unsigned characterProjection{NoCharacterProjection};
    ProjectionPolicy policy{ProjectionPolicy::Cascaded};
};
// Stable sphere fit and texel snapping follow Godot's directional PSSM setup.
// Caster bounds extend the upstream depth range, independently of camera culling.
Cascades build(const DirectionalLight&,const ShadowCamera&,unsigned resolution,std::span<const Aabb> casterBounds={});
// Gameplay uses one stabilized projection for the bounded visible frustum.
// Optional character bounds add a sharper independent projection; only that
// subtree casts into it. Its depth includes downstream world receivers, since a
// character's shadow follows the same light-space XY footprint onto the floor.
// Receiver bounds crop world XY to their intersection with the bounded camera
// frustum. Casters still extend only upstream depth. Empty/unknown receivers or
// no receiver intersection conservatively retain the full-frustum fit.
// Invalid focus bounds omit the character projection; invalid camera/light
// input returns an invalid fit. Resolution must provide at least three texels.
Cascades buildGame(const DirectionalLight&,const ShadowCamera&,unsigned resolution,
    std::span<const Aabb> casterBounds={},std::optional<Aabb> focusBounds={},std::span<const Aabb> receiverBounds={});
// One camera-independent, stabilized player-caster map. The XY footprint is
// bounded by the player; light-space depth extends downstream by distance.
std::optional<Cascade> buildCharacter(const CharacterShadowSource&,const Aabb& casterBounds,unsigned resolution);
bool intersects(const Aabb&,const Cascade&);
// LOD error is projected with a linear orthographic footprint, not distance to a light.
std::uint32_t chooseLod(std::span<const streaming::ModelLod>,const Mat4&,float texelWorld,float allowedPixels=1.f);
} // namespace crimson::directional_shadow
