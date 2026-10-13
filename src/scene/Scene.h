#pragma once

#include "math/Math.h"
#include "crimson-renderer/RenderTypes.h"
#include <entt/entt.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson {

using EntityId = uint64_t;
inline constexpr EntityId NoEntity = 0;
inline constexpr size_t MaximumDirectionalLights = 4;
inline constexpr size_t MaximumPointLights = 16;

struct Identity { EntityId id{}; std::string name; };
struct Transform { Mat4 local{}; };
struct Relationship { EntityId parent{}; uint32_t order{}; };
struct ModelReference { std::string assetId; };
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
struct EntityRecord {
    EntityId id{};
    std::string name;
    EntityId parent{};
    uint32_t order{};
    Mat4 local{};
    std::optional<std::string> model;
    std::optional<SceneDirectionalLight> directional;
    std::optional<ScenePointLight> point;
};
struct SceneProperties {
    EnvironmentSettings environment{};
    float exposure{1.0f};
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
private:
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> entities_;
    SceneProperties properties_;
    uint64_t generation_{};
    uint64_t revision_{};
};

bool sameEntity(const EntityRecord& a, const EntityRecord& b);
bool sameProperties(const SceneProperties& a, const SceneProperties& b);

} // namespace crimson
