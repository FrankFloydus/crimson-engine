////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_preview_types.hpp"
#include "math/types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Projects cut preview fill, outline, and seam geometry into viewport overlay primitives.
 */
class CutPreviewOverlayProjection final {
public:
    /**
     * Represents a Preview Triangle Mesh value used by cut preview projection.
     */
    struct PreviewTriangleMesh {
        std::vector<QVec3> vertices;
        std::vector<std::uint32_t> indices;

        [[nodiscard]] bool empty() const {
            return vertices.empty() || indices.empty();
        }
    };

    [[nodiscard]] static PreviewTriangleMesh
    mesh_from_previews(const std::vector<ModelingToolPreviewMesh> &previews);
    static void append_mesh_fill(ViewportOverlayFrame &frame,
                                 const PreviewTriangleMesh &mesh,
                                 std::uint32_t color,
                                 ViewportOverlayStyleRole style_role =
                                     ViewportOverlayStyleRole::ToolPreviewFill);
    static void append_result_lines(ViewportOverlayFrame &frame,
                                    const PreviewTriangleMesh &mesh,
                                    QVec3 plane_point, QVec3 plane_normal,
                                    std::uint32_t outline_color);
};

} // namespace quader
