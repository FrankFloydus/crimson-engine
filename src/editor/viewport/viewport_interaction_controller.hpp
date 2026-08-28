////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/viewport_tool_runtime.hpp"
#include "editor/viewport/transform_gizmo_interaction.hpp"
#include "editor/viewport/viewport_native_tool_interaction.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace quader {

class QuaderEditorApi;
class ViewportToolHoverPort;

/**
 * Coordinates editor-owned viewport interactions around active viewport tools.
 */
class ViewportInteractionController {
public:
  /// Cheap counters proving idle frames avoid material interaction work.
  struct MaterialInteractionWorkStats final {
    std::uint64_t face_pick_count = 0;
    std::uint64_t sample_request_count = 0;
    std::uint64_t apply_target_request_count = 0;
    std::uint64_t apply_face_to_mesh_request_count = 0;
    std::uint64_t transfer_face_texture_request_count = 0;
  };

  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_before_active_tool(QuaderEditorApi &editor,
                            const ViewportInteractionFrame &frame);
  void handle_after_active_tool(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool,
      ViewportToolHoverPort *tool_hover = nullptr);
  [[nodiscard]] QuaderSelectionSummary
  pick_context_menu_target(QuaderEditorApi &editor,
                           const ViewportInteractionFrame &frame);
  [[nodiscard]] std::optional<QuaderHoverTarget>
  pick_material_face_target(QuaderEditorApi &editor,
                            const ViewportInteractionFrame &frame);
  [[nodiscard]] bool cancel_active_drag(QuaderEditorApi &editor);
  [[nodiscard]] bool dragging() const;
  /// Keeps a texture-transfer press and its release in the initiating pane.
  [[nodiscard]] bool texture_transfer_pointer_active() const {
    return transfer_face_texture_pointer_.has_value();
  }
  void cancel_pending_material_interaction();
  void cancel_pending_selection_click();
  void reset(QuaderEditorApi *editor = nullptr);

  [[nodiscard]] const quader_editor_core::TransformGizmoFrame *
  transform_gizmo_frame() const;
  [[nodiscard]] const TransformGizmoSelectionCacheStats &
  transform_gizmo_selection_cache_stats() const;
  [[nodiscard]] ViewportSelectionMarquee selection_marquee() const;
  [[nodiscard]] const MaterialInteractionWorkStats &
  material_interaction_work_stats() const;

  [[nodiscard]] bool active_tool_owns_viewport(ViewportToolId tool);
  [[nodiscard]] bool active_tool_blocks_viewport_focus_shortcuts(
      ViewportToolId tool);
  [[nodiscard]] bool active_tool_allows_selection_fallback(
      ViewportToolId tool) const;
  [[nodiscard]] bool
  has_active_modal_preview(const ViewportToolPreviewPort &preview,
                           bool snap_enabled = true) const;
  [[nodiscard]] ModelingBoxPreview
  box_preview(const ViewportToolPreviewPort &preview,
              bool snap_enabled = true) const;
  [[nodiscard]] const ViewportTool *active_tool(ViewportToolId tool);
  bool set_active_tool(ViewportToolState &state, ViewportToolId tool,
                       ViewportInputResult *result = nullptr);
  void clear_modal_tool_state();
  void clear_modal_drag_state();
  ViewportToolResult handle_active_tool_event(ViewportToolState &state,
                                              ViewportToolContext &context,
                                              const ViewportToolEvent &event);
  [[nodiscard]] ViewportNativeToolInteractionResult handle_native_tools(
      QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
      PivotToolService *pivot, LinearArrayToolService *linear_array,
      RadialArrayToolService *radial_array,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame);
  void clear_native_tool_drag_state();
  [[nodiscard]] bool native_tool_dragging() const;

private:
  /// User intent retained only while disambiguating click from double-click.
  struct PendingApplyActiveMaterial final {
    double deadline_seconds = 0.0;
    std::uint64_t scene_session_id = 0;
    ViewportSelectionMode selection_mode = ViewportSelectionMode::Object;
    int modeling_object_id = -1;
    int face_id = -1;
    std::string active_material_id;
  };

  /// Keeps the original scene and selected source for a reserved mouse gesture.
  struct FaceTextureTransferPointer final {
    SceneSessionId scene_session_id;
    std::uint64_t selection_revision = 0;
  };

  void handle_material_interactions(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool);
  [[nodiscard]] std::optional<ViewportBeforeToolInteractionResult>
  reserve_face_texture_transfer(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame);
  [[nodiscard]] std::optional<QuaderHoverTarget> pick_material_face_target(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      QVec2 local_mouse);
  void sync_tool_runtime(ViewportToolId tool);

  ViewportToolRuntime tool_runtime_;
  ViewportNativeToolInteraction native_tools_;
  TransformGizmoInteraction transform_gizmo_;
  ViewportSelectionInteraction selection_;
  std::optional<PendingApplyActiveMaterial> pending_apply_active_material_;
  // Ownership only: transfer happens on the semantic press, never while held.
  std::optional<FaceTextureTransferPointer> transfer_face_texture_pointer_;
  // A reset drops pane ownership, but the old release must not become a click
  // in the new scene. A fresh press starts a new gesture and clears this bit.
  bool discard_transfer_pointer_release_ = false;
  MaterialInteractionWorkStats material_interaction_work_stats_;
};

} // namespace quader
