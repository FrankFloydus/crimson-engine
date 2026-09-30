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
struct CurveAsset;
struct MaterialComponent;
struct MeshComponent;
struct ModelingObjectComponent;
struct ModelingObjectSceneIdentity;
class SceneDocument;
struct SceneTransformAssignment;
struct TransformComponent;
struct TextureMappingComponent;

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
                                            EntityId parent,
                                            bool preserve_world = false) const;
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
  [[nodiscard]] bool apply_scene_identity_to_entity(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectSceneIdentity &identity,
      bool include_selection = true,
      bool include_hierarchy_placement = true) const;
  [[nodiscard]] bool apply_scene_identity_parent(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectSceneIdentity &identity) const;
  [[nodiscard]] bool apply_scene_identity_sibling_ordinal(
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
  [[nodiscard]] bool apply_scene_identity_to_duplicate_entity(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectSceneIdentity &identity) const;
  [[nodiscard]] bool apply_scene_transforms(
      SceneDocument &scene,
      std::span<const SceneTransformAssignment> transforms) const;
  [[nodiscard]] bool place_created_scene_object(
      SceneDocument &scene, EntityId entity, EntityId parent,
      const TransformComponent &local_transform) const;
};

} // namespace quader
