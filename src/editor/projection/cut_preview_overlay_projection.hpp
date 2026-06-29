////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <vector>

#include <mesh/polygon/document.hpp>

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

    [[nodiscard]] static PreviewTriangleMesh meshFromDocuments(
            const std::vector<quader_poly::Document>& documents);
    static void appendMeshFill(
            ViewportOverlayFrame& frame,
            const PreviewTriangleMesh& mesh,
            std::uint32_t color,
            ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::ToolPreviewFill);
    static void appendResultLines(
            ViewportOverlayFrame& frame,
            const PreviewTriangleMesh& mesh,
            QVec3 planePoint,
            QVec3 planeNormal,
            std::uint32_t outlineColor);
};

} // namespace quader
