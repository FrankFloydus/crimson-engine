////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/tools/viewport_curve_interaction_port.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "viewport/curve_tool_preview.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <editor/interactions/move_tool.hpp>
#include <editor/interactions/transform_gizmo.hpp>
#include <editor/interactions/transform_drag.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

class QuaderEditorApi;
struct QuaderOperationInputContext;

/// Counts camera-independent selection-pivot cache work for diagnostics and tests.
struct TransformGizmoSelectionCacheStats {
  std::uint64_t selection_pivot_recompute_count = 0;
  std::uint64_t selection_cache_hit_count = 0;
  std::uint64_t curve_control_pivot_recompute_count = 0;
  std::uint64_t curve_control_cache_hit_count = 0;
};

/**
 * Owns transform gizmo hit testing and transform drag lifecycle routing for viewport frames.
 */
class TransformGizmoInteraction {
public:
  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_frame(QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
               ViewportCurveInteractionPort *curve_tool = nullptr);
  [[nodiscard]] bool cancel_active_drag(QuaderEditorApi &editor);
  [[nodiscard]] bool cancel_active_drag(QuaderEditorApi &editor,
                                        ViewportCurveInteractionPort *curve_tool);
  void reset(QuaderEditorApi *editor = nullptr);
  void reset(QuaderEditorApi *editor,
             ViewportCurveInteractionPort *curve_tool);

  [[nodiscard]] bool dragging() const { return dragging_; }
  /// Copies accepted hit/gesture state without picking or querying scene data.
  void populate_operation_input_context(
      QuaderOperationInputContext &context,
      const ViewportInteractionFrame &frame) const;
  [[nodiscard]] ViewportPointerCursor pointer_cursor() const {
    if (!current_frame_ || current_frame_->info.tool !=
                               quader_editor_core::ViewportInteractionTool::Rotate) {
      return ViewportPointerCursor::Default;
    }
    const auto axis = dragging_ ? active_axis_
                     : pending_drag_ ? pending_drag_->axis : hover_axis_;
    if (axis != quader_editor_core::TransformAxis::All) {
      return ViewportPointerCursor::Default;
    }
    return dragging_ ? ViewportPointerCursor::ClosedHand
                     : ViewportPointerCursor::OpenHand;
  }
  /// A pending gizmo click owns its release before a drag preview exists.
  [[nodiscard]] bool pointer_interaction_active() const {
    return dragging_ || pending_drag_.has_value();
  }
  [[nodiscard]] const quader_editor_core::TransformGizmoFrame *frame() const;
  [[nodiscard]] const std::vector<quader_editor_core::SelectionOverlayPoint> &
  snap_overlay_points() const {
    return snap_overlay_points_;
  }
  [[nodiscard]] const TransformGizmoSelectionCacheStats &
  selection_cache_stats() const {
    return selection_cache_stats_;
  }

private:
    /// Chooses the owner that receives one pending or active gizmo gesture.
    enum class DragBackend {
      None,
      EditorTransform,
      CurveControl,
    };

    /**
     * Pins the owner-issued complete-selection token without retaining its
     * service or copying its control vector on every idle gizmo frame. The
     * active control remains explicit so a pending Move cannot retarget.
     */
    struct CurveControlSelectionIdentity {
      std::optional<CurveControlTarget> active;
      std::size_t selected_count = 0;
      std::uint64_t selection_revision = 0;
      EntityId entity = kInvalidEntityId;
      int modeling_object_id = -1;
      bool drawing = false;
    };

    /**
     * Captures a validated Curve control selection for an idle gizmo frame.
     */
    struct CurveControlSelectionSnapshot {
      CurveControlSelectionIdentity identity;
      QVec3 world_pivot = {};
      QuaderTransform3 orientation = {};
      bool anchors_only = false;
    };

    /**
     * Excludes hover-only tool revisions from curve-control pivot invalidation.
     */
    struct CurveControlCacheKey {
      CurveControlSelectionIdentity identity;
      std::uint64_t content_identity = 0;
      QMat4 world_transform;
      QuaderTransformSpace transform_space = QuaderTransformSpace::Global;
    };

    /**
     * Stores a gizmo hit that is waiting for drag-threshold confirmation.
     */
    struct PendingGizmoDrag {
      ViewportPointerGesture pointer;
      QuaderTransformDragBeginRequest begin_request;
      ViewportToolId tool = ViewportToolId::Select;
      quader_editor_core::TransformAxis axis =
          quader_editor_core::TransformAxis::None;
      quader::QVec2 click_local = {};
      int click_count = 1;
      bool cycle_depth = false;
      ViewportModifiers cycle_depth_modifiers = {};
      DragBackend backend = DragBackend::EditorTransform;
      std::optional<CurveControlSelectionIdentity> curve_selection;
    };

    /**
     * Captures the inputs that can change an active transform preview.
     */
    struct DragUpdateSignature {
      quader::QVec2 screen_position = {};
      quader::QVec2 viewport_size = {};
      int grid_preset = 0;
      float vertex_snap_radius = 0.0f;
      bool grid_snap_enabled = false;
      double rotation_snap_degrees = 0.0;
      bool precision = false;
    };

    [[nodiscard]] bool
    pending_drag_threshold_reached(quader::QVec2 current) const;
    [[nodiscard]] bool
    begin_pending_drag(QuaderEditorApi &editor,
                       const ViewportInteractionFrame &frame,
                       ViewportCurveInteractionPort *curve_tool);
    [[nodiscard]] ViewportBeforeToolInteractionResult release_pending_click();
    [[nodiscard]] std::optional<CurveControlSelectionSnapshot>
    selected_curve_controls(ViewportCurveInteractionPort *curve_tool,
                            QuaderTransformSpace transform_space);
    [[nodiscard]] static bool
    curve_control_selection_identity_matches(
        ViewportCurveInteractionPort *curve_tool,
        const CurveControlSelectionIdentity &identity);
    [[nodiscard]] bool
    matches_last_drag_update(const DragUpdateSignature &signature) const;
    void reset_drag_state();
    void reset_pending_drag();
    void reset_curve_control_cache();
    void clear_frame();

    bool dragging_ = false;
    QuaderTransformOperation active_operation_ = QuaderTransformOperation::Transform;
    bool active_duplicate_ = false;
    bool presented_curve_route_ = false;
    bool presented_curve_anchors_only_ = false;
    DragBackend active_backend_ = DragBackend::None;
    std::optional<PendingGizmoDrag> pending_drag_;
    std::optional<CurveControlSelectionIdentity> active_curve_selection_;
    quader_editor_core::MoveToolState curve_move_state_;
    bool curve_anchor_snap_eligible_ = false;
    std::vector<ViewportCurveAnchorSnapCandidate>
        curve_anchor_snap_candidates_;
    SceneSurfaceQueryScratch surface_query_scratch_;
    std::vector<quader_editor_core::SelectionOverlayPoint>
        snap_overlay_points_;
    quader_editor_core::TransformAxis active_axis_ =
        quader_editor_core::TransformAxis::None;
    quader_editor_core::TransformAxis hover_axis_ =
        quader_editor_core::TransformAxis::None;
    quader::QVec3 active_drag_pivot_ = {};
    quader::QVec3 active_world_direction_ = {};
    quader_poly::Transform3 active_orientation_ = {};
    std::optional<quader::QVec3> rotation_indicator_start_direction_;
    std::optional<DragUpdateSignature> last_drag_update_signature_;
    quader_editor_core::TransformGizmoRotateIndicator
        active_rotate_indicator_;
    std::optional<quader_editor_core::TransformGizmoFrame> current_frame_;
    std::optional<ViewportInteractionSelectionState> cached_selection_state_;
    std::optional<quader::QVec3> cached_selection_pivot_;
    QuaderTransform3 cached_selection_orientation_ = {};
    std::uint64_t cached_transform_space_revision_ = 0;
    bool has_cached_transform_space_revision_ = false;
    std::optional<CurveControlCacheKey> cached_curve_control_key_;
    std::optional<CurveControlSelectionSnapshot>
        cached_curve_control_snapshot_;
    TransformGizmoSelectionCacheStats selection_cache_stats_;
};

} // namespace quader
