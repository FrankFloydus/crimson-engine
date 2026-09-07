////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/render_overlay_snapshot.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/render_signature_builder.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"

#include <algorithm>
#include <unordered_set>

namespace quader {
namespace {

bool collision_colors_match(ViewportSettingColor left,
                            ViewportSettingColor right) {
  return left.r == right.r && left.g == right.g && left.b == right.b &&
         left.a == right.a;
}

/** All membership/geometry inputs remain fixed during transform refresh. */
constexpr RenderSourceDependencyMask kCollisionTransformStableDependencies =
    RenderSourceDependencyMask::from({
        RenderSourceDependency::SceneBinding,
        RenderSourceDependency::MeshMaterialUsageRevision,
        RenderSourceDependency::VisibilityRevision,
        RenderSourceDependency::LocalVisibilityRevision,
        RenderSourceDependency::MeshBindingRevision,
        RenderSourceDependency::RenderComponentRevision,
        RenderSourceDependency::HierarchyPresentationRevision,
        RenderSourceDependency::LayerRevision,
    });

} // namespace

RenderOverlayCollisionSnapshot build_render_overlay_collision_snapshot(
    const SceneDocument &document, const AssetRegistry &assets, bool visible,
    ViewportSettingColor color, RenderSceneBindingKey scene_binding,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  QDR_PROFILE_FUNCTION();
  RenderOverlayCollisionSnapshot snapshot;
  static_cast<RenderSourceStamp &>(snapshot) = capture_render_source_stamp(
      document, assets,
      {.scene_binding = scene_binding, .local_visibility = local_visibility});
  snapshot.visible = visible;
  snapshot.color = color;
  if (!visible) {
    return snapshot;
  }
  const auto projection = document.mesh_occurrence_projection();
  for (const SceneMeshOccurrenceProjectionRecord &placement :
       projection->records()) {
    if (!placement.effectively_visible ||
        (local_visibility != nullptr &&
         !local_visibility->allows(placement.display_entity))) {
      continue;
    }
    const MeshComponent *component = document.mesh(placement.geometry_entity);
    if (component == nullptr || !component->generate_collisions ||
        !component->display_collision || !mesh_collision_supported(*component)) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(component->mesh);
    if (mesh == nullptr) {
      continue;
    }
    const auto payload = mesh_collision_geometry_payload(*mesh->geometry_payload());
    if (payload == nullptr || !payload->error_message.empty() ||
        payload->faces.empty()) {
      continue;
    }
    snapshot.meshes.push_back({
        .entity = placement.display_entity,
        .occurrence = placement.formal_instance()
                          ? placement.display_entity : kInvalidEntityId,
        .definition = placement.definition_entity,
        .member = placement.formal_instance()
                      ? placement.geometry_entity : kInvalidEntityId,
        .mesh_id = component->mesh,
        .collision_payload = payload,
        .world_transform = placement.world_from_geometry,
        .selected = document.selection(placement.display_entity) != nullptr &&
                    document.selection(placement.display_entity)->selected,
    });
  }
  return snapshot;
}

bool RenderOverlaySnapshotCache::update_collision_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    const RenderSourceStamp &current,
    const ViewportLocalVisibilitySnapshot *local_visibility, bool visible,
    ViewportSettingColor color) {
  const bool visibility_changed = !has_collision_snapshot_ ||
                                  collision_snapshot_.visible != visible;
  const bool color_changed = !has_collision_snapshot_ ||
      !collision_colors_match(collision_snapshot_.color, color);
  const bool sources_changed = !has_collision_snapshot_ ||
      !render_source_dependencies_match(collision_snapshot_, current,
                                         kRenderOverlayCollisionSourceDependencies);
  bool geometry_changed = false;
  const bool selection_changed = visible && has_collision_snapshot_ &&
      collision_snapshot_.selection_revision != current.selection_revision;
  if (visibility_changed || (visible && sources_changed)) {
    // Update placements directly against the authoritative occurrence index;
    // immutable collision payloads stay pinned without consulting mesh assets.
    bool transforms_refreshed = !visibility_changed && visible &&
        render_source_dependencies_match(collision_snapshot_, current,
                                           kCollisionTransformStableDependencies);
    if (transforms_refreshed) {
      const auto projection = document.mesh_occurrence_projection();
      for (RenderOverlayCollisionMeshSnapshot &mesh : collision_snapshot_.meshes) {
        const auto *placement = projection->find({
            .display_entity = mesh.entity,
            .definition_entity = mesh.definition,
            .geometry_entity = mesh.member == kInvalidEntityId ? mesh.entity
                                                               : mesh.member,
        });
        if (placement == nullptr) {
          transforms_refreshed = false;
          break;
        }
        mesh.world_transform = placement->world_from_geometry;
      }
    }
    if (transforms_refreshed) {
      ++stats_.collision_transform_refresh_count;
    } else {
      collision_snapshot_ = build_render_overlay_collision_snapshot(
          document, assets, visible, color, current.scene_binding,
          local_visibility);
      ++stats_.collision_build_count;
    }
    geometry_changed = true;
  }
  if (selection_changed) {
    for (RenderOverlayCollisionMeshSnapshot &mesh : collision_snapshot_.meshes) {
      const SelectionComponent *selection = document.selection(mesh.entity);
      mesh.selected = selection != nullptr && selection->selected;
    }
  }
  static_cast<RenderSourceStamp &>(collision_snapshot_) = current;
  collision_snapshot_.visible = visible;
  collision_snapshot_.color = color;
  has_collision_snapshot_ = true;
  return geometry_changed || color_changed || selection_changed;
}

RenderOverlayCollisionSnapshot filter_render_overlay_collision_snapshot(
    const RenderOverlayCollisionSnapshot &snapshot,
    const RenderToolPreviewSuppressionSnapshot &suppression) {
  RenderOverlayCollisionSnapshot filtered = snapshot;
  if (suppression.empty()) {
    return filtered;
  }
  const std::unordered_set<EntityId> entities(suppression.entities.begin(),
                                             suppression.entities.end());
  std::erase_if(filtered.meshes, [&](const auto &mesh) {
    const auto contains = [&](EntityId entity) {
      return entity != kInvalidEntityId && entities.contains(entity);
    };
    return contains(mesh.entity) || contains(mesh.occurrence) ||
           contains(mesh.definition) || contains(mesh.member) ||
           std::ranges::any_of(suppression.occurrences, [&](const auto &entry) {
             return entry.occurrence == mesh.occurrence &&
                    (entry.source_entity == mesh.definition ||
                     entry.source_entity == mesh.member);
           });
  });
  filtered.local_visibility_revision =
      RenderSignatureBuilder(filtered.local_visibility_revision)
          .add_value(suppression.signature).value();
  return filtered;
}

} // namespace quader
