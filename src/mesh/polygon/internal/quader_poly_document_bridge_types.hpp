////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

/**
 * Represents a Connect Edge Face Path value used by the polygon document and mesh editing core.
 */
struct ConnectEdgeFacePath {
    std::vector<std::size_t> face_indices;
    std::vector<Edge> shared_edges;
};

/**
 * Represents a Connect Edge Face Region value used by the polygon document and mesh editing core.
 */
struct ConnectEdgeFaceRegion {
    std::vector<std::size_t> face_indices;
    std::vector<Edge> boundary_selected_edges;
};

/**
 * Represents a Connect Edge Pair Path value used by the polygon document and mesh editing core.
 */
struct ConnectEdgePairPath {
    Edge first;
    Edge second;
    ConnectEdgeFacePath path;
};

/**
 * Stores the Open Bridge Edge Info data contract used by the polygon document and mesh editing core.
 */
struct OpenBridgeEdgeInfo {
    Edge edge;
    std::pair<ElementId, ElementId> oriented_face_edge;
    quader::QVec3 face_normal;
    std::uint32_t material_slot = 0;
};

/**
 * Stores the Bridge Rail Step Key data contract used by the polygon document and mesh editing core.
 */
struct BridgeRailStepKey {
  ElementId first = kInvalidElementId;
  ElementId second = kInvalidElementId;
  int step = 0;

  friend bool operator<(const BridgeRailStepKey &left,
                        const BridgeRailStepKey &right) {
    if (left.first != right.first) {
      return left.first < right.first;
    }
    if (left.second != right.second) {
      return left.second < right.second;
    }
    return left.step < right.step;
  }
};

/**
 * Represents a Face Island Boundary value used by the polygon document and mesh editing core.
 */
struct FaceIslandBoundary {
    std::vector<ElementId> face_ids;
    std::vector<Edge> edges;
    std::vector<ElementId> vertices;
    quader::QVec3 normal;
    std::uint32_t material_slot = 0;
    bool has_material_slot = false;
    bool all_open = false;
};

} // namespace quader_poly::document_internal
