////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_scene_projection.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_local_visibility.hpp"

namespace quader {

std::vector<ViewportSceneBillboardPickCandidate>
ViewportSceneProjection::point_light_pick_candidates(
    const SceneDocument &document,
    const ViewportLocalVisibilitySnapshot &local_visibility) {
  std::vector<ViewportSceneBillboardPickCandidate> candidates;
  for (const EntityId entity : document.entities()) {
    const LightComponent *light = document.light(entity);
    if (light == nullptr || light->type != LightType::Point ||
        !document.is_effectively_visible(entity) ||
        !local_visibility.allows(
            static_cast<ViewportLocalVisibilityEntityId>(entity))) {
      continue;
    }
    candidates.push_back({
        .entity = entity,
        .position = transform_point(document.world_transform_matrix(entity), {}),
        .size_pixels = kOverlayPointLightGizmoSizePixels,
    });
  }
  return candidates;
}

std::optional<ViewportFocusBounds> ViewportSceneProjection::world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot &local_visibility) {
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
