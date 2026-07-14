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
struct MaterialComponent;
struct ModelingObjectSceneIdentity;
class SceneDocument;
struct SceneTransformAssignment;
class SceneModelingSyncService;

/**
 * Owns the modeling runtime to SceneDocument / AssetRegistry synchronization boundary.
 *
 * Modeling runtime code that needs to create, destroy, or update scene mesh
 * entities and projected mesh assets should route through this service instead
 * of mutating SceneDocument or AssetRegistry directly.
 */
class ModelingMeshSyncService {
public:
  [[nodiscard]] static bool should_force_refresh(bool preview_active,
                                                 bool preview_finished);

  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(const quader_editor_core::PolygonObject &object) const;
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
  void apply_scene_identity_to_duplicate_entity(
      SceneDocument &scene, EntityId entity,
      const ModelingObjectSceneIdentity &identity) const;
  void apply_scene_transforms(
      SceneDocument &scene,
      std::span<const SceneTransformAssignment> transforms) const;
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
};

} // namespace quader
