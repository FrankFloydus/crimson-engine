#pragma once

#include "math/Math.h"
#include "crimson-renderer/RenderTypes.h"
#include "resources/AssetRef.h"
#include <entt/entt.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson {
struct ModelAsset;

using EntityId = uint64_t;
inline constexpr EntityId NoEntity = 0;
inline constexpr size_t MaximumDirectionalLights = 4;
inline constexpr size_t MaximumPointLights = 16;

struct Identity { EntityId id{}; std::string name; };
struct Transform { Mat4 local{}; };
struct Relationship { EntityId parent{}; uint32_t order{}; };
struct ModelReference { AssetRef<ModelAsset> assetId; };
struct SceneDirectionalLight {
    Vec3 color{1.0f, 0.96f, 0.9f};
    float intensity{2.0f};
    bool enabled{true};
};
struct ScenePointLight {
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{35.0f};
    bool enabled{true};
};
// Global lighting settings are authored on ordinary entities. Spatial transforms
// remain part of their hierarchy but do not alter these global settings.
struct SceneEnvironment {
    EnvironmentSettings environment{};
    float exposure{1.0f};
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
};
struct SceneProperties {
    EntityId activeEnvironment{NoEntity};
};

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
    static void validate(const std::vector<EntityRecord>& records, const SceneProperties& properties);
    // Validates and prepares a complete candidate before replacing this registry.
    void apply(const std::vector<EntityRecord>& records, const SceneProperties& properties);
    // Final publication of an already validated/allocated registry. No allocation
    // remains after a durable project transaction has succeeded.
    void adoptPrepared(Scene& candidate) noexcept;
private:
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> entities_;
    SceneProperties properties_;
    uint64_t generation_{};
    uint64_t revision_{};
};

bool sameEntity(const EntityRecord& a, const EntityRecord& b);
bool sameProperties(const SceneProperties& a, const SceneProperties& b);
bool sameEnvironment(const SceneEnvironment& a, const SceneEnvironment& b);
// No active entity resolves to the built-in studio environment and exposure 1.
SceneEnvironment resolveEnvironment(const Scene& scene);

} // namespace crimson
