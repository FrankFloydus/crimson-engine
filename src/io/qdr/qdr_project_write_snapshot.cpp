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
mesh_asset_write_snapshot(AssetId id, const MeshAsset &mesh) {
  QdrProjectMeshAssetWriteSnapshot snapshot{
      .id = id,
      .name = mesh.name,
      .has_stable_cpu_topology = mesh.has_stable_cpu_topology,
      .has_authored_polygon_document = mesh.has_authored_polygon_document,
      .geometry = mesh.geometry_payload(),
      .authored_polygon_document = mesh.authored_polygon_document,
  };
  return snapshot;
}

QdrProjectEntityWriteSnapshot
entity_write_snapshot(const SceneDocument &scene, EntityId entity) {
  QdrProjectEntityWriteSnapshot snapshot{
      .id = entity,
      .uuid = scene.uuid(entity),
      .editor_object_id = scene.editor_object_id(entity),
      .name = scene.name(entity) ? scene.name(entity)->name : std::string{},
      .kind = scene.kind(entity),
      .parent = scene.parent(entity),
      .layer_id = scene.entity_layer(entity),
  };
  if (const TransformComponent *transform = scene.transform(entity)) {
    snapshot.transform = *transform;
  }
  if (const VisibilityComponent *visibility = scene.visibility(entity)) {
    snapshot.visibility = *visibility;
  }
  if (const LockComponent *lock = scene.lock(entity)) {
    snapshot.lock = *lock;
  }
  if (const ModelingObjectComponent *object = scene.modeling_object(entity)) {
    snapshot.modeling_object = *object;
  }
  if (const MeshComponent *mesh = scene.mesh(entity)) {
    snapshot.mesh = *mesh;
  }
  if (const MaterialComponent *material = scene.material(entity)) {
    snapshot.material = *material;
  }
  if (const LightComponent *light = scene.light(entity)) {
    snapshot.light = *light;
  }
  if (const InstanceDefinitionComponent *definition =
          scene.instance_definition(entity)) {
    snapshot.instance_definition = *definition;
  }
  if (const InstanceOccurrenceComponent *occurrence =
          scene.instance_occurrence(entity)) {
    snapshot.instance_occurrence = *occurrence;
  }
  if (const SceneSettingsComponent *settings = scene.scene_settings(entity)) {
    snapshot.scene_root = scene.is_scene_root(entity);
    snapshot.scene_settings = *settings;
  }
  return snapshot;
}

} // namespace

QdrProjectWriteSnapshot
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
    if (const MeshAsset *mesh = assets.mesh(id)) {
      snapshot.meshes.push_back(mesh_asset_write_snapshot(id, *mesh));
    }
  }

  const std::vector<AssetId> material_ids = assets.material_ids();
  snapshot.materials.reserve(material_ids.size());
  for (const AssetId id : material_ids) {
    if (const MaterialDocument *material = assets.material(id)) {
      snapshot.materials.push_back(QdrProjectMaterialAssetWriteSnapshot{
          .id = id,
          .material = *material,
      });
    }
  }

  const std::vector<AssetId> texture_ids = assets.texture_ids();
  snapshot.textures.reserve(texture_ids.size());
  for (const AssetId id : texture_ids) {
    if (const TextureAsset *texture = assets.texture(id)) {
      snapshot.textures.push_back(QdrProjectTextureAssetWriteSnapshot{
          .id = id,
          .texture = *texture,
      });
    }
  }

  const std::vector<EntityId> entity_ids = scene.entity_tree_order();
  snapshot.entities.reserve(entity_ids.size());
  for (const EntityId entity : entity_ids) {
    snapshot.entities.push_back(entity_write_snapshot(scene, entity));
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

  snapshot.object_records = document.object_records();
  return snapshot;
}

} // namespace quader
