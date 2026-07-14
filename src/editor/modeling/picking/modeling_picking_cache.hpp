////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <vector>

#include <editor/modeling/runtime/session.hpp>

namespace quader {

/**
 * Represents a Modeling Pick Face Geometry Cache Entry value used by the modeling domain.
 */
struct ModelingPickFaceGeometryCacheEntry {
  int object_id = -1;
  std::uint64_t geometry_signature = 0;
};

/**
 * Represents a Modeling Picking Cache value used by the modeling domain.
 */
struct ModelingPickingCache {
  bool objects_valid = false;
  bool bounds_valid = false;
  bool face_geometry_valid = false;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t scene_transform_revision = 0;
  std::uint64_t scene_visibility_revision = 0;
  std::uint64_t scene_mesh_binding_revision = 0;
  std::uint64_t object_map_revision = 0;
  std::vector<quader_editor_core::PolygonObject> objects;
  std::vector<quader_editor_core::PickObjectBounds> bounds;
  std::vector<quader_editor_core::PickObjectFaceGeometry> face_geometry;
  std::vector<ModelingPickFaceGeometryCacheEntry> face_geometry_entries;
};

} // namespace quader
