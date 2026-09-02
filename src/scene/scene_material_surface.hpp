////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/scene_document.hpp"

namespace quader {

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
