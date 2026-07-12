////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/overlay_mesh_types.hpp"

#include <optional>

namespace quader {

/**
 * Represents an Overlay View Point value used by the render extraction and Filament resource layer.
 */
struct OverlayViewPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/**
 * Represents an Overlay Projected Point value used by the render extraction and Filament resource layer.
 */
struct OverlayProjectedPoint {
    OverlayFloat4 device;
    QVec2 pixel;
    bool valid = false;
};

/**
 * Represents an Overlay Clipped Line value used by the render extraction and Filament resource layer.
 */
struct OverlayClippedLine {
    OverlayViewPoint start;
    OverlayViewPoint end;
};

/**
 * Owns Overlay Camera Projector behavior within the render extraction and Filament resource layer.
 */
class OverlayCameraProjector {
public:
    explicit OverlayCameraProjector(ViewportOverlayCamera camera);

    const ViewportOverlayCamera& camera() const;

    static ViewportOverlayCamera sanitized_camera(ViewportOverlayCamera camera);

    OverlayViewPoint view_point_for(QVec3 world) const;

    float near_plane_distance(const OverlayViewPoint &point) const;

    std::optional<OverlayClippedLine> clip_line_to_near_plane(QVec3 start,
                                                              QVec3 end) const;

    OverlayProjectedPoint project(OverlayViewPoint point) const;

    void apply_device_depth_bias(OverlayProjectedPoint &projected,
                                 float depth_bias_units) const;

    OverlayFloat4 with_pixel_offset(OverlayFloat4 device,
                                    QVec2 pixel_offset) const;

  private:
    static constexpr float kPi = 3.14159265358979323846f;

    ViewportOverlayCamera camera_;
    float aspect_ = 1.0f;
    float tan_half_fov_ = 1.0f;
    float orthographic_half_height_ = 1.0f;
    float orthographic_half_width_ = 1.0f;
    float orthographic_depth_scale_ = 1.0f;
    float orthographic_depth_offset_ = 0.0f;
};

} // namespace quader
