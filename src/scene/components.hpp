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

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

using ModelingObjectId = std::int32_t;
inline constexpr ModelingObjectId kInvalidModelingObjectId = -1;
inline constexpr std::uint32_t
    kLegacyUnknownModelingObjectLocalFrameVersion = 0u;
inline constexpr std::uint32_t kCanonicalModelingObjectLocalFrameVersion = 1u;

using EditorObjectId = std::uint64_t;
inline constexpr EditorObjectId kInvalidEditorObjectId = 0;

using SceneLayerId = std::uint64_t;
inline constexpr SceneLayerId kInvalidSceneLayerId = 0;
inline constexpr QVec3 kDefaultSceneLayerWireframeColor = {
    238.0f / 255.0f,
    242.0f / 255.0f,
    247.0f / 255.0f,
};

/**
 * Stores one scene-owned display layer.
 */
struct SceneLayerRecord {
  SceneLayerId id = kInvalidSceneLayerId;
  std::string name;
  bool visible = true;
  QVec3 wireframe_color = kDefaultSceneLayerWireframeColor;
  bool locked = false;

  [[nodiscard]] bool operator==(const SceneLayerRecord &other) const {
    return id == other.id && name == other.name && visible == other.visible &&
           wireframe_color.x == other.wireframe_color.x &&
           wireframe_color.y == other.wireframe_color.y &&
           wireframe_color.z == other.wireframe_color.z &&
           locked == other.locked;
  }
};

/**
 * Assigns one public scene entity to at most one display layer.
 */
struct SceneLayerMembershipComponent {
  SceneLayerId layer_id = kInvalidSceneLayerId;

  [[nodiscard]] bool
  operator==(const SceneLayerMembershipComponent &) const = default;
};

/**
 * Stores one exact entity-to-layer assignment in a scene-layer state.
 */
struct SceneLayerAssignment {
  EntityId entity = kInvalidEntityId;
  SceneLayerId layer_id = kInvalidSceneLayerId;

  [[nodiscard]] bool operator==(const SceneLayerAssignment &) const = default;
};

/**
 * Captures the complete scene-layer state used by persistence and undo/redo.
 */
struct SceneLayerState {
  std::vector<SceneLayerRecord> layers;
  std::vector<SceneLayerAssignment> assignments;

  [[nodiscard]] bool operator==(const SceneLayerState &) const = default;
};

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
    Empty = 0,
    Mesh = 1,
    Light = 2,
    Generic = 3,
    Instance = 4,
    Curve = 5,
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
  // True when authored topology and interactive modeling state are owned by
  // the portable SDK session rather than the legacy native runtime.
  bool sdk_owned = false;
  // Identifies whether the authored vertices use Quader's canonical
  // object-local coordinate frame. Zero deliberately also represents older
  // files whose local-frame convention was not recorded.
  std::uint32_t local_frame_version =
      kLegacyUnknownModelingObjectLocalFrameVersion;
};

/**
 * Maps an occurrence's local positions into its texture sampling frame.
 *
 * Missing components mean identity. Face-owned UV settings remain in the
 * modeling document; this component only compensates scene placement edits.
 */
struct TextureMappingComponent {
  QMat4 sampling_from_local;

  [[nodiscard]] bool operator==(const TextureMappingComponent &other) const {
    return sampling_from_local.values == other.sampling_from_local.values;
  }
};

/**
 * Marks an internal, scene-local instance definition root.
 *
 * Canonical definition members are derived from this entity's hierarchy
 * children. The marker intentionally carries no second member list.
 */
struct InstanceDefinitionComponent {
  // Keep this marker non-empty because EnTT may otherwise use empty-type
  // optimization and expose no addressable component value.
  bool internal = true;
};

/**
 * Makes a leaf scene entity an occurrence of a scene-local definition.
 */
struct InstanceOccurrenceComponent {
  EntityId definition = kInvalidEntityId;

  [[nodiscard]] bool
  operator==(const InstanceOccurrenceComponent &) const = default;
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
  MeshCollisionType collision_type = MeshCollisionType::Mesh;
  bool display_collision = true;
  MeshPhysicsBodyType body_type = MeshPhysicsBodyType::StaticBody;

  [[nodiscard]] bool operator==(const MeshComponent &) const = default;
};

/// Generation currently supports triangle mesh collisions on static bodies.
[[nodiscard]] inline constexpr bool
mesh_collision_supported(const MeshComponent &mesh) noexcept {
  return mesh.collision_type == MeshCollisionType::Mesh &&
         mesh.body_type == MeshPhysicsBodyType::StaticBody;
}

inline constexpr int kCurveSolidifyMinimumResolution = 3;
inline constexpr int kCurveSolidifyMaximumResolution = 128;
inline constexpr int kCurveSolidifyDefaultResolution = 8;
inline constexpr double kCurveSolidifyMinimumRadius = 0.001;
inline constexpr double kCurveSolidifyMaximumRadius = 100000.0;
inline constexpr double kCurveSolidifyDefaultRadius = 0.1;

/// Binds one native scene entity to an immutable SDK curve asset projection.
struct CurveComponent {
  AssetId curve = kInvalidAssetId;
  bool solidify = false;
  int solidify_resolution = kCurveSolidifyDefaultResolution;
  float solidify_radius = static_cast<float>(kCurveSolidifyDefaultRadius);

  [[nodiscard]] bool operator==(const CurveComponent &) const = default;
};

[[nodiscard]] inline bool
curve_component_is_valid(const CurveComponent &component) {
  return component.curve != kInvalidAssetId &&
         component.solidify_resolution >= kCurveSolidifyMinimumResolution &&
         component.solidify_resolution <= kCurveSolidifyMaximumResolution &&
         std::isfinite(component.solidify_radius) &&
         component.solidify_radius >= kCurveSolidifyMinimumRadius &&
         component.solidify_radius <= kCurveSolidifyMaximumRadius;
}

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
    float distance_attenuation = 2.0f;

    [[nodiscard]] bool operator==(const LightComponent& other) const {
      return type == other.type && color.x == other.color.x &&
             color.y == other.color.y && color.z == other.color.z &&
             intensity == other.intensity && range == other.range &&
             casts_shadows == other.casts_shadows &&
             distance_attenuation == other.distance_attenuation;
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
