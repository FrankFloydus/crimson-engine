#pragma once

#include "math/Math.h"
#include "CollisionMesh.h"
#include "SceneComponentKind.h"
#include "crimson-renderer/RenderTypes.h"
#include <crimson/renderer/forward/MeshShading.h>
#include "resources/AssetRef.h"
#include "assets/ModelMaterialOverrides.h"
#include "assets/NavigationAgentProfile.h"
#include "physics/CollisionFilter.h"
#include "gameplay/GameplayTags.h"
#include <entt/entt.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson {
struct ModelAsset;
struct ParticleEffectAsset;
struct MaterialAsset;
struct GameplayEffectAsset;

using EntityId = uint64_t;
inline constexpr EntityId NoEntity = 0;
inline constexpr size_t MaximumDirectionalLights = 4;
inline constexpr size_t MaximumPointLights = MaximumClusteredPointLights;

struct Identity { EntityId id{}; std::string name; };
struct Transform { Mat4 local{}; };
struct Relationship { EntityId parent{}; uint32_t order{}; };
struct SceneModelAnimation {
    std::int32_t clip{-1}; // -1 uses the imported bind pose and default morph weights.
    bool playing{true},loop{true};
    float speed{1},startTime{}; // Authored start; the live playback clock is transient.
    bool operator==(const SceneModelAnimation&) const = default;
};
struct ModelReference {
    AssetRef<ModelAsset> assetId;
    ModelMaterialOverrides materials;
    bool castShadows{true},receiveShadows{true};
    SurfaceLightingScopes lightingScopes{};
    SceneModelAnimation animation{};
    MeshShading meshShading{MeshShading::UseMeshNormals};
};
// Authored identity only. Playback is transient and reconciled by document
// generation plus persistent EntityId, never by an EnTT handle.
struct ParticleReference { AssetRef<ParticleEffectAsset> assetId; };
struct SceneDirectionalLight {
    Vec3 color{1.0f, 0.96f, 0.9f};
    float intensity{2.0f};
    bool enabled{true};
    bool castShadows{true};
    float shadowDistance{100.f};
    float shadowDepthBias{.005f};
    float shadowNormalBias{1.f};
    LightRoles roles{};
    float indirectEnergy{1.f};
    float shadowAngularDiameter{.53f}; // Apparent light diameter in degrees.
    bool operator==(const SceneDirectionalLight&) const = default;
};
struct ScenePointLight {
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{35.0f};
    bool enabled{true};
    float radius{20.f}; // World-space meters, independent of entity scale.
    bool castShadows{false};
    float shadowDepthBias{.005f}; // World-space meters.
    float shadowNormalBias{1.f}; // Shadow-map texels.
    LightRoles roles{};
    float nearFalloffRadius{0},specularStrength{1};
    float rangeFalloffExponent{4},edgeSoftness{1};
    float sourceRadius{0};
    float attenuation{2};
    float indirectEnergy{1.f};
    bool operator==(const ScenePointLight&) const = default;
};
// Global lighting settings are authored on ordinary entities. Spatial transforms
// remain part of their hierarchy but do not alter these global settings.
struct SceneEnvironment {
    EnvironmentSettings environment{};
    float exposure{1.0f};
    EntityId sun{NoEntity}; // Optional explicit directional-light reference.
};
// Explicit capabilities compose ordinary entities; Actor marks a visual boundary.
struct SceneActor { bool operator==(const SceneActor&) const = default; };
struct MovementComponent {
    bool enabled{true};
    float jumpHeight{1.1f}; // Meters, converted to takeoff speed by the motor.
    float groundAcceleration{45.f},groundFriction{8.f};
    float airAcceleration{12.f},airControl{.35f};
    float strafeTilt{3.f},tiltResponse{12.f}; // Degrees and exponential response per second.
    float stairSmoothingTime{.10f}; // Camera step response time in seconds; zero disables smoothing.
    bool operator==(const MovementComponent&) const = default;
};
struct SceneCharacterController {
    AssetRef<NavigationAgentProfile> profile{DefaultNavigationAgentProfileId};
    CollisionFilter collision;
    bool showCollisionInGame{false};
    Vec4 collisionColor{57.f/255.f,175.f/255.f,221.f/255.f,1.f};
    bool operator==(const SceneCharacterController&) const = default;
};
inline std::optional<AgentTraversalSettings> resolveAgentTraversal(const SceneCharacterController& controller,const NavigationAgentProfileQuery& query={}) {
    const auto profile=resolveNavigationAgentProfile(controller.profile.id,query);
    return profile?std::optional<AgentTraversalSettings>{profile->traversal}:std::nullopt;
}
struct SceneNavigationAgent {
    bool showRoute{true},enabled{true};
    bool operator==(const SceneNavigationAgent&) const = default;
};
enum class EnemyBehaviour : std::uint8_t { Idle, ChasePlayer, RandomMove };
struct SceneEnemy {
    EnemyBehaviour behaviour{EnemyBehaviour::Idle};
    float radius{5.f},minWait{1.f},maxWait{3.f};
    bool operator==(const SceneEnemy&) const = default;
};
struct SceneNavigationVolume {
    Vec3 size{20,10,20};
    bool enabled{true};
    NavigationBakeSettings bake;
    std::vector<AssetRef<NavigationAgentProfile>> profiles{AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId}};
    bool operator==(const SceneNavigationVolume&) const = default;
};
struct ScenePlayerInput {
    bool enabled{true};
    float mouseSensitivity{.10f}; // Degrees per logical mouse pixel.
    bool invertY{false};
    bool operator==(const ScenePlayerInput&) const = default;
};
struct SceneEffects {
    // Application order and repeated references are meaningful. Active handles,
    // captured values and clocks belong to the running simulation.
    std::vector<AssetRef<GameplayEffectAsset>> starting;
    bool operator==(const SceneEffects&) const = default;
};

enum class ColliderShape : std::uint8_t { Box, Sphere, Cylinder, Capsule, Trimesh };
struct SceneCollider {
    Vec3 size{1,1,1};
    bool showCollisionInGame{false};
    Vec4 collisionColor{57.f/255.f,175.f/255.f,221.f/255.f,1.f};
    ColliderShape shape{ColliderShape::Box};
    float radius{.5f},height{1.f}; // Height includes both capsule hemispheres.
    AssetRef<ModelAsset> source; // Retained when changing shape; used by Trimesh.
    std::shared_ptr<const CollisionMesh> mesh; // Native triangles, retained across primitive shape edits.
    ColliderMode mode{ColliderMode::Solid};
    CollisionFilter collision{WorldCollisionLayer,AllCollisionLayers};
    bool operator==(const SceneCollider& other) const {
        return size==other.size && showCollisionInGame==other.showCollisionInGame && collisionColor==other.collisionColor &&
            shape==other.shape && radius==other.radius && height==other.height && source==other.source && sameCollisionMesh(mesh,other.mesh) && mode==other.mode && collision==other.collision;
    }
};
struct QuaderLink {
    std::string projectPath,projectId,sceneId;
    float importScale{1.f};
    bool autoSync{true},refreshMaterials{false};
    std::string saveKey;
    bool syncLights{true};
    bool operator==(const QuaderLink&) const = default;
};
struct QuaderGenerated {
    EntityId owner{};
    std::string sourceKey;
    bool operator==(const QuaderGenerated&) const = default;
};
struct SceneCamera {
    float verticalFov{45.f}, nearPlane{.1f}, farPlane{2048.f};
    bool operator==(const SceneCamera&) const = default;
};
struct SceneCameraFollow {
    EntityId target{NoEntity};
    Vec3 offset{}; // World-space positional offset, independent of target rotation.
    Mat4 worldBasis{}; // Translation-free full affine world basis, including stretch/reflection.
    bool operator==(const SceneCameraFollow&) const = default;
};
struct SceneOutlinerStyle {
    std::optional<Vec4> color;
    bool operator==(const SceneOutlinerStyle&) const = default;
};
struct EntityRecord {
    EntityId id{};
    std::string name;
    EntityId parent{};
    uint32_t order{};
    Mat4 local{};
    std::optional<AssetRef<ModelAsset>> model;
    std::optional<SceneDirectionalLight> directional;
    std::optional<ScenePointLight> point;
    std::optional<SceneEnvironment> environment;
    // A present empty reference retains an editable emitter marker after an
    // explicit asset clear/deletion. A missing nonempty asset keeps its ID.
    std::optional<AssetRef<ParticleEffectAsset>> particle;
    std::optional<SceneActor> actor;
    std::optional<MovementComponent> movement;
    std::optional<SceneCharacterController> controller;
    std::optional<SceneNavigationAgent> navigation;
    std::optional<SceneEnemy> enemy;
    std::optional<GameplayTagContainer> tags;
    std::optional<SceneEffects> effects;
    std::optional<SceneNavigationVolume> navigationVolume;
    std::optional<ScenePlayerInput> playerInput;
    std::optional<SceneCollider> collider;
    std::optional<SceneCamera> camera;
    std::optional<SceneCameraFollow> cameraFollow;
    // Empty slots use their source material. Geometry and source materials
    // remain shared; assignments belong only to this entity.
    ModelMaterialOverrides modelMaterials;
    // Per rendered model entity; these settings are never inherited by children.
    bool castShadows{true},receiveShadows{true};
    std::optional<QuaderLink> quader;
    std::optional<QuaderGenerated> quaderGenerated;
    SurfaceLightingScopes lightingScopes{};
    SceneModelAnimation modelAnimation{};
    bool visible{true}; // Authored rendering visibility, inherited by descendants.
    std::optional<SceneOutlinerStyle> outlinerStyle;
    MeshShading meshShading{MeshShading::UseMeshNormals};
};
bool hasSceneComponent(const EntityRecord&,SceneComponentKind);
std::vector<SceneComponentKind> sceneComponents(const EntityRecord&);
void normalizeSceneOutlinerStyle(EntityRecord&);
struct SceneProperties {
    EntityId activeEnvironment{NoEntity};
    EntityId activeCamera{NoEntity};
    EntityId playerCharacter{NoEntity};
};
struct ScenePoseUpdate { EntityId id{}; Mat4 local{}; };

// Authoritative CPU world. EnTT handles and component pointers never escape
// serialization/history; the renderer consumes immutable extracted values.
class Scene {
public:
    Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    const entt::registry& registry() const { return registry_; }
    const SceneProperties& properties() const { return properties_; }
    std::vector<EntityRecord> records() const;
    std::optional<EntityRecord> find(EntityId id) const;
    bool contains(EntityId id) const { return entities_.contains(id); }
    bool isVisible(EntityId id) const; // Cached authored visibility AND all ancestors; false for missing IDs.
    std::optional<Mat4> worldTransform(EntityId id) const;
    std::vector<EntityId> children(EntityId parent = NoEntity) const;
    std::vector<EntityId> preorder() const;
    std::vector<EntityId> subtree(EntityId root) const;
    std::vector<EntityId> topmost(const std::vector<EntityId>& selection) const;
    uint64_t generation() const { return generation_; }
    uint64_t revision() const { return revision_; }
    uint64_t poseRevision() const { return poseRevision_; }
    static void validate(const std::vector<EntityRecord>& records, const SceneProperties& properties);
    // Validates and prepares a complete candidate before replacing this registry.
    void apply(const std::vector<EntityRecord>& records, const SceneProperties& properties);
    // Final publication of an already validated/allocated registry. No allocation
    // remains after a durable project transaction has succeeded.
    void adoptPrepared(Scene& candidate) noexcept;
    // Runtime-only callers publish complete fixed poses without rebuilding the
    // registry or invalidating asynchronous picks through the edit revision.
    void applyRuntimePoses(std::span<const ScenePoseUpdate> poses);
    // Presentation-only camera override; authored follow placement remains intact.
    void applyRuntimeCameraPose(EntityId camera,const Mat4& world);
private:
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> entities_;
    SceneProperties properties_;
    uint64_t generation_{};
    uint64_t revision_{};
    uint64_t poseRevision_{};
    std::vector<EntityId> cameraFollowOrder_;
    std::vector<ScenePoseUpdate> poseRollback_;
    bool resolveCameraFollow();
};

bool sameEntity(const EntityRecord& a, const EntityRecord& b);
bool sameProperties(const SceneProperties& a, const SceneProperties& b);
bool sameEnvironment(const SceneEnvironment& a, const SceneEnvironment& b);
// No active entity resolves to the built-in studio environment and exposure 1.
SceneEnvironment resolveEnvironment(const Scene& scene);

} // namespace crimson
