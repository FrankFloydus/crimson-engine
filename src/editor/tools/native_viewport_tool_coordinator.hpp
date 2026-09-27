////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "assets/material_reference.hpp"
#include "core/types.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/placement/snap_to_floor_tool_state_port.hpp"
#include "editor/tools/light_creation_tool_service.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/tools/extrude_viewport_interaction.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"
#include "editor/tools/viewport_curve_interaction_port.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"

#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace quader {

class EditorSelectionService;
class CurveToolService;
class LinearArrayToolService;
class MaterialReference;
class ModelingSessionService;
class PathArrayToolService;
class PivotToolService;
class PrimitiveCreationToolService;
class RadialArrayToolService;
class UvPeelToolService;

/**
 * Borrows one coherent, owner-derived view of native-tool presentation state.
 * The Curve preview remains owned by CurveToolService and is valid until the
 * coordinator's next refresh or mutation.
 */
struct NativeViewportToolPresentationSnapshot final {
  PivotHandleState pivot_handle;
  LinearArrayHandleState linear_array_handle;
  RadialArrayCenterState radial_array_center;
  ExtrudeHandleState extrusion_handle;
  const CurveToolPreviewState *curve_preview = nullptr;
  bool curve_controls_editable = false;
  bool curve_dragging = false;
  bool path_array_source_picking = false;
  std::uint64_t selection_pivot_revision = 0;
};

/**
 * Routes native editor-owned viewport tools through one lifecycle boundary.
 */
class NativeViewportToolCoordinator final
    : public SnapToFloorToolStatePort,
      private ViewportNativeToolLifecyclePort {
public:
  NativeViewportToolCoordinator(PivotToolService &pivot,
                                LinearArrayToolService &linear_array,
                                RadialArrayToolService &radial_array,
                                PathArrayToolService &path_array,
                                PrimitiveCreationToolService &primitives,
                                LightCreationToolService &light_creation,
                                EditorSelectionService &selection,
                                ModelingSessionService &modeling,
                                CurveToolService &curve,
                                UvPeelToolService &uv_peel,
                                ExtrudeToolService &extrude,
                                std::function<CommandResult(MaterialReference)> settle_modeling);
  NativeViewportToolCoordinator(const NativeViewportToolCoordinator &) = delete;
  NativeViewportToolCoordinator &
  operator=(const NativeViewportToolCoordinator &) = delete;
  NativeViewportToolCoordinator(NativeViewportToolCoordinator &&) = delete;
  NativeViewportToolCoordinator &
  operator=(NativeViewportToolCoordinator &&) = delete;
  ~NativeViewportToolCoordinator() override;

  [[nodiscard]] bool handles(std::string_view tool_id) const;
  [[nodiscard]] bool active(std::string_view tool_id) const {
    return runtime_.native_lifecycle_active(tool_id);
  }
  [[nodiscard]] bool pending(std::string_view tool_id) const {
    return runtime_.native_lifecycle_pending(tool_id);
  }
  [[nodiscard]] bool can_activate(std::string_view tool_id) const;
  [[nodiscard]] bool has_pending_tool() const override;
  /// Distinguishes uncommitted gestures from an idle persistent curve editor.
  [[nodiscard]] bool has_pending_history_preview() const;
  [[nodiscard]] bool blocks_modeling_content_mutation() const override;
  [[nodiscard]] bool blocks_document_mutation(EntityId entity) const;
  [[nodiscard]] std::string_view active_tool_id() const;
  [[nodiscard]] bool light_creation_active() const override {
    return light_creation_.active();
  }
  [[nodiscard]] std::optional<LightCreationOperation>
  active_light_creation() const {
    return light_creation_.active_operation();
  }
  [[nodiscard]] std::optional<ViewportNativeToolCommitMetadata>
  active_commit_metadata() const {
    return runtime_.active_native_lifecycle_commit_metadata();
  }
  [[nodiscard]] std::optional<ViewportNativeToolHistoryState>
  active_history_state() const {
    return runtime_.active_native_lifecycle_history_state();
  }
  [[nodiscard]] const ViewportToolRuntime &runtime() const { return runtime_; }

  /** Refreshes every native viewport tool through its editor owner. */
  [[nodiscard]] CommandResult refresh();
  [[nodiscard]] NativeViewportToolPresentationSnapshot
  presentation_snapshot() const;
  /// Projects retained input facts without repeating viewport picking.
  void populate_operation_input_context(
      QuaderOperationInputContext &context) const;

  /** Routes one toolkit-neutral pointer frame through the native-tool owner. */
  [[nodiscard]] ViewportNativeToolInteractionResult handle_pointer_frame(
      SceneDocument &document, AssetRegistry &assets,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame);
  void clear_pointer_drag_state();
  [[nodiscard]] bool pointer_dragging() const;
  [[nodiscard]] ViewportPointerCursor pointer_cursor() const;

  [[nodiscard]] bool curve_control_gizmo_start_allowed(
      ViewportToolId active_tool, bool selection_fallback_allowed,
      bool active_tool_owns_viewport, bool modal_tool_active) const;

  /** Narrow bridge used by editor-owned transform-gizmo integration. */
  [[nodiscard]] ViewportCurveInteractionPort &curve_interaction() {
    return *curve_interaction_;
  }

  CommandResult activate(std::string_view tool_id, double grid_world_size);
  /// Starts a new extrusion in the requested mode through the existing owner.
  CommandResult activate_extrude(modeling::FaceExtrudeMode mode);
  CommandResult begin_light_creation(LightCreationKind kind);
  CommandResult
  complete_active_light_creation(LightCreationOperation operation);
  CommandResult cancel_competing_state_for_tool_activation();
  /// Accepts pending work before an implicit tool transition.
  CommandResult settle_competing_state_for_tool_activation();
  /// Accepts pending authored work; failed acceptance leaves the operation active.
  CommandResult settle_transient_state();
  void set_creation_material(MaterialReference material) { creation_material_ = material; }
  [[nodiscard]] bool settlement_in_progress() const { return settling_transient_state_; }
  /// Explicit cancellation and destructive session teardown only.
  CommandResult cancel_transient_state();
  CommandResult commit_active() override;
  CommandResult undo_active();
  CommandResult redo_active();
  CommandResult cancel_active();
  CommandResult accept_active_primitive(std::string name,
                                        MaterialReference material,
                                        bool snap_enabled);
  void reset_transient_state();

private:
  PivotToolService &pivot_;
  LinearArrayToolService &linear_array_;
  RadialArrayToolService &radial_array_;
  PathArrayToolService &path_array_;
  PrimitiveCreationToolService &primitives_;
  LightCreationToolService &light_creation_;
  EditorSelectionService &selection_;
  ModelingSessionService &modeling_;
  CurveToolService &curve_;
  UvPeelToolService &uv_peel_;
  ExtrudeToolService &extrude_;
  std::function<CommandResult(MaterialReference)> settle_modeling_;
  bool settling_transient_state_ = false;
  MaterialReference creation_material_;
  ViewportToolRuntime runtime_;
  ViewportNativeToolInteraction interaction_;
  ExtrudeViewportInteraction extrude_interaction_;
  std::unique_ptr<ViewportCurveInteractionPort> curve_interaction_;
  mutable std::optional<std::uint64_t> hint_curve_selection_revision_;
  mutable bool hint_curve_anchor_selection_only_ = false;
};

} // namespace quader
