////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "core/types.hpp"
#include "core/scene_environment_catalog.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

using ModelingObjectId = std::int32_t;
inline constexpr ModelingObjectId kInvalidModelingObjectId = -1;

using EditorObjectId = std::uint64_t;
inline constexpr EditorObjectId kInvalidEditorObjectId = 0;

/**
 * Represents an Entity Identity Component value used by the scene graph document layer.
 */
struct EntityIdentityComponent {
  EntityId id = kInvalidEntityId;
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
  EditorObjectId id = kInvalidEditorObjectId;
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
    QVec3 rotation_euler = {};
    QVec3 scale = {1.0f, 1.0f, 1.0f};
    bool custom_pivot = false;

    QTransform spatial_transform() const {
      return QTransform{
          .position = position,
          .rotation = quat_from_euler_radians(rotation_euler),
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
  EntityId parent = kInvalidEntityId;
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
  ModelingObjectId object_id = kInvalidModelingObjectId;
  std::uint32_t material_slot = 0;
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
  AssetId mesh = kInvalidAssetId;
  bool editor_only = false;
  bool cast_shadows = true;
  bool double_sided = false;
  bool generate_collisions = true;
  MeshCollisionType collision_type = MeshCollisionType::Box;
  bool display_collision = false;
  MeshPhysicsBodyType body_type = MeshPhysicsBodyType::StaticBody;
};

/**
 * Represents a Material Component value used by the scene graph document layer.
 */
struct MaterialComponent {
  MaterialReference material;
  std::unordered_map<std::uint32_t, MaterialReference> material_slots;

  [[nodiscard]] MaterialReference
  material_for_slot(std::uint32_t slot) const {
    const auto found = material_slots.find(slot);
    return found == material_slots.end() ? material : found->second;
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
    float intensity = 1.0f;
    float range = 10.0f;
    bool casts_shadows = false;

    [[nodiscard]] bool operator==(const LightComponent& other) const {
      return type == other.type && color.x == other.color.x &&
             color.y == other.color.y && color.z == other.color.z &&
             intensity == other.intensity && range == other.range &&
             casts_shadows == other.casts_shadows;
    }
};

/**
 * Stores scene-wide environment settings on the real scene root entity.
 */
struct SceneEnvironmentSettings {
  std::string environment_id = std::string(kDefaultSceneEnvironmentId);
  bool enabled = true;
  bool show_background = true;
  QVec3 clear_color = {kDefaultSceneClearColorChannel,
                       kDefaultSceneClearColorChannel,
                       kDefaultSceneClearColorChannel};
  float intensity = 1.0f;
  float background_intensity = 1.0f;
  float rotation_y = 0.0f;

  [[nodiscard]] bool operator==(const SceneEnvironmentSettings &other) const {
    return environment_id == other.environment_id && enabled == other.enabled &&
           show_background == other.show_background &&
           clear_color.x == other.clear_color.x &&
           clear_color.y == other.clear_color.y &&
           clear_color.z == other.clear_color.z &&
           intensity == other.intensity &&
           background_intensity == other.background_intensity &&
           rotation_y == other.rotation_y;
  }
};

/**
 * Stores scene-wide camera exposure settings on the real scene root entity.
 */
struct SceneCameraSettings {
    float aperture = 16.0f;
    float shutter_speed = 1.0f / 125.0f;
    float sensitivity = 100.0f;
    float near_plane = 0.05f;
    float far_plane = 1000.0f;

    [[nodiscard]] bool operator==(const SceneCameraSettings& other) const {
      return aperture == other.aperture &&
             shutter_speed == other.shutter_speed &&
             sensitivity == other.sensitivity &&
             near_plane == other.near_plane && far_plane == other.far_plane;
    }
};

/**
 * Selects the scene-wide rendered viewport tone mapping operator.
 */
enum class SceneToneMapping {
    Linear,
    AcesLegacy,
    Aces,
    Filmic,
    DisplayRange,
};

/**
 * Stores scene-wide viewport post-process settings on the real scene root entity.
 */
struct ScenePostProcessSettings {
    bool enabled = true;
    bool anti_aliasing = true;
    bool ambient_occlusion = false;
    float ambient_occlusion_intensity = 1.0f;
    float ambient_occlusion_radius = 0.3f;
    bool bloom = false;
    float bloom_strength = 0.1f;
    bool dithering = true;
    SceneToneMapping tone_mapping = SceneToneMapping::Aces;

    [[nodiscard]] bool operator==(const ScenePostProcessSettings& other) const {
      return enabled == other.enabled && anti_aliasing == other.anti_aliasing &&
             ambient_occlusion == other.ambient_occlusion &&
             ambient_occlusion_intensity == other.ambient_occlusion_intensity &&
             ambient_occlusion_radius == other.ambient_occlusion_radius &&
             bloom == other.bloom && bloom_strength == other.bloom_strength &&
             dithering == other.dithering && tone_mapping == other.tone_mapping;
    }
};

/**
 * Marks the real scene root entity and stores global scene render settings.
 */
struct SceneSettingsComponent {
    SceneEnvironmentSettings environment;
    SceneCameraSettings camera;
    ScenePostProcessSettings post_process;

    [[nodiscard]] bool operator==(const SceneSettingsComponent& other) const {
      return environment == other.environment && camera == other.camera &&
             post_process == other.post_process;
    }
};

} // namespace quader
