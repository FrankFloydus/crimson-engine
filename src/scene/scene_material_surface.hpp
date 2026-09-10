////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/scene_document.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace quader {

/** Reuses a binding or stages the lowest free nondefault slot deterministically. */
[[nodiscard]] inline std::uint32_t stage_scene_material_slot(
    MaterialComponent &component, MaterialReference material) {
  if (component.material_for_slot(0u) == material) return 0u;
  const auto bound = std::min_element(
      component.material_slots.begin(), component.material_slots.end(),
      [&](const auto &left, const auto &right) {
        const bool left_matches = left.second == material;
        const bool right_matches = right.second == material;
        return left_matches != right_matches ? left_matches
                                             : left.first < right.first;
      });
  if (bound != component.material_slots.end() && bound->second == material) {
    return bound->first;
  }
  std::uint32_t slot = 1u;
  while (component.material_slots.contains(slot)) {
    if (slot == std::numeric_limits<std::uint32_t>::max()) {
      throw std::overflow_error("No material slots remain available.");
    }
    ++slot;
  }
  component.material_slots.emplace(slot, material);
  return slot;
}

/** Reports whether an ordinary Curve currently contributes a rendered tube. */
[[nodiscard]] inline bool scene_entity_has_solid_curve_surface(
    const SceneDocument &scene, EntityId entity) {
  const CurveComponent *curve = scene.curve(entity);
  return curve != nullptr && curve->solidify &&
         curve_component_is_valid(*curve);
}

/** Central scene fact used by material assignment and presentation scopes. */
[[nodiscard]] inline bool scene_entity_has_material_surface(
    const SceneDocument &scene, EntityId entity) {
  return scene.exists(entity) &&
         (scene.mesh(entity) != nullptr ||
          scene_entity_has_solid_curve_surface(scene, entity));
}

} // namespace quader
