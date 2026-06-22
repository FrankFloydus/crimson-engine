////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <geometry/geometry.h>
#include <modeling/polygon/document.h>

#include <vector>

namespace quader_poly::document_internal {

using Plane3 = quader_geometry::Plane3<float>;

/**
 * Represents a Hull Vertex value used by the polygon document and mesh editing core.
 */
struct HullVertex {
    ElementId id = invalid_element_id;
    Vec3 position;
};

/**
 * Represents a Hull Face value used by the polygon document and mesh editing core.
 */
struct HullFace {
    Plane3 plane { {}, 0.0F };
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
