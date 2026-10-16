#pragma once

#include "Scene.h"
#include "physics/StaticCollision.h"

#include <functional>
#include <span>

namespace crimson {

struct ExtractedModel {
    EntityId id{};
    std::string assetId;
    Mat4 world{};
    ModelMaterialOverrides materialOverrides;
    bool castShadows{true},receiveShadows{true};
    SurfaceLightingScopes lightingScopes{};
    std::uint32_t lightingCategory{lighting_scope::World};
    SceneModelAnimation animation{};
    double animationSeconds{}; // Transient sample time; never scene-authored.
};
struct ExtractedParticle {
    EntityId id{};
    std::string assetId;
    Mat4 world{};
};
struct ExtractedScene {
    std::vector<ExtractedModel> models;
    std::vector<DirectionalLight> directional;
    std::vector<PointLight> points;
    SceneEnvironment environment;
    ProceduralSun proceduralSun;
    std::vector<ExtractedParticle> particles;
    PlayerRevealSource playerReveal{};
    std::array<CharacterLight,2> characterLights{};
    CharacterShadowSource characterShadow{};
};

// The scene owns ECS traversal. Rendering receives copied presentation values,
// with no EnTT handles, registry pointers, component pointers, or GPU ownership.
ExtractedScene extractScene(const Scene& scene);
// Resolves the active entity camera, including positional follow. Editor camera
// and persisted thumbnail preview camera remain independent.
std::optional<Camera> resolveGameCamera(const Scene& scene);

// Direction in which emitted light travels. Use the authored local -Z axis,
// including affine parent transforms; scale affects its length, not intensity.
// The viewport marker and render extraction must consume this same direction.
Vec3 directionalLightDirection(const Mat4& world);

using ModelBoundsQuery = std::function<std::optional<Aabb>(const std::string&)>;
// Live particle bounds are already in world space and belong to the transient
// runtime. Use these for explicit focus/framing, not selection wires or pivots.
// Missing/stopped effects fall back to their authored marker position.
using ParticleBoundsQuery = std::function<std::optional<Aabb>(EntityId)>;
// Parent selections include descendants once. Missing models, empty entities
// and lights get a small, fixed world-space box around their world position.
Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds,const ParticleBoundsQuery& particleBounds = {},const CollisionMeshQuery& meshCollision = {});

// Collision interiors are editor-selectable even without a rendered model.
// Used only when the material-aware GPU model pick reports background.
std::optional<EntityId> pickCollisionShape(const Scene& scene,const Ray& ray,float maximumDistance,const CollisionMeshQuery& meshCollision = {});

} // namespace crimson
