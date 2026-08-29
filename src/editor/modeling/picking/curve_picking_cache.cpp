////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/picking/curve_picking_cache.hpp"

#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "editor/interactions/viewport_segment_projection.hpp"
#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <limits>
#include <utility>

namespace quader {

std::span<const ViewportSceneCurvePickCandidate> ViewportCurvePickCache::candidates(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot &local_visibility) {
  return candidates(document, assets, local_visibility.revision(),
                    [&local_visibility](EntityId entity) {
                      return local_visibility.allows(entity);
                    });
}

std::span<const ViewportSceneCurvePickCandidate> ViewportCurvePickCache::candidates(
    const SceneDocument &document, const AssetRegistry &assets,
    std::uint64_t visibility_revision,
    const std::function<bool(EntityId)> &entity_visible,
    bool instance_edit_active) {
  if (valid_ && document_ == &document && assets_ == &assets &&
      scene_revision_ == document.revision() &&
      asset_revision_ == assets.revision() &&
      visibility_revision_ == visibility_revision &&
      instance_edit_active_ == instance_edit_active) {
    return candidates_;
  }
  QDR_PROFILE_SCOPE("Viewport rebuild curve pick candidates");
  document_ = &document;
  assets_ = &assets;
  scene_revision_ = document.revision();
  asset_revision_ = assets.revision();
  visibility_revision_ = visibility_revision;
  instance_edit_active_ = instance_edit_active;
  valid_ = true;
  ++build_count_;
  candidates_.clear();
  if (instance_edit_active || assets.curve_count() == 0u) {
    return candidates_;
  }
  candidates_.reserve(assets.curve_count());
  const auto hierarchy = document.hierarchy_projection_snapshot();
  for (const auto &entity : hierarchy.records()) {
    const CurveComponent *binding = document.curve(entity.entity);
    if (binding == nullptr || !entity.effectively_visible ||
        entity.effectively_locked ||
        entity.containing_instance_definition != kInvalidEntityId ||
        (entity_visible && !entity_visible(entity.entity)) ||
        !scene_hierarchy_segment_is_selectable_ignoring_visibility(
            document, entity.entity)) {
      continue;
    }
    const CurveAsset *curve = assets.curve(binding->curve);
    if (curve == nullptr || curve->document == nullptr) {
      continue;
    }
    const auto samples = curve_sample_payload(*curve);
    candidates_.push_back({
        .entity = entity.entity,
        .world_transform = entity.world_transform,
        .document = curve->document,
        .sample_payload = samples,
    });
  }
  return candidates_;
}

std::vector<EntityId> ViewportCurvePickCache::box_pick_entities(
    const SceneDocument &document, const AssetRegistry &assets,
    const ModelingBoxSelectRequest &request, std::uint64_t visibility_revision,
    const std::function<bool(EntityId)> &entity_visible,
    bool instance_edit_active) {
  const auto source = candidates(document, assets, visibility_revision,
                                 entity_visible, instance_edit_active);
  if (!rectangle_valid_ || rectangle_source_revision_ != build_count_ ||
      !modeling_viewport_camera_matches(rectangle_camera_, request.camera)) {
    QDR_PROFILE_SCOPE("Viewport rebuild curve rectangle projection");
    rectangle_request_.objects.clear();
    rectangle_entities_.clear();
    rectangle_request_.objects.reserve(source.size());
    rectangle_entities_.reserve(source.size());
    SceneCameraSettings settings;
    if (const auto *scene_settings = document.scene_settings(document.scene_root())) {
      settings = scene_settings->camera;
    }
    const auto clips = sanitize_viewport_camera_clip_planes(
        settings.near_plane, settings.far_plane);
    const auto camera = modeling_runtime_camera(request.camera);
    for (const auto &candidate : source) {
      if (rectangle_entities_.size() >=
          static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        break;
      }
      quader_editor_core::RectangleProjectedObject projected;
      projected.object_id = static_cast<int>(rectangle_entities_.size());
      if (candidate.sample_payload == nullptr ||
          candidate.sample_payload->samples.size() < 2u) {
        continue;
      }
      const auto &samples = candidate.sample_payload->samples;
      projected.screen_points.reserve(samples.size() * 2u);
      projected.screen_segments.reserve(samples.size() - 1u);
      for (std::size_t index = 1; index < samples.size(); ++index) {
        const auto &a = samples[index - 1u].position;
        const auto &b = samples[index].position;
        const QVec3 start = transform_point(candidate.world_transform, {a.x, a.y, a.z});
        const QVec3 end = transform_point(candidate.world_transform, {b.x, b.y, b.z});
        if (start.x == end.x && start.y == end.y && start.z == end.z) {
          continue;
        }
        // Keep offscreen endpoints: clipping them to the viewport would make a
        // partly visible curve incorrectly satisfy whole-object containment.
        const auto segment = project_visible_viewport_segment(
            camera, start, end, clips.near_plane, clips.far_plane, false);
        if (!segment.has_value()) {
          continue;
        }
        quader_editor_core::include_projected_object_point(projected, segment->start);
        quader_editor_core::include_projected_object_point(projected, segment->end);
        projected.screen_segments.push_back({segment->start, segment->end});
      }
      if (projected.screen_segments.empty()) {
        continue;
      }
      rectangle_entities_.push_back(candidate.entity);
      rectangle_request_.objects.push_back(std::move(projected));
    }
    rectangle_source_revision_ = build_count_;
    rectangle_camera_ = request.camera;
    rectangle_valid_ = true;
    ++rectangle_build_count_;
  }
  rectangle_request_.rect =
      quader_editor_core::normalized_screen_rect(request.start, request.end);
  rectangle_request_.match =
      resolve_box_selection_rule(request.rule, request.start, request.end) ==
              ModelingBoxSelectionRule::Contain
          ? quader_editor_core::RectangleSelectionMatch::Contain
          : quader_editor_core::RectangleSelectionMatch::Crossing;
  const auto hits = quader_editor_core::rectangle_hits_from_projected_candidates(
      rectangle_request_);
  std::vector<EntityId> entities;
  entities.reserve(hits.object_ids.size());
  for (const int id : hits.object_ids) {
    entities.push_back(rectangle_entities_[static_cast<std::size_t>(id)]);
  }
  return entities;
}

void ViewportCurvePickCache::reset() {
  document_ = nullptr;
  assets_ = nullptr;
  candidates_.clear();
  build_count_ = 0;
  valid_ = false;
  rectangle_request_.objects.clear();
  rectangle_entities_.clear();
  rectangle_build_count_ = 0;
  rectangle_valid_ = false;
}

} // namespace quader
