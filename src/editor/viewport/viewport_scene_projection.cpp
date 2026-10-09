////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_scene_projection.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_local_visibility.hpp"
#include "viewport/viewport_pixel_scale.hpp"

namespace quader {

std::vector<ViewportSceneBillboardPickCandidate>
ViewportSceneProjection::point_light_pick_candidates(
    const SceneDocument &document,
    const ViewportLocalVisibilitySnapshot &local_visibility,
    float viewport_pixel_scale) {
  const float resolved_pixel_scale =
      viewport_sanitized_pixel_scale(viewport_pixel_scale);
  std::vector<ViewportSceneBillboardPickCandidate> candidates;
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> hierarchy =
      document.hierarchy_projection();
  for (const EntityId entity : document.entities()) {
    const LightComponent *light = document.light(entity);
    const SceneHierarchyProjectionRecord *state = hierarchy->find(entity);
    if (light == nullptr || light->type != LightType::Point ||
        state == nullptr || !state->effectively_visible ||
        !local_visibility.allows(
            static_cast<ViewportLocalVisibilityEntityId>(entity))) {
      continue;
    }
    const SelectionComponent *selection = document.selection(entity);
    candidates.push_back({
        .entity = entity,
        .position = transform_point(state->world_transform, {}),
        .size_pixels =
            kOverlayPointLightGizmoSizePixels * resolved_pixel_scale,
        .selectable =
            scene_hierarchy_segment_is_selectable_ignoring_visibility(
                document, entity),
        .effectively_selectable = state->effectively_selectable,
        .point_light_range = selection != nullptr && selection->selected
                                 ? std::optional<float>(light->range)
                                 : std::nullopt,
    });
  }
  return candidates;
}

std::optional<ViewportFocusBounds> ViewportSceneProjection::world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot &local_visibility) {
  QDR_PROFILE_SCOPE("Viewport scene world bounds");
  const std::optional<SceneBounds> bounds = document_world_bounds(
      document, assets, [&local_visibility](EntityId entity) {
        return local_visibility.allows(
            static_cast<ViewportLocalVisibilityEntityId>(entity));
      });
  return bounds.has_value()
             ? std::optional<ViewportFocusBounds>(
                   ViewportFocusBounds{.min = bounds->min, .max = bounds->max})
             : std::nullopt;
}

std::optional<ViewportFocusBounds> ViewportSceneProjection::selection_bounds(
    const ModelingSessionService &modeling) {
  const std::optional<SceneBounds> bounds = modeling.selection_bounds();
  return bounds.has_value()
             ? std::optional<ViewportFocusBounds>(
                   ViewportFocusBounds{.min = bounds->min, .max = bounds->max})
             : std::nullopt;
}

} // namespace quader
