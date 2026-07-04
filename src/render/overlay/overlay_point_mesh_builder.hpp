////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/color_space.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_mesh_types.hpp"

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Builds Overlay Point Mesh Builder data for the render extraction and Filament resource layer.
 */
class OverlayPointMeshBuilder {
public:
  static OverlayPointMesh
  build(const std::vector<const ViewportOverlayPoint *> &points,
        const ViewportOverlayCamera &camera, float size_pixels,
        float depth_bias, std::uint32_t override_color,
        bool use_override_color);

  static OverlayPointMesh
  build_icons(const std::vector<const ViewportOverlayIcon *> &icons,
              const ViewportOverlayCamera &camera, float size_pixels,
              float depth_bias);

  static bool append_point_quad(OverlayPointMesh &mesh, QVec3 position,
                                std::uint32_t color,
                                const OverlayCameraProjector &projector,
                                float size_pixels, float depth_bias);
};

} // namespace quader
