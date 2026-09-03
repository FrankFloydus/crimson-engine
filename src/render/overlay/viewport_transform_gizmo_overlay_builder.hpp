////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <vector>

namespace quader {

struct ViewportOverlayCamera;

/**
 * Identifies renderer-neutral transform axes and planar combinations.
 */
enum class RenderTransformAxis : std::uint8_t {
  None,
  X,
  Y,
  Z,
  Xy,
  Xz,
  Yz,
  All,
};

/**
 * Identifies the transform operation represented by a neutral gizmo record.
 */
enum class RenderTransformTool : std::uint8_t {
  Other,
  Move,
  Rotate,
  Scale,
};

/**
 * Stores one renderer-neutral axis line for transform-gizmo projection.
 */
struct RenderTransformGizmoAxis final {
  RenderTransformAxis axis = RenderTransformAxis::None;
  QVec3 world_start;
  QVec3 world_tip;
  QVec2 screen_start;
  QVec2 screen_tip;
  float alpha = 1.0f;
};

/**
 * Stores one renderer-neutral planar handle for transform-gizmo projection.
 */
struct RenderTransformGizmoPlane final {
  RenderTransformAxis axis = RenderTransformAxis::None;
  QVec3 world0;
  QVec3 world1;
  QVec3 world2;
  QVec3 world3;
  QVec2 screen0;
  QVec2 screen1;
  QVec2 screen2;
  QVec2 screen3;
};

/**
 * Stores one renderer-neutral rotation-ring segment.
 */
struct RenderTransformGizmoRingSegment final {
  RenderTransformAxis axis = RenderTransformAxis::None;
  QVec3 world_start;
  QVec3 world_end;
  QVec2 screen_start;
  QVec2 screen_end;
  float alpha = 1.0f;
  bool front_facing = true;
};

/**
 * Stores one renderer-neutral trackball-ring segment.
 */
struct RenderTransformGizmoTrackballSegment final {
  QVec3 world_start;
  QVec3 world_end;
  QVec2 screen_start;
  QVec2 screen_end;
};

/**
 * Stores the semantic rotation indicator shown during an active drag.
 */
struct RenderTransformGizmoRotateIndicator final {
  bool active = false;
  RenderTransformAxis axis = RenderTransformAxis::None;
  QVec3 world_axis;
  QVec3 start_direction;
  bool has_mouse_position = false;
  QVec3 mouse_position;
  double angle_degrees = 0.0;
};

/**
 * Stores semantic transform-gizmo state shared by all primitive records.
 */
struct RenderTransformGizmoInfo final {
  bool ok = false;
  RenderTransformTool tool = RenderTransformTool::Other;
  RenderTransformAxis hover_axis = RenderTransformAxis::None;
  RenderTransformAxis active_axis = RenderTransformAxis::None;
  QVec3 pivot;
  QVec2 screen_pivot;
  double gizmo_scale = 1.0;
  double move_center_radius_pixels = 7.0;
  RenderTransformGizmoRotateIndicator rotate_indicator;
};

/// Complete renderer-neutral transform-gizmo record.
struct RenderTransformGizmoRecord final {
  RenderTransformGizmoInfo info;
  std::vector<RenderTransformGizmoAxis> axes;
  std::vector<RenderTransformGizmoPlane> planes;
  std::vector<RenderTransformGizmoRingSegment> ring_segments;
  std::vector<RenderTransformGizmoTrackballSegment> trackball_segments;
};

/**
 * Converts transform gizmo runtime frames into viewport overlay primitives.
 */
class ViewportTransformGizmoOverlayBuilder {
public:
  static void append(ViewportOverlayFrame &overlay_frame,
                     const RenderTransformGizmoRecord &gizmo_frame,
                     const ViewportOverlayCamera &camera);
};

} // namespace quader
