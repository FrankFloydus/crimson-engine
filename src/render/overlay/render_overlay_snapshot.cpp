////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/render_overlay_snapshot.hpp"

#include "assets/asset_registry.hpp"
#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "mesh/polygon/document_model.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <optional>
#include <utility>

namespace quader {
namespace {

bool local_visibility_allows(
    const ViewportLocalVisibilitySnapshot *local_visibility,
    EntityId entity) {
  return local_visibility == nullptr || local_visibility->allows(entity);
}

std::uint64_t local_visibility_revision(
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  return local_visibility != nullptr ? local_visibility->revision() : 0u;
}

const ViewportPersistentOverlayStyleSnapshot &
default_persistent_overlay_style() {
  static const ViewportPersistentOverlayStyleSnapshot kStyle =
      ViewportOverlayStyleResolver::persistent_style_snapshot(
          ViewportSettings::defaults());
  return kStyle;
}

std::uint32_t resolved_scene_wireframe_color(
    const SceneDocument &document, EntityId entity) {
  const SceneLayerRecord *layer = document.layer(document.entity_layer(entity));
  if (layer == nullptr) {
    return kOverlayWireframeXrayColor;
  }
  constexpr std::uint8_t kWireframeAlpha = static_cast<std::uint8_t>(
      (kOverlayWireframeXrayColor >> 24u) & 0xffu);
  return rgba8(normalized_to_byte(layer->wireframe_color.x),
               normalized_to_byte(layer->wireframe_color.y),
               normalized_to_byte(layer->wireframe_color.z),
               kWireframeAlpha);
}

std::span<const MeshVertex> mesh_vertices(const MeshAsset &mesh) {
  return {mesh.geometry().vertices.data(), mesh.geometry().vertices.size()};
}

std::span<const std::uint32_t> mesh_indices(const MeshAsset &mesh) {
  return {mesh.geometry().indices.data(), mesh.geometry().indices.size()};
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
                         const AssetRegistry &assets, EntityId entity,
                         const ViewportLocalVisibilitySnapshot *
                             local_visibility) {
  if (!document.is_effectively_visible(entity) ||
      !local_visibility_allows(local_visibility, entity)) {
    return std::nullopt;
  }
  const MeshComponent *scene_mesh = document.mesh(entity);
  if (scene_mesh == nullptr ||
      scene_mesh->mesh == kInvalidAssetId) {
    return std::nullopt;
  }
  const MeshAsset *asset = assets.mesh(scene_mesh->mesh);
  if (asset == nullptr || asset->geometry().vertices.empty() ||
      asset->geometry().indices.size() < 3u) {
    return std::nullopt;
  }
  const SelectionComponent *selection = document.selection(entity);
  const std::shared_ptr<const MeshGeometryPayload> geometry_payload =
      asset->geometry_payload();
  return RenderOverlayMeshSnapshot{
      .entity = entity,
      .participant_token = entity,
      .mesh_id = scene_mesh->mesh,
      .mesh_revision = asset->revision.value,
      .geometry_payload = geometry_payload,
      .geometry_content_id = asset->geometry_content_id(),
      .authored_topology_payload = asset->authored_polygon_document,
      .world_transform = document.world_transform_matrix(entity),
      .selected = selection != nullptr && selection->selected,
      .wireframe_color = resolved_scene_wireframe_color(document, entity),
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

bool affine_matrices_differ_only_by_translation(const QMat4 &left,
                                                const QMat4 &right) {
  const auto is_affine = [](const QMat4 &matrix) {
    return matrix.values[3] == 0.0f && matrix.values[7] == 0.0f &&
           matrix.values[11] == 0.0f && matrix.values[15] == 1.0f;
  };
  if (!is_affine(left) || !is_affine(right)) {
    return false;
  }
  for (std::size_t index = 0; index < left.values.size(); ++index) {
    if (index >= 12u && index <= 14u) {
      continue;
    }
    if (left.values[index] != right.values[index]) {
      return false;
    }
  }
  return true;
}

QVec3 affine_translation_delta(const QMat4 &current, const QMat4 &previous) {
  return {
      current.values[12] - previous.values[12],
      current.values[13] - previous.values[13],
      current.values[14] - previous.values[14],
  };
}

std::uint64_t instance_overlay_participant_token(EntityId occurrence,
                                                 EntityId member) {
  return RenderSignatureBuilder{}
      .add_value(0x77697265696e7374ULL)
      .add_value(occurrence)
      .add_value(member)
      .value();
}

void append_instance_wireframe_meshes(
    RenderOverlaySceneWireframeSnapshot &snapshot,
    const SceneDocument &document, const AssetRegistry &assets,
    EntityId occurrence,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  const InstanceOccurrenceComponent *instance =
      document.instance_occurrence(occurrence);
  if (instance == nullptr || !document.is_effectively_visible(occurrence) ||
      !local_visibility_allows(local_visibility, occurrence) ||
      !document.is_instance_definition_entity(instance->definition)) {
    return;
  }
  const std::optional<QMat4> definition_inverse = try_scene_matrix_inverse(
      document.world_transform_matrix(instance->definition));
  if (!definition_inverse.has_value()) {
    return;
  }
  const QMat4 occurrence_world = document.world_transform_matrix(occurrence);
  const SelectionComponent *selection = document.selection(occurrence);
  const bool selected = selection != nullptr && selection->selected;
  const std::uint32_t wireframe_color =
      resolved_scene_wireframe_color(document, occurrence);
  for (const EntityId member :
       document.descendants(instance->definition, false)) {
    if (document.containing_instance_definition(member) !=
            instance->definition ||
        !scene_hierarchy_segment_is_visible(document, member,
                                            instance->definition)) {
      continue;
    }
    const MeshComponent *mesh_component = document.mesh(member);
    if (mesh_component == nullptr) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (mesh == nullptr || mesh->geometry().vertices.empty() ||
        mesh->geometry().indices.size() < 3u) {
      continue;
    }
    const QMat4 member_from_definition = scene_transform_multiply(
        *definition_inverse, document.world_transform_matrix(member));
    const std::shared_ptr<const MeshGeometryPayload> geometry_payload =
        mesh->geometry_payload();
    snapshot.meshes.push_back({
        .entity = occurrence,
        .participant_token =
            instance_overlay_participant_token(occurrence, member),
        .mesh_id = mesh_component->mesh,
        .mesh_revision = mesh->revision.value,
        .geometry_payload = geometry_payload,
        .geometry_content_id = mesh->geometry_content_id(),
        .authored_topology_payload = mesh->authored_polygon_document,
        .world_transform = scene_transform_multiply(occurrence_world,
                                                    member_from_definition),
        .selected = selected,
        .wireframe_color = wireframe_color,
        .vertices = mesh_vertices(*mesh),
        .indices = mesh_indices(*mesh),
        .authored_vertices = authored_vertices(*mesh),
        .authored_faces = authored_faces(*mesh),
    });
  }
}

void append_instance_selection_fills(
    RenderOverlaySelectedObjectSnapshot &snapshot,
    const SceneDocument &document, const AssetRegistry &assets,
    EntityId occurrence,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  const InstanceOccurrenceComponent *instance =
      document.instance_occurrence(occurrence);
  if (instance == nullptr || !document.is_effectively_visible(occurrence) ||
      !local_visibility_allows(local_visibility, occurrence) ||
      !document.is_instance_definition_entity(instance->definition)) {
    return;
  }
  const std::optional<QMat4> definition_inverse = try_scene_matrix_inverse(
      document.world_transform_matrix(instance->definition));
  if (!definition_inverse.has_value()) {
    return;
  }
  const QMat4 occurrence_world =
      document.world_transform_matrix(occurrence);
  for (const EntityId member :
       document.descendants(instance->definition, false)) {
    if (document.containing_instance_definition(member) !=
            instance->definition ||
        !scene_hierarchy_segment_is_visible(document, member,
                                            instance->definition)) {
      continue;
    }
    const MeshComponent *mesh_component = document.mesh(member);
    if (mesh_component == nullptr) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (mesh == nullptr || mesh->geometry().vertices.empty() ||
        mesh->geometry().indices.size() < 3u) {
      continue;
    }
    const QMat4 member_from_definition = scene_transform_multiply(
        *definition_inverse, document.world_transform_matrix(member));
    const std::shared_ptr<const MeshGeometryPayload> geometry_payload =
        mesh->geometry_payload();
    const QMat4 world_transform = scene_transform_multiply(
        occurrence_world, member_from_definition);
    snapshot.instance_selection_fills.push_back({
        .occurrence = occurrence,
        .definition = instance->definition,
        .member = member,
        .mesh_id = mesh_component->mesh,
        .mesh_revision = mesh->revision.value,
        .geometry_payload = geometry_payload,
        .geometry_content_id = mesh->geometry_content_id(),
        .world_transform = world_transform,
        .vertices = mesh_vertices(*mesh),
        .indices = mesh_indices(*mesh),
        .color = kOverlayInstanceSelectionFillColor,
        .depth_test = true,
        .depth_write = false,
        .polygon_offset_scale = -1.0f,
        .polygon_offset_constant = -1.0f,
        .blend_order = static_cast<std::uint16_t>(
            viewport_overlay_draw_order(
                ViewportOverlayLayer::SelectedVisibleFace) *
            64),
        .draws_before = ViewportOverlayLayer::SourceWire,
    });
    snapshot.instance_selection_wires.push_back({
        .occurrence = occurrence,
        .definition = instance->definition,
        .member = member,
        .mesh_id = mesh_component->mesh,
        .mesh_revision = mesh->revision.value,
        .geometry_payload = geometry_payload,
        .geometry_content_id = mesh->geometry_content_id(),
        .authored_topology_payload = mesh->authored_polygon_document,
        .world_transform = world_transform,
        .vertices = mesh_vertices(*mesh),
        .indices = mesh_indices(*mesh),
        .authored_vertices = authored_vertices(*mesh),
        .authored_faces = authored_faces(*mesh),
        .color = kOverlaySelectionWireColor,
        .line_width_pixels = kOverlaySelectionWireLineWidthPixels,
        .blend_order = static_cast<std::uint16_t>(
            viewport_overlay_draw_order(ViewportOverlayLayer::SourceWire) *
            64),
        .layer = ViewportOverlayLayer::SourceWire,
    });
  }
}

RenderOverlayBoundsSnapshot translated_bounds(
    RenderOverlayBoundsSnapshot bounds, QVec3 delta) {
  bounds.min += delta;
  bounds.max += delta;
  return bounds;
}

} // namespace

RenderOverlaySelectedObjectSnapshot
build_render_overlay_selected_object_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              bool visible,
                                              RenderSceneBindingKey
                                                  scene_binding,
                                              const ViewportLocalVisibilitySnapshot *
                                                  local_visibility) {
  return build_render_overlay_selected_object_snapshot(
      document, assets, visible, default_persistent_overlay_style(),
      scene_binding, local_visibility);
}

RenderOverlaySelectedObjectSnapshot
build_render_overlay_selected_object_snapshot(
    const SceneDocument &document, const AssetRegistry &assets, bool visible,
    ViewportPersistentOverlayStyleSnapshot style,
    RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  QDR_PROFILE_FUNCTION();
  RenderOverlaySelectedObjectSnapshot snapshot{
      .scene_binding = scene_binding,
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .selection_revision = document.selection_revision(),
      .visibility_revision = document.visibility_revision(),
      .local_visibility_revision =
          local_visibility_revision(local_visibility),
      .transform_revision = document.transform_revision(),
      .mesh_binding_revision = document.mesh_binding_revision(),
      .style = style,
      .clip_planes = scene_camera_clip_planes(document),
      .visible = visible,
  };
  if (!visible) {
    return snapshot;
  }
  const std::vector<EntityId> selected = document.selected_entities();
  snapshot.meshes.reserve(selected.size());
  for (const EntityId entity : selected) {
    if (document.instance_occurrence(entity) != nullptr) {
      append_instance_selection_fills(snapshot, document, assets, entity,
                                      local_visibility);
      continue;
    }
    if (std::optional<RenderOverlayMeshSnapshot> mesh =
            mesh_snapshot_for_entity(document, assets, entity,
                                     local_visibility)) {
      snapshot.meshes.push_back(*mesh);
    }
  }
  return snapshot;
}

RenderOverlaySceneWireframeSnapshot
build_render_overlay_scene_wireframe_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              std::uint8_t layer_mask,
                                              RenderSceneBindingKey
                                                  scene_binding,
                                              const ViewportLocalVisibilitySnapshot *
                                                  local_visibility) {
  return build_render_overlay_scene_wireframe_snapshot(
      document, assets, layer_mask, default_persistent_overlay_style(),
      scene_binding, local_visibility);
}

RenderOverlaySceneWireframeSnapshot
build_render_overlay_scene_wireframe_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    std::uint8_t layer_mask, ViewportPersistentOverlayStyleSnapshot style,
    RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  QDR_PROFILE_FUNCTION();
  RenderOverlaySceneWireframeSnapshot snapshot{
      .scene_binding = scene_binding,
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .local_visibility_revision =
          local_visibility_revision(local_visibility),
      .style = style,
      .clip_planes = scene_camera_clip_planes(document),
      .layer_mask = layer_mask,
  };
  if (layer_mask == 0u) {
    return snapshot;
  }
  const std::vector<EntityId> entities = document.entities();
  snapshot.meshes.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (document.instance_occurrence(entity) != nullptr) {
      append_instance_wireframe_meshes(snapshot, document, assets, entity,
                                       local_visibility);
      continue;
    }
    if (std::optional<RenderOverlayMeshSnapshot> mesh =
            mesh_snapshot_for_entity(document, assets, entity,
                                     local_visibility)) {
      snapshot.meshes.push_back(*mesh);
    }
  }
  return snapshot;
}

namespace {

template <typename SelectedMeshBoundsResolver>
RenderOverlaySelectionSnapshot build_render_overlay_selection_snapshot_impl(
    const SceneDocument &document, const AssetRegistry &assets,
    ViewportOverlayFrame tool_overlays, RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility,
    SelectedMeshBoundsResolver &&selected_mesh_bounds,
    std::vector<EntityId> *selected_mesh_entities = nullptr,
    std::vector<EntityId> *group_entities = nullptr) {
  QDR_PROFILE_FUNCTION();
  RenderOverlaySelectionSnapshot snapshot{
      .scene_binding = scene_binding,
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .selection_revision = document.selection_revision(),
      .visibility_revision = document.visibility_revision(),
      .local_visibility_revision =
          local_visibility_revision(local_visibility),
      .transform_revision = document.transform_revision(),
      .mesh_binding_revision = document.mesh_binding_revision(),
      .render_component_revision = document.render_component_revision(),
      .tool_overlays = std::move(tool_overlays),
  };

  if (selected_mesh_entities != nullptr) {
    selected_mesh_entities->clear();
  }
  if (group_entities != nullptr) {
    group_entities->clear();
  }

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
        !document.is_effectively_visible(entity) ||
        !local_visibility_allows(local_visibility, entity)) {
      continue;
    }
    snapshot.point_light_gizmos.push_back(RenderOverlayLightMarkerSnapshot{
        .entity = entity,
        .type = scene_light->type,
        .world_transform = document.world_transform_matrix(entity),
    });
  }

  for (const EntityId entity : selected) {
    if (!local_visibility_allows(local_visibility, entity)) {
      continue;
    }
    if (document.mesh(entity) != nullptr) {
      if (const std::optional<RenderOverlayBoundsSnapshot> bounds =
              selected_mesh_bounds(entity)) {
        snapshot.selected_mesh_bounds.push_back(*bounds);
        if (selected_mesh_entities != nullptr) {
          selected_mesh_entities->push_back(entity);
        }
      }
    }

    if (document.has_children(entity)) {
      for (const EntityId descendant : document.descendants(entity, false)) {
        const SelectionComponent *selection = document.selection(descendant);
        if (selection != nullptr && selection->selected) {
          continue;
        }
        if (std::optional<RenderOverlayMeshSnapshot> mesh =
                mesh_snapshot_for_entity(document, assets, descendant,
                                         local_visibility)) {
          snapshot.hierarchy_selection_wires.push_back(*mesh);
        }
      }
    }

    if (document.kind(entity) == EntityKind::Empty &&
        document.has_children(entity)) {
      if (const std::optional<SceneBounds> bounds =
              entity_hierarchy_world_bounds(document, assets, entity)) {
        snapshot.group_bounds.push_back(bounds_snapshot(*bounds));
        if (group_entities != nullptr) {
          group_entities->push_back(entity);
        }
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

} // namespace

RenderOverlaySelectionSnapshot build_render_overlay_selection_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    ViewportOverlayFrame tool_overlays,
    RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  return build_render_overlay_selection_snapshot_impl(
      document, assets, std::move(tool_overlays), scene_binding,
      local_visibility,
      [&](EntityId entity) -> std::optional<RenderOverlayBoundsSnapshot> {
        if (const std::optional<SceneBounds> bounds =
                entity_world_bounds(document, assets, entity)) {
          return bounds_snapshot(*bounds);
        }
        return std::nullopt;
      });
}

RenderOverlayPersistentSnapshotUpdate
RenderOverlaySnapshotCache::update_persistent(
    const SceneDocument &document, const AssetRegistry &assets,
    bool selected_object_overlay_visible,
    std::uint8_t scene_wireframe_layer_mask,
    ViewportPersistentOverlayStyleSnapshot style,
    RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  QDR_PROFILE_FUNCTION();
  bind_sources(document, assets, scene_binding);
  const std::uint64_t current_local_visibility_revision =
      local_visibility_revision(local_visibility);

  const bool selected_transform_is_relevant =
      has_selected_object_snapshot_ && selected_object_snapshot_.visible &&
      (selected_object_face_fills_enabled_for_selection_count(
           selected_object_snapshot_.meshes.size()) ||
       !selected_object_snapshot_.instance_selection_fills.empty() ||
       !selected_object_snapshot_.instance_selection_wires.empty());
  const bool selected_transform_matches =
      !selected_transform_is_relevant ||
      selected_object_snapshot_.transform_revision ==
          document.transform_revision();
  const bool selected_snapshot_matches =
      has_selected_object_snapshot_ &&
      selected_object_snapshot_.asset_revision == assets.revision() &&
      selected_object_snapshot_.selection_revision ==
          document.selection_revision() &&
      selected_object_snapshot_.visibility_revision ==
          document.visibility_revision() &&
      selected_object_snapshot_.local_visibility_revision ==
          current_local_visibility_revision &&
      selected_transform_matches &&
      selected_object_snapshot_.mesh_binding_revision ==
          document.mesh_binding_revision() &&
      selected_object_snapshot_.visible == selected_object_overlay_visible &&
      selected_object_snapshot_.style.face_signature ==
          style.face_signature;
  const bool selected_object_changed = !selected_snapshot_matches;
  if (selected_object_changed) {
    selected_object_snapshot_ = build_render_overlay_selected_object_snapshot(
        document, assets, selected_object_overlay_visible, style,
        scene_binding, local_visibility);
    has_selected_object_snapshot_ = true;
    ++stats_.selected_object_build_count;
  } else {
    selected_object_snapshot_.scene_revision = document.revision();
    selected_object_snapshot_.style = style;
    selected_object_snapshot_.clip_planes =
        scene_camera_clip_planes(document);
  }

  const bool wireframe_sources_are_relevant =
      scene_wireframe_layer_mask != 0u;
  const bool wireframe_snapshot_matches =
      has_scene_wireframe_snapshot_ &&
      scene_wireframe_snapshot_.layer_mask == scene_wireframe_layer_mask &&
      scene_wireframe_snapshot_.local_visibility_revision ==
          current_local_visibility_revision &&
      (!wireframe_sources_are_relevant ||
       (scene_wireframe_snapshot_.scene_revision == document.revision() &&
        scene_wireframe_snapshot_.asset_revision == assets.revision() &&
        scene_wireframe_snapshot_.style.wire_signature ==
            style.wire_signature));
  const bool scene_wireframe_changed = !wireframe_snapshot_matches;
  if (scene_wireframe_changed) {
    scene_wireframe_snapshot_ = build_render_overlay_scene_wireframe_snapshot(
        document, assets, scene_wireframe_layer_mask, style, scene_binding,
        local_visibility);
    has_scene_wireframe_snapshot_ = true;
    ++stats_.scene_wireframe_build_count;
  } else if (!wireframe_sources_are_relevant) {
    scene_wireframe_snapshot_.scene_revision = document.revision();
    scene_wireframe_snapshot_.asset_revision = assets.revision();
    scene_wireframe_snapshot_.style = style;
    scene_wireframe_snapshot_.clip_planes =
        scene_camera_clip_planes(document);
  }

  return {
      .selected_object = &selected_object_snapshot_,
      .scene_wireframe = &scene_wireframe_snapshot_,
      .selected_object_changed = selected_object_changed,
      .scene_wireframe_changed = scene_wireframe_changed,
  };
}

const RenderOverlaySelectionSnapshot &
RenderOverlaySnapshotCache::selection_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  QDR_PROFILE_FUNCTION();
  bind_sources(document, assets, scene_binding);
  const std::uint64_t current_local_visibility_revision =
      local_visibility_revision(local_visibility);
  const bool snapshot_matches =
      has_selection_snapshot_ &&
      selection_snapshot_.scene_revision == document.revision() &&
      selection_snapshot_.asset_revision == assets.revision() &&
      selection_snapshot_.local_visibility_revision ==
          current_local_visibility_revision;
  if (!snapshot_matches &&
      (selection_snapshot_.local_visibility_revision !=
           current_local_visibility_revision ||
       !can_refresh_selection_transforms(document, assets) ||
       !refresh_selection_transforms(document, assets))) {
    selection_snapshot_ = build_render_overlay_selection_snapshot_impl(
        document, assets, {}, scene_binding, local_visibility,
        [&](EntityId entity) {
          return selected_mesh_world_bounds(document, assets, entity);
        },
        &selection_selected_mesh_entities_, &selection_group_entities_);
    has_selection_snapshot_ = true;
    ++stats_.selection_build_count;
  }
  return selection_snapshot_;
}

void RenderOverlaySnapshotCache::reset() {
  source_document_ = nullptr;
  source_assets_ = nullptr;
  source_scene_binding_ = {};
  invalidate_snapshots();
  stats_ = {};
}

void RenderOverlaySnapshotCache::bind_sources(
    const SceneDocument &document, const AssetRegistry &assets,
    RenderSceneBindingKey scene_binding) {
  if (source_document_ == &document && source_assets_ == &assets &&
      source_scene_binding_ == scene_binding) {
    return;
  }
  source_document_ = &document;
  source_assets_ = &assets;
  source_scene_binding_ = scene_binding;
  invalidate_snapshots();
}

void RenderOverlaySnapshotCache::invalidate_snapshots() {
  selected_object_snapshot_ = {};
  scene_wireframe_snapshot_ = {};
  selection_snapshot_ = {};
  has_selected_object_snapshot_ = false;
  has_scene_wireframe_snapshot_ = false;
  has_selection_snapshot_ = false;
  selection_local_mesh_bounds_.clear();
  selection_world_mesh_bounds_.clear();
  selection_selected_mesh_entities_.clear();
  selection_group_entities_.clear();
}

std::optional<RenderOverlayBoundsSnapshot>
RenderOverlaySnapshotCache::selected_mesh_world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    EntityId entity) {
  if (!document.is_effectively_visible(entity) ||
      document.transform(entity) == nullptr) {
    return std::nullopt;
  }

  const MeshComponent *mesh_component = document.mesh(entity);
  if (mesh_component == nullptr) {
    return std::nullopt;
  }
  const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
  const std::uint64_t mesh_revision =
      mesh != nullptr ? mesh->revision.value : 0u;
  const QMat4 world_transform = document.world_transform_matrix(entity);
  auto world_cached = selection_world_mesh_bounds_.find(entity);
  if (world_cached != selection_world_mesh_bounds_.end() &&
      world_cached->second.mesh_id == mesh_component->mesh &&
      world_cached->second.mesh_revision == mesh_revision &&
      affine_matrices_differ_only_by_translation(
          world_transform, world_cached->second.world_transform)) {
    const RenderOverlayBoundsSnapshot translated = translated_bounds(
        world_cached->second.bounds,
        affine_translation_delta(world_transform,
                                 world_cached->second.world_transform));
    ++stats_.selection_world_bounds_translation_reuse_count;
    return translated;
  }

  std::optional<SceneBounds> local_bounds;
  if (mesh != nullptr) {
    auto cached = selection_local_mesh_bounds_.find(mesh_component->mesh);
    if (cached == selection_local_mesh_bounds_.end() ||
        cached->second.mesh_revision != mesh_revision) {
      const std::optional<SceneBounds> built_bounds = local_mesh_bounds(*mesh);
      CachedLocalMeshBounds replacement{
          .mesh_revision = mesh_revision,
          .has_bounds = built_bounds.has_value(),
          .bounds = built_bounds ? bounds_snapshot(*built_bounds)
                                 : RenderOverlayBoundsSnapshot{},
      };
      cached = selection_local_mesh_bounds_
                   .insert_or_assign(mesh_component->mesh,
                                     std::move(replacement))
                   .first;
      ++stats_.selection_local_bounds_build_count;
    } else {
      ++stats_.selection_local_bounds_reuse_count;
    }
    if (cached->second.has_bounds) {
      local_bounds = SceneBounds{
          .min = cached->second.bounds.min,
          .max = cached->second.bounds.max,
      };
    }
  }

  const std::optional<SceneBounds> world_bounds =
      world_bounds_from_local_mesh_bounds(world_transform, local_bounds);
  if (!world_bounds) {
    return std::nullopt;
  }
  const RenderOverlayBoundsSnapshot snapshot = bounds_snapshot(*world_bounds);
  selection_world_mesh_bounds_.insert_or_assign(
      entity,
      CachedWorldMeshBounds{
          .mesh_id = mesh_component->mesh,
          .mesh_revision = mesh_revision,
          .has_mesh_asset = mesh != nullptr,
          .world_transform = world_transform,
          .bounds = snapshot,
      });
  ++stats_.selection_world_bounds_build_count;
  return snapshot;
}

std::optional<RenderOverlayBoundsSnapshot>
RenderOverlaySnapshotCache::refresh_selected_mesh_world_bounds(
    const SceneDocument &document, EntityId entity) {
  auto world_cached = selection_world_mesh_bounds_.find(entity);
  if (world_cached == selection_world_mesh_bounds_.end()) {
    return std::nullopt;
  }

  // This helper is reachable only after the transform-only revision gate has
  // proved entity structure, visibility, mesh bindings and render components
  // unchanged. Avoid repeating two scene-map probes for every selected mesh.
  const QMat4 world_transform = document.world_transform_matrix(entity);
  if (affine_matrices_differ_only_by_translation(
          world_transform, world_cached->second.world_transform)) {
    ++stats_.selection_world_bounds_translation_reuse_count;
    return translated_bounds(
        world_cached->second.bounds,
        affine_translation_delta(world_transform,
                                 world_cached->second.world_transform));
  }

  std::optional<SceneBounds> local_bounds;
  if (world_cached->second.has_mesh_asset) {
    const auto local_cached =
        selection_local_mesh_bounds_.find(world_cached->second.mesh_id);
    if (local_cached == selection_local_mesh_bounds_.end() ||
        local_cached->second.mesh_revision !=
            world_cached->second.mesh_revision) {
      return std::nullopt;
    }
    ++stats_.selection_local_bounds_reuse_count;
    if (local_cached->second.has_bounds) {
      local_bounds = SceneBounds{
          .min = local_cached->second.bounds.min,
          .max = local_cached->second.bounds.max,
      };
    }
  }

  const std::optional<SceneBounds> world_bounds =
      world_bounds_from_local_mesh_bounds(world_transform, local_bounds);
  if (!world_bounds) {
    return std::nullopt;
  }
  const RenderOverlayBoundsSnapshot snapshot = bounds_snapshot(*world_bounds);
  world_cached->second.world_transform = world_transform;
  world_cached->second.bounds = snapshot;
  ++stats_.selection_world_bounds_build_count;
  return snapshot;
}

bool RenderOverlaySnapshotCache::can_refresh_selection_transforms(
    const SceneDocument &document, const AssetRegistry &assets) const {
  if (!has_selection_snapshot_ ||
      selection_snapshot_.asset_revision != assets.revision() ||
      selection_snapshot_.selection_revision !=
          document.selection_revision() ||
      selection_snapshot_.visibility_revision !=
          document.visibility_revision() ||
      selection_snapshot_.mesh_binding_revision !=
          document.mesh_binding_revision() ||
      selection_snapshot_.render_component_revision !=
          document.render_component_revision() ||
      document.revision() <= selection_snapshot_.scene_revision ||
      document.transform_revision() <=
          selection_snapshot_.transform_revision) {
    return false;
  }

  const std::uint64_t scene_delta =
      document.revision() - selection_snapshot_.scene_revision;
  const std::uint64_t transform_delta =
      document.transform_revision() - selection_snapshot_.transform_revision;
  return scene_delta == transform_delta;
}

bool RenderOverlaySnapshotCache::refresh_selection_transforms(
    const SceneDocument &document, const AssetRegistry &assets) {
  if (selection_snapshot_.selected_mesh_bounds.size() !=
          selection_selected_mesh_entities_.size() ||
      selection_snapshot_.group_bounds.size() !=
          selection_group_entities_.size()) {
    return false;
  }

  std::vector<RenderOverlayBoundsSnapshot> selected_mesh_bounds;
  selected_mesh_bounds.reserve(selection_selected_mesh_entities_.size());
  for (const EntityId entity : selection_selected_mesh_entities_) {
    const std::optional<RenderOverlayBoundsSnapshot> bounds =
        refresh_selected_mesh_world_bounds(document, entity);
    if (!bounds) {
      return false;
    }
    selected_mesh_bounds.push_back(*bounds);
  }

  std::vector<RenderOverlayBoundsSnapshot> group_bounds;
  group_bounds.reserve(selection_group_entities_.size());
  for (const EntityId entity : selection_group_entities_) {
    const std::optional<SceneBounds> bounds =
        entity_hierarchy_world_bounds(document, assets, entity);
    if (!bounds) {
      return false;
    }
    group_bounds.push_back(bounds_snapshot(*bounds));
  }

  selection_snapshot_.selected_mesh_bounds = std::move(selected_mesh_bounds);
  selection_snapshot_.group_bounds = std::move(group_bounds);
  for (RenderOverlayMeshSnapshot &mesh :
       selection_snapshot_.hierarchy_selection_wires) {
    mesh.world_transform = document.world_transform_matrix(mesh.entity);
  }
  for (RenderOverlayLightMarkerSnapshot &light :
       selection_snapshot_.selected_lights) {
    light.world_transform = document.world_transform_matrix(light.entity);
  }
  for (RenderOverlayLightMarkerSnapshot &light :
       selection_snapshot_.point_light_gizmos) {
    light.world_transform = document.world_transform_matrix(light.entity);
  }

  selection_snapshot_.scene_revision = document.revision();
  selection_snapshot_.asset_revision = assets.revision();
  selection_snapshot_.selection_revision = document.selection_revision();
  selection_snapshot_.visibility_revision = document.visibility_revision();
  selection_snapshot_.transform_revision = document.transform_revision();
  selection_snapshot_.mesh_binding_revision =
      document.mesh_binding_revision();
  selection_snapshot_.render_component_revision =
      document.render_component_revision();
  ++stats_.selection_transform_refresh_count;
  return true;
}

} // namespace quader
