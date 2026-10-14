#pragma once

#include "math/Math.h"
#include "crimson-renderer/RenderTypes.h"
#include "resources/AssetRef.h"
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

using EntityId = uint64_t;
inline constexpr EntityId NoEntity = 0;
inline constexpr size_t MaximumDirectionalLights = 4;
inline constexpr size_t MaximumPointLights = 16;

struct Identity { EntityId id{}; std::string name; };
struct Transform { Mat4 local{}; };
struct Relationship { EntityId parent{}; uint32_t order{}; };
struct ModelReference {
    AssetRef<ModelAsset> assetId;
    AssetRef<MaterialAsset> material;
    bool castShadows{true},receiveShadows{true};
};
// Authored identity only. Playback is transient and reconciled by document
// generation plus persistent EntityId, never by an EnTT handle.
struct ParticleReference { AssetRef<ParticleEffectAsset> assetId; };
struct SceneDirectionalLight {
    Vec3 color{1.0f, 0.96f, 0.9f};
    float intensity{2.0f};
    bool enabled{true};
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
    bool operator==(const ScenePointLight&) const = default;
};
// Global lighting settings are authored on ordinary entities. Spatial transforms
// remain part of their hierarchy but do not alter these global settings.
struct SceneEnvironment {
    EnvironmentSettings environment{};
    float exposure{1.0f};
    EntityId sun{NoEntity}; // Optional explicit directional-light reference.
};
struct SceneCharacter {
    float height{1.8f}, radius{.3f}, moveSpeed{5.f};
    bool showRoute{true},showCollisionInGame{false};
    Vec4 collisionColor{57.f/255.f,175.f/255.f,221.f/255.f,1.f};
    bool operator==(const SceneCharacter&) const = default;
};
struct SceneCollider {
    Vec3 size{1,1,1};
    bool showCollisionInGame{false};
    Vec4 collisionColor{57.f/255.f,175.f/255.f,221.f/255.f,1.f};
    bool operator==(const SceneCollider&) const = default;
};
struct SceneCamera {
    float verticalFov{45.f}, nearPlane{.1f}, farPlane{2048.f};
    EntityId followTarget{NoEntity};
    Vec3 followOffset{}; // World-space positional offset; target rotation is not inherited.
    bool operator==(const SceneCamera&) const = default;
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
    std::optional<SceneCharacter> character;
    std::optional<SceneCollider> collider;
    std::optional<SceneCamera> camera;
    // Empty uses the shared model material. This slice permits overrides only
    // on built-in Box instances; the protected geometry remains immutable.
    AssetRef<MaterialAsset> modelMaterial;
    // Per rendered model entity; these settings are never inherited by children.
    bool castShadows{true},receiveShadows{true};
};
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
