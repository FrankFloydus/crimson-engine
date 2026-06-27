////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/color_space.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/overlay_mesh_types.hpp"

namespace quader {

/**
 * Builds Overlay Face Fill Builder data for the render extraction and Filament resource layer.
 */
class OverlayFaceFillBuilder {
public:
    static OverlayFaceFillMesh buildLayer(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer);

    static void appendTriangle(OverlayFaceFillMesh& mesh, const ViewportOverlayTriangle& triangle);
};

} // namespace quader
