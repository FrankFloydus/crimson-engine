////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include <editor/modeling/runtime/session.hpp>

namespace quader {

class AssetRegistry;
class AssetRegistryProjectionCheckpoint;
struct CurveAsset;
struct MaterialComponent;
struct MeshComponent;
struct ModelingObjectComponent;
struct ModelingObjectSceneIdentity;
class SceneDocument;
class SceneProjectionCheckpoint;
struct SceneTransformAssignment;
class SceneModelingSyncService;
struct TransformComponent;
struct TextureMappingComponent;

/// Reports sharing and compilation work from the latest mesh projection pass.
struct ModelingMeshSyncStats final {
  std::uint64_t participant_count = 0u;
  std::uint64_t unique_topology_count = 0u;
  std::uint64_t unique_authored_topology_bytes = 0u;
  std::uint64_t unique_compiled_payload_bytes = 0u;
  std::uint64_t compiled_topology_count = 0u;
  std::uint64_t reused_compilation_count = 0u;
  std::uint64_t compile_time_microseconds = 0u;
  bool skipped_unchanged = false;
};

/**
 * Owns the modeling runtime to SceneDocument / AssetRegistry synchronization boundary.
 *
 * Modeling runtime code that needs to create, destroy, or update scene modeling
 * entities and projected geometry assets should route through this service instead
 * of mutating SceneDocument or AssetRegistry directly.
 */
class ModelingMeshSyncService {
public:
  [[nodiscard]] static bool should_force_refresh(bool preview_active,
                                                 bool preview_finished);

  [[nodiscard]] const ModelingMeshSyncStats &last_sync_stats() const {
    return last_sync_stats_;
  }

  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(const quader_editor_core::PolygonObject &object) const;
  [[nodiscard]] AssetId install_projected_mesh(AssetRegistry &assets,
                                               MeshAsset mesh) const;
  [[nodiscard]] bool replace_projected_mesh(AssetRegistry &assets,
                                            AssetId mesh_id,
                                            MeshAsset mesh) const;
  [[nodiscard]] AssetId install_projected_curve(AssetRegistry &assets,
                                               CurveAsset curve) const;
  [[nodiscard]] bool replace_projected_curve(AssetRegistry &assets,
                                            AssetId curve_id,
                                            CurveAsset curve) const;
  [[nodiscard]] EntityId create_projected_mesh_entity(
      SceneDocument &scene, const std::string &name) const;
  [[nodiscard]] EntityId create_projected_curve_entity(
      SceneDocument &scene, const std::string &name) const;
  [[nodiscard]] EntityId restore_projected_mesh_entity(
      SceneDocument &scene,
      const ModelingObjectSceneIdentity &identity) const;
  [[nodiscard]] bool destroy_projected_entity(SceneDocument &scene,
                                              EntityId entity) const;
  [[nodiscard]] bool parent_projected_entity(SceneDocument &scene,
                                             EntityId entity,
                                             EntityId parent) const;
  [[nodiscard]] bool set_projected_transform(
      SceneDocument &scene, EntityId entity,
      const TransformComponent &transform) const;
  [[nodiscard]] bool set_projected_texture_mapping(
      SceneDocument &scene, EntityId entity,
      const TextureMappingComponent &mapping) const;
  [[nodiscard]] bool set_projected_mesh_component(
      SceneDocument &scene, EntityId entity, const MeshComponent &mesh) const;
  [[nodiscard]] bool set_projected_modeling_object_component(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectComponent &modeling_object) const;
  [[nodiscard]] bool bind_projected_object(
      SceneDocument &scene, EntityId entity, int modeling_object_id,
      std::uint32_t material_slot, bool sdk_owned, AssetId mesh_id,
      std::optional<std::uint32_t> local_frame_version = std::nullopt) const;
  [[nodiscard]] bool bind_projected_curve_object(
      SceneDocument &scene, EntityId entity, int modeling_object_id,
      AssetId curve_id,
      std::optional<std::uint32_t> local_frame_version = std::nullopt) const;
  [[nodiscard]] bool mark_projected_object_sdk_owned(
      SceneDocument &scene, EntityId entity) const;
  [[nodiscard]] bool set_projected_mesh_binding(SceneDocument &scene,
                                                EntityId entity,
                                                AssetId mesh_id) const;
  [[nodiscard]] bool set_projected_curve_binding(SceneDocument &scene,
                                                 EntityId entity,
                                                 AssetId curve_id) const;
  [[nodiscard]] bool set_projected_material(
      SceneDocument &scene, EntityId entity,
      const MaterialComponent *material) const;
  [[nodiscard]] bool notify_projected_mesh_changed(SceneDocument &scene,
                                                   EntityId entity) const;
  [[nodiscard]] bool notify_projected_curve_changed(SceneDocument &scene,
                                                    EntityId entity) const;
  void apply_scene_identity_to_entity(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectSceneIdentity &identity) const;
  [[nodiscard]] bool set_entity_material(
      SceneDocument &scene, EntityId entity,
      const MaterialComponent &material) const;
  [[nodiscard]] bool restore_history_mesh_assets(
      AssetRegistry &assets,
      const std::unordered_map<AssetId, MeshAsset> &mesh_assets) const;
  void remove_unreferenced_history_mesh_assets(
      const SceneDocument &scene, AssetRegistry &assets,
      std::span<const AssetId> mesh_assets) const;
  [[nodiscard]] bool restore_history_curve_assets(
      AssetRegistry &assets,
      const std::unordered_map<AssetId, CurveAsset> &curve_assets) const;
  void remove_unreferenced_history_curve_assets(
      const SceneDocument &scene, AssetRegistry &assets,
      std::span<const AssetId> curve_assets) const;
  void apply_scene_identity_to_duplicate_entity(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectSceneIdentity &identity) const;
  void apply_scene_transforms(
      SceneDocument &scene,
      std::span<const SceneTransformAssignment> transforms) const;
  [[nodiscard]] bool restore_projected_asset_checkpoint(
      AssetRegistry &assets,
      const AssetRegistryProjectionCheckpoint &checkpoint) const;
  [[nodiscard]] bool restore_projected_scene_checkpoint(
      SceneDocument &scene, const SceneProjectionCheckpoint &checkpoint) const;
  void establish_loaded_scene_sync_baseline(
      const quader_editor_core::EditorSession &session,
      SceneModelingSyncService &scene_sync) const;
  [[nodiscard]] bool sync_scene_objects_from_modeling(
      const quader_editor_core::EditorSession &session, SceneDocument &scene,
      AssetRegistry &assets, SceneModelingSyncService &scene_sync,
      bool force = false) const;
  [[nodiscard]] ModelingCreateBoxResult create_scene_object_from_latest(
      const quader_editor_core::EditorSession &session, SceneDocument &scene,
      AssetRegistry &assets, SceneModelingSyncService &scene_sync,
      const std::string &name, const std::string &message) const;
  [[nodiscard]] bool place_created_scene_object(
      SceneDocument &scene, EntityId entity, EntityId parent,
      const TransformComponent &local_transform) const;
  void rollback_created_scene_object(
      SceneDocument &scene, AssetRegistry &assets,
      SceneModelingSyncService &scene_sync, int modeling_object_id,
      EntityId entity, AssetId mesh) const;

private:
  mutable ModelingMeshSyncStats last_sync_stats_;
};

} // namespace quader
