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
    bool fadeWhenOccluding{false};
    bool visible{true}; // Hidden models retain transient animation clocks.
    EntityId actorOwner{};
};
struct ExtractedParticle {
    EntityId id{};
    std::string assetId;
    Mat4 world{};
    bool visible{true}; // Retained for simulation; only presentation is hidden.
};
struct ExtractedScene {
    std::vector<ExtractedModel> models;
    std::vector<DirectionalLight> directional;
    std::vector<PointLight> points;
    SceneEnvironment environment;
    ProceduralSun proceduralSun;
    std::vector<ExtractedParticle> particles;
    std::vector<DarknessRevealSource> reveals;
    std::vector<ActorLighting> actorLighting;
    std::vector<CharacterShadowSource> actorShadows;
};

// The scene owns ECS traversal. Rendering receives copied presentation values,
// with no EnTT handles, registry pointers, component pointers, or GPU ownership.
ExtractedScene extractScene(const Scene& scene);
// Resolve any authored camera, including positional follow and full affine
// parenting. Visibility controls its marker, not its view/control behavior.
std::optional<Camera> resolveSceneCamera(const Scene& scene,EntityId id);
// Resolves the active entity camera. Editor and thumbnail cameras are independent.
std::optional<Camera> resolveGameCamera(const Scene& scene);
// Convert a navigated world view into this camera's local affine pose. The
// camera's view-relative stretch/shear/reflection is retained; lens and follow
// settings are not authored here. Invalid frames/singular ancestry return none.
std::optional<Mat4> sceneCameraLocalTransform(const Scene& scene,EntityId id,const Camera& navigated);

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

struct SelectionBox { EntityId id{};Aabb local;Mat4 world{}; };
// Outlines keep the selected root's local axes, including affine parenting.
// Model/particle content bounds take precedence over auxiliary colliders and
// empty hierarchy markers. Nonvisual selections fall back to their own shapes.
// Particle markers are authored/stable; live effect bounds belong to framing.
std::vector<SelectionBox> selectionBoxes(const Scene& scene,std::span<const EntityId> selection,
    const ModelBoundsQuery& modelBounds,const CollisionMeshQuery& meshCollision = {});

// Collision interiors are editor-selectable even without a rendered model.
// Used only when the material-aware GPU model pick reports background.
std::optional<EntityId> pickCollisionShape(const Scene& scene,const Ray& ray,float maximumDistance,const CollisionMeshQuery& meshCollision = {});

} // namespace crimson
