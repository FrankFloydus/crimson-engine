////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/color_space.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_mesh_types.hpp"

#include <vector>

namespace quader {

/**
 * Builds Overlay Line Mesh Builder data for the render extraction and Filament resource layer.
 */
class OverlayLineMeshBuilder {
public:
  static OverlayLineMesh
  build(const std::vector<const ViewportOverlayLine *> &lines,
        const ViewportOverlayCamera &camera, float width_pixels,
        float depth_bias);

  static void append_line_quad(OverlayLineMesh &mesh,
                               const ViewportOverlayLine &line);
};

} // namespace quader
