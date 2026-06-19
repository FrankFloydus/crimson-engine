#pragma once

#include <quader_geometry/geometry.h>
#include <quader_poly/document.h>

#include <vector>

namespace quader_poly::document_internal {

using Plane3 = quader_geometry::Plane3<float>;

struct HullVertex {
    ElementId id = invalid_element_id;
    Vec3 position;
};

struct HullFace {
    Plane3 plane { {}, 0.0F };
    std::vector<ElementId> vertex_ids;
};

struct HullGeometry {
    std::vector<HullVertex> vertices;
    std::vector<HullFace> faces;
};

} // namespace quader_poly::document_internal
