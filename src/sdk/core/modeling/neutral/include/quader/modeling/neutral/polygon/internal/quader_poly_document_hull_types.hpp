////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <vector>

namespace quader_poly::document_internal {

using QPlane3 = quader_geometry::QPlane3<float>;

/**
 * Represents a Hull Vertex value used by the polygon document and mesh editing core.
 */
struct HullVertex {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents a Hull Face value used by the polygon document and mesh editing core.
 */
struct HullFace {
    QPlane3 plane { {}, 0.0F };
    std::vector<ElementId> vertex_ids;
};

/**
 * Represents a Hull Geometry value used by the polygon document and mesh editing core.
 */
struct HullGeometry {
    std::vector<HullVertex> vertices;
    std::vector<HullFace> faces;
};

} // namespace quader_poly::document_internal
