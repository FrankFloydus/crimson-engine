////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

using ModelingObjectId = std::int32_t;
inline constexpr ModelingObjectId InvalidModelingObjectId = -1;

using EditorObjectId = std::uint64_t;
inline constexpr EditorObjectId InvalidEditorObjectId = 0;

/**
 * Represents an Entity Identity Component value used by the scene graph document layer.
 */
struct EntityIdentityComponent {
    EntityId id = InvalidEntityId;
};

/**
 * Represents a stable UUID assigned to a scene entity.
 */
struct EntityUuidComponent {
    std::string uuid;
};

/**
 * Represents an Editor Object Identity Component value used by the scene graph document layer.
 */
struct EditorObjectIdentityComponent {
    EditorObjectId id = InvalidEditorObjectId;
};

/**
 * Represents a Name Component value used by the scene graph document layer.
 */
struct NameComponent {
    std::string name;
};

/**
 * Represents a Transform Component value used by the scene graph document layer.
 */
struct TransformComponent {
    QVec3 position = {};
    QVec3 rotationEuler = {};
    QVec3 scale = {1.0f, 1.0f, 1.0f};

    QTransform spatialTransform() const {
        return QTransform{
                .position = position,
                .rotation = quatFromEulerRadians(rotationEuler),
                .scale = scale,
        };
    }
};

/**
 * Enumerates EntityKind values used by the scene layer.
 */
enum class EntityKind {
    Empty,
    Mesh,
    Light,
    Generic,
};

/**
 * Represents an Entity Kind Component value used by the scene graph document layer.
 */
struct EntityKindComponent {
    EntityKind kind = EntityKind::Generic;
};

/**
 * Represents a Hierarchy Component value used by the scene graph document layer.
 */
struct HierarchyComponent {
    EntityId parent = InvalidEntityId;
    std::vector<EntityId> children;
};

/**
 * Represents a Visibility Component value used by the scene graph document layer.
 */
struct VisibilityComponent {
    bool visible = true;
    bool selectable = true;
};

/**
 * Represents a Lock Component value used by the scene graph document layer.
 */
struct LockComponent {
    bool locked = false;
};

/**
 * Represents a Selection Component value used by the scene graph document layer.
 */
struct SelectionComponent {
    bool selected = false;
};

/**
 * Represents a Modeling Object Component value used by the scene graph document layer.
 */
struct ModelingObjectComponent {
    ModelingObjectId objectId = InvalidModelingObjectId;
    std::uint32_t materialSlot = 0;
};

/**
 * Enumerates MeshCollisionType values used by the scene layer.
 */
enum class MeshCollisionType {
    Box,
    Sphere,
    Cylinder,
    ConvexHull,
    Mesh,
};

/**
 * Enumerates MeshPhysicsBodyType values used by the scene layer.
 */
enum class MeshPhysicsBodyType {
    StaticBody,
    RigidBody,
};

/**
 * Represents a Mesh Component value used by the scene graph document layer.
 */
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

/**
 * Represents a Material Component value used by the scene graph document layer.
 */
struct MaterialComponent {
    AssetId material = InvalidAssetId;
    std::unordered_map<std::uint32_t, AssetId> materialSlots;

    [[nodiscard]] AssetId materialForSlot(std::uint32_t slot) const {
        const auto found = materialSlots.find(slot);
        return found == materialSlots.end() ? material : found->second;
    }

    [[nodiscard]] bool operator==(const MaterialComponent&) const = default;
};

/**
 * Enumerates LightType values used by the scene layer.
 */
enum class LightType {
    Directional,
    Point,
    Spot
};

/**
 * Represents a Light Component value used by the scene graph document layer.
 */
struct LightComponent {
    LightType type = LightType::Directional;
    QVec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 80000.0f;
    bool castsShadows = false;
};

} // namespace quader
