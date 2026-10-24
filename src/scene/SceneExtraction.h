#pragma once

#include "Scene.h"
#include "physics/StaticCollision.h"

#include <functional>
#include <optional>
#include <span>

namespace crimson {
struct ModelPose;

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
    bool visible{true}; // Hidden models retain transient animation clocks.
    EntityId actorOwner{};
    MeshShading meshShading{MeshShading::UseMeshNormals};
    std::optional<PrimitiveMeshSettings> primitive;
    bool receiveDecals{true};
    SceneModelRagdoll ragdoll;
    std::shared_ptr<const ModelPose> physicsPose; // Transient, immutable per-Play presentation.
    std::optional<SceneMeshPaint> meshPaint;
};
struct ExtractedDecal {EntityId id{};Mat4 world{};SceneDecal decal;bool visible{true};};
struct ExtractedParticle {
    EntityId id{};
    std::string assetId;
    Mat4 world{};
    bool visible{true}; // Retained for simulation; only presentation is hidden.
    bool receiveDecals{true};
};
struct ExtractedScene {
    std::vector<ExtractedModel> models;
    std::vector<DirectionalLight> directional;
    std::vector<PointLight> points;
    SceneEnvironment environment;
    ProceduralSun proceduralSun;
    std::vector<ExtractedParticle> particles;
    std::vector<ExtractedDecal> decals;
};

// The scene owns ECS traversal. Rendering receives copied presentation values,
// with no EnTT handles, registry pointers, component pointers, or GPU ownership.
// No time samples authored base illumination (including Temperature mode).
// A supplied time evaluates every light once for the shared Scene/Game frame.
ExtractedScene extractScene(const Scene& scene,const NavigationAgentProfileQuery& profiles={},
                            std::optional<double> lightSeconds=std::nullopt);
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
using DecalBoundsQuery = std::function<std::optional<Aabb>(EntityId)>; // Prepared patch bounds in world space.
// Parent selections include descendants once. Missing models, empty entities
// and lights get a small, fixed world-space box around their world position.
Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds,const ParticleBoundsQuery& particleBounds = {},const CollisionMeshQuery& meshCollision = {},const NavigationAgentProfileQuery& profiles={},const DecalBoundsQuery& decalBounds={});

struct SelectionBox { EntityId id{};Aabb local;Mat4 world{}; };
// Outlines keep the selected root's local axes, including affine parenting.
// Model/particle content bounds take precedence over auxiliary colliders and
// empty hierarchy markers. Nonvisual selections fall back to their own shapes.
// Particle markers are authored/stable; live effect bounds belong to framing.
std::vector<SelectionBox> selectionBoxes(const Scene& scene,std::span<const EntityId> selection,
    const ModelBoundsQuery& modelBounds,const CollisionMeshQuery& meshCollision = {},const NavigationAgentProfileQuery& profiles={},const DecalBoundsQuery& decalBounds={});

// Collision interiors are editor-selectable even without a rendered model.
// Used only when the material-aware GPU model pick reports background.
std::optional<EntityId> pickCollisionShape(const Scene& scene,const Ray& ray,float maximumDistance,const CollisionMeshQuery& meshCollision = {},const NavigationAgentProfileQuery& profiles={},bool includeCollisions=true);

} // namespace crimson
