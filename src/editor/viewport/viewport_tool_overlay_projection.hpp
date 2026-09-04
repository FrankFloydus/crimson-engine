////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <vector>

namespace quader_editor_core {
struct SelectionOverlayPoint;
struct TransformGizmoFrame;
}

namespace quader {

struct LinearArrayHandleState;
struct PivotHandleState;
struct RadialArrayCenterState;
class ViewportCameraController;
struct ViewportSelectionMarquee;

/** Fixed owner revisions used to invalidate pane-local tool presentation. */
struct ViewportToolOverlaySourceRevisions final {
  std::uint64_t active_tool = 0u;
  std::uint64_t modeling_content = 0u;
  std::uint64_t modeling_selection = 0u;
  std::uint64_t modeling_transform_preview = 0u;
  std::uint64_t scene_transform = 0u;
  std::uint64_t scene_selection = 0u;
  std::uint64_t transform_space = 0u;
  std::uint64_t selection_pivot = 0u;
  std::uint64_t curve_preview = 0u;
};

/**
 * Borrows one synchronous pane presentation without constructing overlay
 * vectors. The render coordinator invokes the provider only after stamp
 * invalidation; every pointer must remain valid through submit().
 */
struct ViewportToolOverlaySourceInputs final {
  std::uintptr_t source_identity = 0u;
  /// Combined interface scale and native viewport-surface DPR.
  float raster_scale = 1.0f;
  const ViewportOverlayCamera *overlay_camera = nullptr;
  const ViewportCameraController *viewport_camera = nullptr;
  const ModelingBoxPreview *box_preview = nullptr;
  const quader_editor_core::TransformGizmoFrame *transform_gizmo = nullptr;
  const std::vector<quader_editor_core::SelectionOverlayPoint> *
      transform_snap_points = nullptr;
  const ViewportSelectionMarquee *selection_marquee = nullptr;
  const PivotHandleState *pivot_handle = nullptr;
  const LinearArrayHandleState *linear_array_handle = nullptr;
  const RadialArrayCenterState *radial_array_center = nullptr;
  ViewportToolOverlaySourceRevisions revisions;
  bool interaction_pane = false;
  bool tool_overlay_visible = false;
  bool gizmos_visible = false;
  bool pivot_handle_editing = false;
  bool overlays_visible = false;
  bool radial_array_center_visible = false;
};

/**
 * Lazily compiles editor-owned semantic tool state for one viewport pane.
 * TransformGizmoInteraction remains the upstream semantic-frame owner; this
 * source gates pane projection and ViewportOverlayFrame construction.
 */
class ViewportToolOverlaySource final
    : public ViewportOverlayDynamicToolFrameProvider {
public:
  explicit ViewportToolOverlaySource(ViewportToolOverlaySourceInputs inputs);

  [[nodiscard]] ViewportOverlayDynamicToolSourceStamp source_stamp() const {
    return source_stamp_;
  }
  [[nodiscard]] ViewportOverlayFrame
  build_dynamic_tool_overlay_frame() const override;

private:
  ViewportToolOverlaySourceInputs inputs_;
  ViewportOverlayDynamicToolSourceStamp source_stamp_;
};

/**
 * Adapts editor tool state into renderer-neutral semantic overlay records.
 */
class ViewportToolOverlayProjection final {
public:
  static void append_box(ViewportOverlayFrame &frame,
                         const ViewportOverlayCamera &camera,
                         const ModelingBoxPreview &preview,
                         float raster_scale = 1.0f);
  static void append_transform_gizmo(
      ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
      const quader_editor_core::TransformGizmoFrame &gizmo);
  static void append_selection_marquee(
      ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
      const ViewportSelectionMarquee &marquee, float raster_scale = 1.0f);
  static void append_pivot_handle(ViewportOverlayFrame &frame,
                                  const ViewportCameraController &camera,
                                  const PivotHandleState &handle,
                                  float raster_scale = 1.0f);
  static void append_linear_array_handle(
      ViewportOverlayFrame &frame, const LinearArrayHandleState &handle);
  static void append_radial_array_center(
      ViewportOverlayFrame &frame, const RadialArrayCenterState &center);

  [[nodiscard]] static RenderOverlaySelectionMode
  selection_mode(ModelingSelectionMode mode);
};

} // namespace quader
