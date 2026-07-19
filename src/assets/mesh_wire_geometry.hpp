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
#include <vector>

namespace quader {

struct MeshAsset;

/// One renderer-neutral local-space mesh edge.
struct MeshWireSegment final {
  QVec3 start;
  QVec3 end;
};

/**
 * Owns the immutable edge projection consumed by render snapshots. The asset
 * layer resolves authored polygon boundaries (or compiled-triangle fallback)
 * before render code sees the mesh.
 */
struct MeshWireGeometryPayload final {
  std::vector<MeshWireSegment> segments;
  std::uint64_t content_id = 0u;
  bool authored = false;
};

/// Renderer-neutral topology totals for one logical mesh asset.
struct MeshTopologyCounts final {
  std::uint64_t vertices = 0;
  std::uint64_t edges = 0;
  std::uint64_t faces = 0;
  std::uint64_t triangles = 0;
};

[[nodiscard]] std::shared_ptr<const MeshWireGeometryPayload>
build_mesh_wire_geometry_payload(const MeshAsset &mesh);

/** Counts authored topology when available, with compiled-triangle fallback. */
[[nodiscard]] MeshTopologyCounts mesh_topology_counts(const MeshAsset &mesh);

} // namespace quader
