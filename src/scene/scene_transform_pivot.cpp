////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_transform_pivot.hpp"

#include "assets/asset_registry.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <cmath>
#include <vector>

namespace quader {
namespace {

bool finite_point(QVec3 point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

std::optional<QVec3> finite_pivot(QVec3 pivot) {
  return finite_point(pivot) ? std::optional<QVec3>{pivot} : std::nullopt;
}

} // namespace

std::optional<QVec3> resolve_entity_visible_pivot(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity,
    VisiblePivotBoundsScope bounds_scope) {
  if (!scene.exists(entity)) {
    return std::nullopt;
  }

  const TransformComponent *transform = scene.transform(entity);
  if (transform == nullptr) {
    return std::nullopt;
  }

  const QVec3 world_origin =
      transform_point(scene.world_transform_matrix(entity), {});
  if (transform->custom_pivot) {
    return finite_pivot(world_origin);
  }

  const std::optional<SceneBounds> bounds =
      bounds_scope == VisiblePivotBoundsScope::Hierarchy
          ? entity_hierarchy_world_bounds(scene, assets, entity)
          : entity_world_bounds(scene, assets, entity);
  return finite_pivot(bounds.has_value() ? bounds_center(*bounds)
                                         : world_origin);
}

std::optional<QVec3> resolve_entity_transform_visible_pivot(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity) {
  return resolve_entity_visible_pivot(
      scene, assets, entity,
      scene.has_children(entity) ? VisiblePivotBoundsScope::Hierarchy
                                 : VisiblePivotBoundsScope::Entity);
}

std::optional<QVec3>
resolve_scene_selection_visible_pivot(const SceneDocument &scene,
                                      const AssetRegistry &assets) {
  const std::vector<EntityId> selected = scene.selected_entities();
  if (selected.empty()) {
    return std::nullopt;
  }

  if (selected.size() == 1u) {
    return resolve_entity_transform_visible_pivot(scene, assets,
                                                  selected.front());
  }

  const std::optional<SceneBounds> bounds =
      selected_transform_world_bounds(scene, assets);
  return bounds.has_value() ? finite_pivot(bounds_center(*bounds))
                            : std::nullopt;
}

} // namespace quader
