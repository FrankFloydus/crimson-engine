////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/viewport_overlay_frame.hpp"

namespace quader_editor_core {
struct TransformGizmoFrame;
}

namespace quader {

struct LinearArrayHandleState;
struct PivotHandleState;
struct RadialArrayCenterState;
class ViewportCameraController;
struct ViewportSelectionMarquee;

/**
 * Adapts editor tool state into renderer-neutral semantic overlay records.
 */
class ViewportToolOverlayProjection final {
public:
  static void append_box(ViewportOverlayFrame &frame,
                         const ViewportOverlayCamera &camera,
                         const ModelingBoxPreview &preview);
  static void append_transform_gizmo(
      ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
      const quader_editor_core::TransformGizmoFrame &gizmo);
  static void append_selection_marquee(
      ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
      const ViewportSelectionMarquee &marquee);
  static void append_pivot_handle(ViewportOverlayFrame &frame,
                                  const ViewportCameraController &camera,
                                  const PivotHandleState &handle);
  static void append_linear_array_handle(
      ViewportOverlayFrame &frame, const LinearArrayHandleState &handle);
  static void append_radial_array_center(
      ViewportOverlayFrame &frame, const RadialArrayCenterState &center);

  [[nodiscard]] static RenderOverlaySelectionMode
  selection_mode(ModelingSelectionMode mode);
};

} // namespace quader
