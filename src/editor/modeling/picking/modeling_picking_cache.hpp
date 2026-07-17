////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <editor/modeling/runtime/session.hpp>

namespace quader {

/**
 * Represents a Modeling Pick Face Geometry Cache Entry value used by the modeling domain.
 */
struct ModelingPickFaceGeometryCacheEntry {
  int object_id = -1;
  std::uintptr_t topology_storage = 0;
  std::uint64_t geometry_signature = 0;
};

/// Pins reusable local object bounds for one immutable topology signature.
struct ModelingSharedPickBoundsCacheEntry {
  std::uint64_t geometry_signature = 0;
  quader_editor_core::PickObjectBounds bounds;
};

/// Pins reusable local face triangles for one immutable topology signature.
struct ModelingSharedPickFaceGeometryCacheEntry {
  std::uintptr_t topology_storage = 0;
  std::uint64_t geometry_signature = 0;
  std::shared_ptr<const std::vector<quader_poly::PickFaceTriangle>> triangles;
};

/**
 * Represents a Modeling Picking Cache value used by the modeling domain.
 */
struct ModelingPickingCache {
  bool objects_valid = false;
  bool world_objects_valid = false;
  bool bounds_valid = false;
  bool face_geometry_valid = false;
  bool local_face_geometry_valid = false;
  bool local_face_geometry_has_hidden_material_slot = false;
  std::uint32_t local_face_geometry_hidden_material_slot = 0;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t scene_selection_revision = 0;
  std::uint64_t scene_transform_revision = 0;
  std::uint64_t scene_visibility_revision = 0;
  std::uint64_t scene_layer_revision = 0;
  std::uint64_t scene_mesh_binding_revision = 0;
  std::uint64_t viewport_visibility_revision = 0;
  std::uint64_t object_map_revision = 0;
  ModelingInstanceEditScope instance_edit_scope;
  ModelingSelectionMode app_selection_mode = ModelingSelectionMode::Object;
  std::vector<quader_editor_core::PolygonObject> objects;
  std::vector<quader_editor_core::PickObjectTransform> object_transforms;
  std::vector<quader_editor_core::PolygonObject> world_objects;
  std::unordered_map<int, EntityId> scene_target_by_participant;
  std::vector<quader_editor_core::PickObjectBounds> bounds;
  std::vector<quader_editor_core::PickObjectFaceGeometry> face_geometry;
  std::vector<ModelingPickFaceGeometryCacheEntry> face_geometry_entries;
  std::vector<quader_editor_core::PickObjectFaceGeometry>
      local_face_geometry;
  std::vector<ModelingPickFaceGeometryCacheEntry>
      local_face_geometry_entries;
  std::unordered_map<std::uintptr_t, ModelingSharedPickBoundsCacheEntry>
      shared_local_bounds;
  std::vector<ModelingSharedPickFaceGeometryCacheEntry>
      shared_local_face_geometry;
};

} // namespace quader
