////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <vector>

#include <editor/modeling/runtime/session.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {

class ModelingObjectRuntimeMap;
class SceneDocument;

/**
 * Owns Picking Cache Projection behavior within the derived projection and cache layer.
 */
class PickingCacheProjection final {
public:
  [[nodiscard]] static std::vector<quader_editor_core::PolygonObject>
  scene_transformed_objects_for_picking(
      const std::vector<quader_editor_core::PolygonObject> &objects,
      const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map);
  [[nodiscard]] static quader_editor_core::PickObjectBounds
  build_object_pick_bounds(const quader_editor_core::PolygonObject &object);
  [[nodiscard]] static std::uint64_t object_face_geometry_signature(
      const quader_editor_core::PolygonObject &object,
      quader_poly::PickOptions pick_options = {});
};

} // namespace quader
