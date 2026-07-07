////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/render_overlay_snapshot.hpp"

#include "assets/asset_registry.hpp"
#include "mesh/polygon/document_model.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <optional>
#include <utility>

namespace quader {
namespace {

std::span<const MeshVertex> mesh_vertices(const MeshAsset &mesh) {
  return {mesh.vertices.data(), mesh.vertices.size()};
}

std::span<const std::uint32_t> mesh_indices(const MeshAsset &mesh) {
  return {mesh.indices.data(), mesh.indices.size()};
}

std::span<const quader_poly::Vertex>
authored_vertices(const MeshAsset &mesh) {
  if (!mesh.has_authored_polygon_document ||
      !mesh.authored_polygon_document) {
    return {};
  }
  return {mesh.authored_polygon_document->vertices.data(),
          mesh.authored_polygon_document->vertices.size()};
}

std::span<const quader_poly::Face> authored_faces(const MeshAsset &mesh) {
  if (!mesh.has_authored_polygon_document ||
      !mesh.authored_polygon_document) {
    return {};
  }
  return {mesh.authored_polygon_document->faces.data(),
          mesh.authored_polygon_document->faces.size()};
}

std::optional<RenderOverlayMeshSnapshot>
mesh_snapshot_for_entity(const SceneDocument &document,
                         const AssetRegistry &assets, EntityId entity) {
  if (!document.is_effectively_visible(entity)) {
    return std::nullopt;
  }
  const MeshComponent *scene_mesh = document.mesh(entity);
  if (scene_mesh == nullptr ||
      scene_mesh->mesh == kInvalidAssetId) {
    return std::nullopt;
  }
  const MeshAsset *asset = assets.mesh(scene_mesh->mesh);
  if (asset == nullptr || asset->vertices.empty() ||
      asset->indices.size() < 3u) {
    return std::nullopt;
  }
  const SelectionComponent *selection = document.selection(entity);
  return RenderOverlayMeshSnapshot{
      .entity = entity,
      .mesh_id = scene_mesh->mesh,
      .mesh_revision = asset->revision.value,
      .world_transform = document.world_transform_matrix(entity),
      .selected = selection != nullptr && selection->selected,
      .vertices = mesh_vertices(*asset),
      .indices = mesh_indices(*asset),
      .authored_vertices = authored_vertices(*asset),
      .authored_faces = authored_faces(*asset),
  };
}

RenderOverlayBoundsSnapshot bounds_snapshot(const SceneBounds &bounds) {
  return {.min = bounds.min, .max = bounds.max};
}

ViewportCameraClipPlanes scene_camera_clip_planes(
    const SceneDocument &document) {
  if (const EntityId root = document.scene_root(); root != kInvalidEntityId) {
    if (const SceneSettingsComponent *settings =
            document.scene_settings(root)) {
      return sanitize_viewport_camera_clip_planes(
          settings->camera.near_plane, settings->camera.far_plane);
    }
  }
  const SceneCameraSettings defaults;
  return sanitize_viewport_camera_clip_planes(defaults.near_plane,
                                             defaults.far_plane);
}

} // namespace

RenderOverlaySelectedObjectSnapshot
build_render_overlay_selected_object_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              bool visible) {
  RenderOverlaySelectedObjectSnapshot snapshot{
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .selection_revision = document.selection_revision(),
      .visibility_revision = document.visibility_revision(),
      .transform_revision = document.transform_revision(),
      .mesh_binding_revision = document.mesh_binding_revision(),
      .clip_planes = scene_camera_clip_planes(document),
      .visible = visible,
  };
  if (!visible) {
    return snapshot;
  }
  const std::vector<EntityId> selected = document.selected_entities();
  snapshot.meshes.reserve(selected.size());
  for (const EntityId entity : selected) {
    if (std::optional<RenderOverlayMeshSnapshot> mesh =
            mesh_snapshot_for_entity(document, assets, entity)) {
      snapshot.meshes.push_back(*mesh);
    }
  }
  return snapshot;
}

RenderOverlaySceneWireframeSnapshot
build_render_overlay_scene_wireframe_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              std::uint8_t layer_mask) {
  RenderOverlaySceneWireframeSnapshot snapshot{
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .clip_planes = scene_camera_clip_planes(document),
      .layer_mask = layer_mask,
  };
  if (layer_mask == 0u) {
    return snapshot;
  }
  const std::vector<EntityId> entities = document.entities();
  snapshot.meshes.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (std::optional<RenderOverlayMeshSnapshot> mesh =
            mesh_snapshot_for_entity(document, assets, entity)) {
      snapshot.meshes.push_back(*mesh);
    }
  }
  return snapshot;
}

RenderOverlaySelectionSnapshot build_render_overlay_selection_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    ViewportOverlayFrame tool_overlays) {
  RenderOverlaySelectionSnapshot snapshot{
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .tool_overlays = std::move(tool_overlays),
  };

  const std::vector<EntityId> selected = document.selected_entities();
  const std::vector<EntityId> entities = document.entities();
  snapshot.hierarchy_selection_wires.reserve(selected.size());
  snapshot.group_bounds.reserve(selected.size());
  snapshot.selected_mesh_bounds.reserve(selected.size());
  snapshot.selected_lights.reserve(selected.size());
  snapshot.point_light_gizmos.reserve(entities.size());
  for (const EntityId entity : entities) {
    const LightComponent *scene_light = document.light(entity);
    if (scene_light == nullptr || scene_light->type != LightType::Point ||
        !document.is_effectively_visible(entity)) {
      continue;
    }
    snapshot.point_light_gizmos.push_back(RenderOverlayLightMarkerSnapshot{
        .entity = entity,
        .type = scene_light->type,
        .world_transform = document.world_transform_matrix(entity),
    });
  }

  for (const EntityId entity : selected) {
    if (document.mesh(entity) != nullptr) {
      if (const std::optional<SceneBounds> bounds =
              entity_world_bounds(document, assets, entity)) {
        snapshot.selected_mesh_bounds.push_back(bounds_snapshot(*bounds));
      }
    }

    if (document.has_children(entity)) {
      for (const EntityId descendant : document.descendants(entity, false)) {
        const SelectionComponent *selection = document.selection(descendant);
        if (selection != nullptr && selection->selected) {
          continue;
        }
        if (std::optional<RenderOverlayMeshSnapshot> mesh =
                mesh_snapshot_for_entity(document, assets, descendant)) {
          snapshot.hierarchy_selection_wires.push_back(*mesh);
        }
      }
    }

    if (document.kind(entity) == EntityKind::Empty &&
        document.has_children(entity)) {
      if (const std::optional<SceneBounds> bounds =
              entity_hierarchy_world_bounds(document, assets, entity)) {
        snapshot.group_bounds.push_back(bounds_snapshot(*bounds));
      }
    }

    const LightComponent *scene_light = document.light(entity);
    if (scene_light == nullptr || scene_light->type == LightType::Point ||
        !document.is_effectively_visible(entity)) {
      continue;
    }
    snapshot.selected_lights.push_back(RenderOverlayLightMarkerSnapshot{
        .entity = entity,
        .type = scene_light->type,
        .world_transform = document.world_transform_matrix(entity),
    });
  }
  return snapshot;
}

} // namespace quader
