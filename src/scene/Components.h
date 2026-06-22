#pragma once

#include "core/Types.h"
#include "math/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace quader {

using ModelingObjectId = std::int32_t;
inline constexpr ModelingObjectId InvalidModelingObjectId = -1;

struct EntityIdentityComponent {
    EntityId id = InvalidEntityId;
};

struct NameComponent {
    std::string name;
};

struct TransformComponent {
    Vec3 position = {};
    Vec3 rotationEuler = {};
    Vec3 scale = {1.0f, 1.0f, 1.0f};

    Transform spatialTransform() const {
        return Transform{
                .position = position,
                .rotation = quatFromEulerRadians(rotationEuler),
                .scale = scale,
        };
    }
};

enum class EntityKind {
    Empty,
    Mesh,
    Light,
    Generic,
};

struct EntityKindComponent {
    EntityKind kind = EntityKind::Generic;
};

struct HierarchyComponent {
    EntityId parent = InvalidEntityId;
    std::vector<EntityId> children;
};

struct VisibilityComponent {
    bool visible = true;
    bool selectable = true;
};

struct LockComponent {
    bool locked = false;
};

struct SelectionComponent {
    bool selected = false;
};

struct ModelingObjectComponent {
    ModelingObjectId objectId = InvalidModelingObjectId;
    std::uint32_t materialSlot = 0;
};

enum class MeshCollisionType {
    Box,
    Sphere,
    Cylinder,
    ConvexHull,
    Mesh,
};

enum class MeshPhysicsBodyType {
    StaticBody,
    RigidBody,
};

struct MeshComponent {
    AssetId mesh = InvalidAssetId;
    bool editorOnly = false;
    bool castShadows = true;
    bool doubleSided = false;
    bool generateCollisions = true;
    MeshCollisionType collisionType = MeshCollisionType::Box;
    bool displayCollision = false;
    MeshPhysicsBodyType bodyType = MeshPhysicsBodyType::StaticBody;
};

struct MaterialComponent {
    AssetId material = InvalidAssetId;
};

enum class LightType {
    Directional,
    Point,
    Spot
};

struct LightComponent {
    LightType type = LightType::Directional;
    Vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 80000.0f;
    bool castsShadows = false;
};

} // namespace quader
