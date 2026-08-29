////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_selection_api.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "viewport/curve_tool_preview.hpp"

#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/selection_edit.hpp>

#include <cstdint>
#include <optional>

namespace quader {

class QuaderEditorApi;
class CurveToolService;
class ViewportToolHoverPort;
struct ViewportToolResult;

/**
 * Stores the active viewport selection marquee for overlay presentation.
 */
struct ViewportSelectionMarquee {
    bool active = false;
    QVec2 start = {};
    QVec2 end = {};
    ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::Contain;
};

/// Counts selection hit-test work for diagnostics and deterministic tests.
struct ViewportSelectionInteractionWorkStats {
  std::uint64_t hover_pick_count = 0;
  std::uint64_t paint_pick_count = 0;
};

/**
 * Owns selection fallback, paint selection, and hover sequencing for viewport frames.
 */
class ViewportSelectionInteraction {
public:
    void reset();
    /// Resolves an earlier single click before a new gesture can pick its gizmo.
    [[nodiscard]] std::optional<ViewportInteractionSelectionState>
    prepare_frame(QuaderEditorApi &editor,
                  const ViewportInteractionFrame &frame);
    void cancel_pending_click();
    void handle_frame(QuaderEditorApi &editor,
                      const ViewportInteractionFrame &frame,
                      const ViewportToolResult &tool_result,
                      const ViewportBeforeToolInteractionResult &before_tool,
                      ViewportToolHoverPort *tool_hover,
                      CurveToolService *curve = nullptr);
    [[nodiscard]] QuaderSelectionSummary
    pick_context_menu_target(QuaderEditorApi &editor,
                             const ViewportInteractionFrame &frame);
    [[nodiscard]] ViewportSelectionMarquee marquee() const;
    /// Reports ownership of a pending click, marquee, or held paint gesture.
    [[nodiscard]] bool pointer_interaction_active() const {
      return box_state_.active || paint_selection_active_;
    }
    [[nodiscard]] const ViewportSelectionInteractionWorkStats &
    work_stats() const {
      return work_stats_;
    }

private:
  /// Preserves the first click's selection while its native double is pending.
  struct DepthClickBaseline {
    QuaderSelectionBoxBaseline selection;
    const ViewportCameraController *camera = nullptr;
    QuaderRay ray;
    QVec2 local = {};
    QVec2 viewport_size = {};
    SceneSessionId scene_session_id;
    ViewportToolId tool = ViewportToolId::Select;
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    std::uint64_t scene_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t keybindings_revision = 0;
    bool double_click_pending = false;
    bool delay_single_click = false;
    ViewportModifiers click_modifiers = {};
    double deadline_seconds = 0.0;

    [[nodiscard]] bool matches_context(const ViewportInteractionFrame &frame,
                                       SceneSessionId current_scene) const;
    [[nodiscard]] bool matches_frame(const ViewportInteractionFrame &frame,
                                     SceneSessionId current_scene) const;
  };

  /// Freezes a completed single pick until native double-click disambiguation.
  struct PendingFirstPick {
    QuaderPickSelectRequest request;
    std::optional<EntityId> billboard_entity;
    std::optional<EntityId> curve_entity;
    ViewportModifiers modifiers = {};
    QVec2 local = {};
    QuaderHoverTarget hovered_before_selection;
  };

  /// Defers hover invalidation until the after-tool selection phase.
  struct PendingPickFeedback {
    QVec2 local = {};
    QuaderHoverTarget hovered_before_selection;
  };

  QuaderSelectionSummary commit_pending_pick(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame);
  [[nodiscard]] bool pending_click_matches_context(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame) const;
  std::optional<QuaderSelectionSummary> commit_pick_selection(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      QVec2 local_mouse, int click_count,
      bool preserve_selection_on_miss = false,
      bool preserve_selection_on_selected_hit = false,
      bool preserve_existing_component_selection_on_hit = false,
      bool cycle_depth = false, ViewportModifiers cycle_modifiers = {},
      QuaderHoverTarget hovered_before_selection = {});
  void clear_curve_control_box();

  bool hover_clear_issued_ = false;
  quader_editor_core::SelectionBoxState box_state_;
  quader_editor_core::SelectionEditOperation box_edit_ =
      quader_editor_core::SelectionEditOperation::Replace;
  bool box_cycle_depth_ = false;
  ViewportModifiers box_cycle_modifiers_ = {};
  std::optional<DepthClickBaseline> depth_click_baseline_;
  std::optional<PendingFirstPick> pending_first_pick_;
  std::optional<PendingPickFeedback> pending_pick_feedback_;
  std::optional<QuaderHoverTarget> box_hovered_before_selection_;
  std::optional<QuaderSelectionBoxBaseline> box_baseline_;
  std::optional<CurveControlSelection> curve_box_baseline_;
  bool curve_control_box_active_ = false;
  std::optional<ViewportSelectionMarquee> committed_marquee_;
  std::optional<QuaderHoverTarget> hover_suppressed_target_;
  bool paint_selection_active_ = false;
  bool viewport_hover_state_initialized_ = false;
  bool viewport_hovered_last_frame_ = false;
  ViewportSelectionInteractionWorkStats work_stats_;
};

} // namespace quader
