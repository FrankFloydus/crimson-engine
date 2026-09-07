////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace quader {

struct MeshAsset;
struct MeshGeometryPayload;

/**
 * Pins immutable local triangle faces derived from rendered mesh primitives.
 * Faces retain native winding and Godot-compatible coordinate snapping. Empty
 * input has no faces; invalid input has no faces and a retained diagnostic.
 */
struct MeshCollisionGeometryPayload final {
  std::vector<QVec3> faces;
  std::uint64_t content_id = 0u;
  std::string error_message;
};

/**
 * Returns a shared derived payload with an O(1) cache check before extraction.
 * Concurrent readers of immutable geometry are safe. Geometry edits, replacement,
 * and destruction require external synchronization or a separate pinned snapshot.
 */
[[nodiscard]] std::shared_ptr<const MeshCollisionGeometryPayload>
mesh_collision_geometry_payload(const MeshGeometryPayload &geometry);

[[nodiscard]] std::shared_ptr<const MeshCollisionGeometryPayload>
mesh_collision_geometry_payload(const MeshAsset &mesh);

} // namespace quader
