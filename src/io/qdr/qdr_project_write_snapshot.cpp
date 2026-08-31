////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_project_write_snapshot.hpp"

#include "assets/asset_registry.hpp"
#include "scene/scene_document.hpp"

#include <utility>

namespace quader {
namespace {

QdrProjectMeshAssetWriteSnapshot
mesh_asset_write_snapshot(const MeshAssetWriteSource &source) {
  QdrProjectMeshAssetWriteSnapshot snapshot{
      .id = source.descriptor.id,
      .name = source.descriptor.name,
      .has_stable_cpu_topology = source.descriptor.has_stable_cpu_topology,
      .has_authored_polygon_document =
          source.descriptor.has_authored_polygon_document,
      .geometry = source.geometry,
      .authored_polygon_document = source.authored_polygon_document,
      .encoded_geometry = source.encoded_geometry,
  };
  return snapshot;
}

} // namespace

QdrProjectWriteSnapshotResult
qdr_project_write_snapshot_from_document(const SceneContentDocument &document) {
  const SceneDocument &scene = document.scene();
  const AssetRegistry &assets = document.assets();

  QdrProjectWriteSnapshot snapshot{
      .project_id = document.id(),
      .project_name = document.name(),
  };

  const std::vector<AssetId> mesh_ids = assets.mesh_ids();
  snapshot.meshes.reserve(mesh_ids.size());
  for (const AssetId id : mesh_ids) {
    MeshAssetWriteSourceResult source = assets.mesh_write_source(id);
    if (!source.ok) {
      return QdrProjectWriteSnapshotResult::failure(
          ProjectIoErrorCode::IoFailure,
          "Mesh asset " + std::to_string(id) +
              " could not be captured for save: " + source.message);
    }
    snapshot.meshes.push_back(mesh_asset_write_snapshot(source.source));
  }

  const std::vector<AssetId> curve_ids = assets.curve_ids();
  snapshot.curves.reserve(curve_ids.size());
  for (const AssetId id : curve_ids) {
    const CurveAsset *curve = assets.curve(id);
    if (curve == nullptr || !assets.validate_curve(*curve).ok) {
      return QdrProjectWriteSnapshotResult::failure(
          ProjectIoErrorCode::IoFailure,
          "Curve asset " + std::to_string(id) +
              " could not be captured for save.");
    }
    snapshot.curves.push_back(QdrProjectCurveAssetWriteSnapshot{
        .id = id,
        .name = curve->name,
        .document = curve->document,
    });
  }

  const std::vector<AssetId> material_ids = assets.material_ids();
  snapshot.materials.reserve(material_ids.size());
  for (const AssetId id : material_ids) {
    const MaterialDocument *material = assets.material(id);
    if (material == nullptr) {
      return QdrProjectWriteSnapshotResult::failure(
          ProjectIoErrorCode::IoFailure,
          "Material asset " + std::to_string(id) +
              " could not be captured for save.");
    }
    snapshot.materials.push_back(QdrProjectMaterialAssetWriteSnapshot{
        .id = id,
        .material = *material,
    });
  }

  const std::vector<AssetId> texture_ids = assets.texture_ids();
  snapshot.textures.reserve(texture_ids.size());
  for (const AssetId id : texture_ids) {
    const TextureAsset *texture = assets.texture(id);
    if (texture == nullptr) {
      return QdrProjectWriteSnapshotResult::failure(
          ProjectIoErrorCode::IoFailure,
          "Texture asset " + std::to_string(id) +
              " could not be captured for save.");
    }
    snapshot.textures.push_back(QdrProjectTextureAssetWriteSnapshot{
        .id = id,
        .texture = *texture,
    });
  }

  const std::vector<EntityId> entity_ids = scene.entity_tree_order();
  snapshot.entities.reserve(entity_ids.size());
  for (const EntityId entity : entity_ids) {
    if (const CurveComponent *curve = scene.curve(entity);
        (curve != nullptr && !assets.has_curve(curve->curve)) ||
        (scene.kind(entity) == EntityKind::Curve && curve == nullptr)) {
      return QdrProjectWriteSnapshotResult::failure(
          ProjectIoErrorCode::IoFailure,
          "Curve entity " + std::to_string(entity) +
              " has a missing curve asset binding.");
    }
    std::optional<SceneEntityState> entity_state =
        capture_scene_entity_state(scene, entity);
    if (!entity_state.has_value()) {
      return QdrProjectWriteSnapshotResult::failure(
          ProjectIoErrorCode::IoFailure,
          "Scene entity " + std::to_string(entity) +
              " could not be captured for save.");
    }
    snapshot.entities.push_back(std::move(*entity_state));
  }

  const std::vector<SceneLayerRecord> layers = scene.layers();
  snapshot.layers.reserve(layers.size());
  for (const SceneLayerRecord &layer : layers) {
    snapshot.layers.push_back(QdrProjectSceneLayerWriteSnapshot{
        .id = layer.id,
        .name = layer.name,
        .visible = layer.visible,
        .wireframe_color = layer.wireframe_color,
        .locked = layer.locked,
    });
  }

  return QdrProjectWriteSnapshotResult::success(std::move(snapshot));
}

} // namespace quader
