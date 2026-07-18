////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader {

class ModelingObjectRuntimeMap;
class SceneDocument;

/**
 * Owns one local-space CPU picking projection and participant transforms.
 * Documents remain shared COW topology; no participant bakes a scene transform
 * into authored vertices.
 */
struct ModelingPickingProjection final {
  std::vector<quader_editor_core::PolygonObject> objects;
  std::vector<quader_editor_core::PickObjectTransform> object_transforms;
  std::unordered_map<int, EntityId> scene_target_by_participant;
};

/**
 * Owns Picking Cache Projection behavior within the derived projection and cache layer.
 */
class PickingCacheProjection final {
public:
  [[nodiscard]] static ModelingPickingProjection
  scene_transformed_objects_for_picking(
      const std::vector<quader_editor_core::PolygonObject> &objects,
      const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
      ModelingInstanceEditScope instance_edit_scope,
      ModelingSelectionMode app_selection_mode,
      const std::function<int(EntityId)> &instance_participant_id,
      const std::function<bool(EntityId)> &entity_visible = {});
  [[nodiscard]] static quader_editor_core::PickObjectBounds
  build_object_pick_bounds(const quader_editor_core::PolygonObject &object);
  [[nodiscard]] static quader_editor_core::PickObjectBounds
  transform_object_pick_bounds(
      const quader_editor_core::PickObjectBounds &local_bounds,
      int participant_id, const QMat4 &world_from_object);
  [[nodiscard]] static std::uint64_t local_face_geometry_signature(
      const quader_editor_core::PolygonObject &object,
      quader_poly::PickOptions pick_options = {});
  [[nodiscard]] static std::uint64_t object_face_geometry_signature(
      const quader_editor_core::PolygonObject &object,
      quader_poly::PickOptions pick_options = {});
};

} // namespace quader
