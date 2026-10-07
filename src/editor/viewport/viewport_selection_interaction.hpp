////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/viewport_curve_interaction_port.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"
#include "viewport/curve_tool_preview.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/selection_edit.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

class QuaderEditorApi;
class ViewportToolHoverPort;
struct ViewportToolResult;
struct QuaderOperationInputContext;

/**
 * Stores the active viewport selection marquee for overlay presentation.
 */
struct ViewportSelectionMarquee {
    bool active = false;
    QVec2 start = {};
    QVec2 end = {};
    ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::Contain;
    /// Empty for rectangle selection; otherwise a closed even-odd path.
    std::vector<QVec2> lasso_points;
    std::uint64_t path_revision = 0;
};

/// Counts selection hit-test work for diagnostics and deterministic tests.
struct ViewportSelectionInteractionWorkStats {
  std::uint64_t hover_pick_count = 0;
  std::uint64_t paint_pick_count = 0;
};

/** Carries a checked native selection mutation and its restored/current view. */
struct ViewportSelectionApplyResult {
  ModelingCommandResult command;
  QuaderSelectionSummary selection;
};

/**
 * Owns selection fallback, paint selection, and hover sequencing for viewport frames.
 */
class ViewportSelectionInteraction {
public:
    void reset();
    /// Captures the baseline for a possible native depth double-click.
    [[nodiscard]] std::optional<ViewportInteractionSelectionState>
    prepare_frame(QuaderEditorApi &editor,
                  const ViewportInteractionFrame &frame);
    void cancel_pending_click();
    void handle_frame(QuaderEditorApi &editor,
                      const ViewportInteractionFrame &frame,
                      const ViewportToolResult &tool_result,
                      const ViewportBeforeToolInteractionResult &before_tool,
                      ViewportToolHoverPort *tool_hover,
                      ViewportCurveInteractionPort *curve = nullptr);
    [[nodiscard]] QuaderSelectionSummary
    pick_context_menu_target(QuaderEditorApi &editor,
                             const ViewportInteractionFrame &frame);
    [[nodiscard]] const ViewportSelectionMarquee &marquee() const;
    /// Projects the captured gesture; resting modifiers never create hints.
    void populate_operation_input_context(
        QuaderOperationInputContext &context,
        const ViewportInteractionFrame &frame) const;
    [[nodiscard]] bool lasso_pointer_active() const {
      return lasso_context_.has_value();
    }
    /// Projects the exact selection edit represented by the current pointer
    /// lifecycle. Toggle/non-selection gestures deliberately return no intent.
    [[nodiscard]] std::optional<quader_editor_core::SelectionEditOperation>
    pointer_edit_operation(const ViewportInteractionFrame &frame) const;
    /// Cancels the owned gesture while reserving its eventual button release.
    bool cancel_lasso_drag();
    /// Reports ownership of a marquee or held paint gesture.
    [[nodiscard]] bool pointer_interaction_active() const {
      return box_state_.active ||
             box_pointer_.has_value() || paint_selection_active_ ||
             box_release_suppression_.active();
    }
    [[nodiscard]] const ViewportSelectionInteractionWorkStats &
    work_stats() const {
      return work_stats_;
    }
    [[nodiscard]] const ModelingCommandResult &
    last_selection_command_result() const {
      return last_selection_command_result_;
    }

private:
  /// Avoids republishing empty hover on every unchanged frame.
  enum class HoverClearState : std::uint8_t {
    Armed,
    FullyCleared,
  };

  /// Invalidates queued input even if a mode round trip restores its kind.
  bool synchronize_mode_context(QuaderEditorApi &editor,
                                const ViewportInteractionFrame &frame);

  /// Preserves the first click's selection while its native double is pending.
  struct DepthClickBaseline {
    ViewportPointerGesture pointer;
    EditorSelectionBoxBaseline selection;
    const ViewportCameraController *camera = nullptr;
    QuaderRay ray;
    QVec2 viewport_size = {};
    SceneSessionId scene_session_id;
    ViewportToolId tool = ViewportToolId::Select;
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    std::uint64_t scene_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t keybindings_revision = 0;
    std::uint64_t history_revision = 0;
    std::uint64_t gesture_merge_key = 0;
    bool double_click_pending = false;
    bool has_depth_double_click = false;

    [[nodiscard]] bool matches_context(const ViewportInteractionFrame &frame,
                                       SceneSessionId current_scene) const;
    [[nodiscard]] bool matches_frame(const ViewportInteractionFrame &frame,
                                     SceneSessionId current_scene) const;
  };

  std::optional<ViewportSelectionApplyResult> commit_pick_selection(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      QVec2 local_mouse, int click_count,
      bool preserve_selection_on_miss = false,
      bool preserve_selection_on_selected_hit = false,
      bool preserve_existing_component_selection_on_hit = false,
      ViewportModifiers click_modifiers = {},
      bool cycle_depth = false, ViewportModifiers cycle_modifiers = {},
      ViewportCurveInteractionPort *curve = nullptr,
      bool curve_controls_only = false);
  void clear_curve_control_box();
  void append_lasso_point(QVec2 point, bool include_release = false);
  void collect_lasso_samples(const ViewportInteractionFrame &frame);

  HoverClearState hover_clear_state_ = HoverClearState::Armed;
  quader_editor_core::SelectionBoxState box_state_;
  std::optional<ViewportPointerGesture> box_pointer_;
  ViewportPointerReleaseSuppression box_release_suppression_;
  quader_editor_core::SelectionEditOperation box_edit_ =
      quader_editor_core::SelectionEditOperation::Replace;
  bool box_cycle_depth_ = false;
  ViewportModifiers box_cycle_modifiers_ = {};
  std::optional<DepthClickBaseline> depth_click_baseline_;
  std::optional<ResolvedSelectionTarget> box_hovered_before_selection_;
  std::optional<EditorSelectionBoxBaseline> box_baseline_;
  std::optional<DepthClickBaseline> lasso_context_;
  bool lasso_partial_intersection_ = false;
  double lasso_sample_distance_squared_ = 4.0;
  std::uint64_t lasso_sample_sequence_ = 0;
  std::uint64_t lasso_path_revision_ = 0;
  mutable ViewportSelectionMarquee marquee_cache_;
  bool curve_control_box_active_ = false;
  std::optional<ViewportSelectionMarquee> committed_marquee_;
  std::optional<ResolvedSelectionTarget> hover_suppressed_target_;
  std::optional<SceneSessionId> mode_context_scene_;
  std::uint64_t mode_context_generation_ = 0;
  QuaderSelectionMode mode_context_selection_kind_ = QuaderSelectionMode::Object;
  bool mode_context_invalidated_ = false;
  bool paint_selection_active_ = false;
  bool viewport_hover_state_initialized_ = false;
  bool viewport_hovered_last_frame_ = false;
  bool curve_anchor_hint_hovered_ = false;
  bool curve_tangent_hint_hovered_ = false;
  ViewportSelectionInteractionWorkStats work_stats_;
  ModelingCommandResult last_selection_command_result_{.ok = true};
  SceneSurfaceQueryScratch scene_surface_scratch_;
  ViewportSceneLassoProjectionCache lasso_projection_cache_;
};

} // namespace quader
