////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/OverlayFaceFillBuilder.h"

#include "core/ColorSpace.h"

#include <cstdint>

namespace quader {

OverlayFaceFillMesh OverlayFaceFillBuilder::buildLayer(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer) {
    OverlayFaceFillMesh mesh;
    mesh.vertices.reserve(frame.triangles.size() * 3u);
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        if (triangle.layer == layer) {
            appendTriangle(mesh, triangle);
        }
    }
    mesh.indices.reserve(mesh.vertices.size());
    for (std::uint32_t index = 0; index < mesh.vertices.size(); ++index) {
        mesh.indices.push_back(index);
    }
    return mesh;
}

void OverlayFaceFillBuilder::appendTriangle(OverlayFaceFillMesh& mesh, const ViewportOverlayTriangle& triangle) {
    const std::uint32_t linearColor = rgba8SrgbToLinear(triangle.color);
    mesh.vertices.push_back({{triangle.a.x, triangle.a.y, triangle.a.z}, {}, linearColor});
    mesh.vertices.push_back({{triangle.b.x, triangle.b.y, triangle.b.z}, {}, linearColor});
    mesh.vertices.push_back({{triangle.c.x, triangle.c.y, triangle.c.z}, {}, linearColor});
}

} // namespace quader
