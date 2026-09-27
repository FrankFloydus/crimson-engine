////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/undo_redo_execute_options.hpp"
#include "editor/modeling/runtime/selection_types.hpp"
#include "editor/selection/editor_sub_selection_provider.hpp"
#include "viewport/curve_tool_preview.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;
class SceneDocument;
struct CurveComponent;

inline constexpr std::string_view kCurveToolId = "curve";
inline constexpr std::string_view kCurveClosedSetting = "curve.closed";
inline constexpr std::string_view kCurveTypeSetting = "curve.type";
inline constexpr std::string_view kCurveRadiusSetting = "curve.radius";
inline constexpr std::string_view kCurveLengthSetting = "curve.length";
inline constexpr std::string_view kCurveSolidifySetting = "curve.solidify";
inline constexpr std::string_view kCurveSolidifyResolutionSetting =
    "curve.solidify.resolution";
inline constexpr std::string_view kCurveSolidifyRadiusSetting =
    "curve.solidify.radius";
inline constexpr std::string_view kCurveNewAction = "curve.new";
inline constexpr std::string_view kCurveApplySolidifyAction =
    "curve.solidify.apply";

inline constexpr double kCurvePresetMinimumRadius = 0.001;
inline constexpr double kCurvePresetMaximumRadius = 100000.0;
inline constexpr double kCurvePresetMaximumLength = 200000.0;
inline constexpr double kCurvePresetDefaultRadius = 1.0;
inline constexpr double kCurveOvalDefaultLength = 4.0;

/// Selects point-by-point drawing or one complete closed placement shape.
enum class CurveCreationKind { Draw = 0, Circle = 1, Oval = 2 };

/// Remembers creation preferences across scene owners for one app lifetime.
struct CurveSessionDefaults {
  CurveCreationKind creation_kind = CurveCreationKind::Draw;
  bool closed = false;
  double circle_radius = kCurvePresetDefaultRadius;
  double oval_radius = kCurvePresetDefaultRadius;
  double oval_length = kCurveOvalDefaultLength;
};

/// Pins the snapped construction-plane frame used by one preset placement.
struct CurvePresetPlacementFrame {
  QVec3 center{};
  QVec3 axis_u{1.0f, 0.0f, 0.0f};
  QVec3 axis_v{0.0f, 1.0f, 0.0f};
};

/**
 * Owns one cubic Bezier drawing/editing gesture stream. Authored edits and
 * history are delegated to the same portable modeling session used by polygon
 * tools.
 */
class CurveToolService final : public EditorSubSelectionProvider {
public:
  CurveToolService(SceneDocument &scene, AssetRegistry &assets,
                   EditorHistoryService &history,
                   ModelingSessionService &modeling,
                   EditorSelectionService &selection,
                   DocumentCommandService &document_commands,
                   CurveSessionDefaults *session_defaults = nullptr);

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool has_session() const { return active_; }
  [[nodiscard]] bool can_activate() const;
  [[nodiscard]] bool drawing() const { return active_ && drawing_; }
  /**
   * Reports whether the retained Curve control layer is editable.
   *
   * This is deliberately independent from active(): the Curve creation tool
   * owns active(), while one selected Curve in Object/Mesh mode exposes its
   * controls through the normal selection lifecycle.
   */
  [[nodiscard]] bool controls_editable() const;
  [[nodiscard]] bool dragging() const { return drag_.has_value(); }
  [[nodiscard]] bool point_construction_dragging() const {
    return drag_.has_value() && drag_->appended_point;
  }
  [[nodiscard]] bool can_apply() const;
  [[nodiscard]] bool can_begin_new_curve() const {
    return can_activate() && !dragging() &&
           (!active_ || !drawing_ || draft_.point_count() == 0u);
  }
  [[nodiscard]] bool can_change_creation_kind() const;
  [[nodiscard]] bool can_reset_settings() const;
  /// True only for the retained, identity-checked existing Curve attachment.
  [[nodiscard]] bool has_editable_existing_curve() const;
  [[nodiscard]] bool can_apply_solidify() const;
  [[nodiscard]] bool closed() const;
  [[nodiscard]] bool solidify_enabled() const;
  [[nodiscard]] std::uint32_t solidify_resolution() const;
  [[nodiscard]] double solidify_radius() const;
  [[nodiscard]] CurveCreationKind creation_kind() const {
    return creation_kind_;
  }
  [[nodiscard]] double preset_radius() const {
    return creation_kind_ == CurveCreationKind::Oval ? oval_radius_
                                                     : circle_radius_;
  }
  [[nodiscard]] double oval_length() const { return oval_length_; }
  [[nodiscard]] std::size_t point_count() const;
  [[nodiscard]] bool owns_editing_entity(EntityId entity) const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const;
  [[nodiscard]] const CurveToolPreviewState &preview_state() const;

  CommandResult activate();
  CommandResult refresh();
  CommandResult cancel();
  CommandResult apply();
  /// Accepts a draft or control drag before leaving the Curve tool.
  CommandResult finish();
  /** Activates a detached, empty creation session without discarding a draft. */
  CommandResult begin_new_curve();
  CommandResult new_curve();
  CommandResult set_closed(bool closed);
  CommandResult set_solidify(bool enabled);
  CommandResult
  set_solidify_resolution(std::uint32_t resolution,
                          UndoRedoExecuteOptions options = {});
  CommandResult set_solidify_radius(double radius,
                                    UndoRedoExecuteOptions options = {});
  CommandResult apply_solidify();
  CommandResult set_creation_kind(CurveCreationKind kind);
  CommandResult set_preset_radius(double radius);
  CommandResult set_oval_length(double length);
  CommandResult reset_settings();
  CommandResult toggle_point_smoothness(modeling::CurvePointId point);
  CommandResult set_hover(std::optional<QVec3> world_position);
  /// Publishes a complete, non-pickable preset at the current snapped center.
  CommandResult
  set_preset_hover(std::optional<CurvePresetPlacementFrame> frame);
  /// Authors one complete preset draft at a validated construction-plane frame.
  CommandResult place_preset(CurvePresetPlacementFrame frame);
  /**
   * Applies the editor-wide selection operation to visible Curve controls.
   * baseline pins the press-time selection for rectangle gestures; direct_hit
   * supplies click activation semantics and is omitted for rectangle edits.
   */
  bool edit_control_selection(
      std::span<const CurveControlTarget> hits,
      quader_editor_core::SelectionEditOperation operation,
      const CurveControlSelection *baseline = nullptr,
      std::optional<CurveControlTarget> direct_hit = std::nullopt);

  [[nodiscard]] const CurveControlSelection &control_selection() const {
    return control_selection_;
  }
  [[nodiscard]] bool has_selected_controls() const {
    return controls_editable() && !control_selection_.empty();
  }
  [[nodiscard]] std::uint64_t control_selection_revision() const {
    return control_selection_revision_;
  }
  /// Returns the selected control set's world-space AABB center.
  [[nodiscard]] std::optional<QVec3> selected_controls_world_pivot() const;

  [[nodiscard]] EditorSubSelectionSnapshot
  sub_selection_snapshot() const override;
  [[nodiscard]] EditorSubSelectionBaseline
  capture_sub_selection_baseline() const override;
  void synchronize_sub_selection_scope() override;
  ModelingCommandResult edit_sub_elements(
      std::span<const EditorSubSelectionTarget> hits,
      EditorSubSelectionEdit edit, const EditorSubSelectionBaseline *baseline,
      std::optional<EditorSubSelectionTarget> direct_hit) override;
  ModelingCommandResult restore_sub_selection_baseline(
      const EditorSubSelectionBaseline &baseline) override;
  ModelingCommandResult preview_sub_element_hover(
      std::optional<EditorSubSelectionTarget> target) override;
  ModelingCommandResult select_all_sub_elements() override;
  ModelingCommandResult invert_sub_elements() override;
  ModelingCommandResult clear_sub_elements() override;
  ModelingCommandResult delete_selected_sub_elements() override;

  /// Starts an appended point gesture; its drag creates symmetric handles.
  CommandResult begin_point(QVec3 world_position);
  /// Starts one gizmo preview for the complete selected control set.
  CommandResult begin_selected_control_drag();
  /// Resolves an absolute world position against the frozen gesture transform.
  CommandResult update_drag(QVec3 world_position);
  CommandResult commit_drag();
  CommandResult cancel_drag();
  [[nodiscard]] std::optional<QVec3> drag_origin_world() const;

private:
  /// Pins the exact pre-gesture authored state and placement for cancellation.
  struct Drag {
    modeling::CurveDocument before;
    CurveControlSelection selection;
    CurveControlSelection selection_before;
    QMat4 inverse_world_transform;
    QVec3 origin_world;
    std::optional<modeling::CurvePointId> construction_point;
    bool appended_point = false;
    bool geometry_changed = false;
  };

  [[nodiscard]] bool editable_source(EntityId entity) const;
  [[nodiscard]] const CurveComponent *
  editable_existing_curve_component() const;
  CommandResult commit_existing_curve_component(
      CurveComponent after, UndoRedoExecuteOptions options = {});
  [[nodiscard]] std::optional<EntityId> selected_curve() const;
  [[nodiscard]] CurveControlSelection
  valid_control_selection(const CurveControlSelection &selection,
                          const modeling::CurveDocument &document) const;
  [[nodiscard]] ModelingCommandResult edit_control_selection_checked(
      std::span<const CurveControlTarget> hits,
      quader_editor_core::SelectionEditOperation operation,
      const CurveControlSelection *baseline,
      std::optional<CurveControlTarget> direct_hit);
  [[nodiscard]] std::optional<modeling::CurveDocument>
  make_current_preset_document(std::string &message) const;
  [[nodiscard]] std::optional<modeling::CurveDocument>
  place_current_preset(const CurvePresetPlacementFrame &frame,
                       std::string &message) const;
  bool convert_procedural_draft_to_draw();
  void clear_preset_placement_state();
  void rebuild_preview_cache(std::uint64_t presentation_revision) const;
  bool set_control_selection(CurveControlSelection selection);
  void attach_source(EntityId entity);
  void detach_source();
  void invalidate(bool settings_changed = false);
  void observe_owner_revisions();
  void restore_session_defaults();

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  EditorSelectionService &selection_;
  DocumentCommandService &document_commands_;
  CurveSessionDefaults local_session_defaults_;
  CurveSessionDefaults *session_defaults_ = nullptr;
  bool active_ = false;
  bool drawing_ = true;
  bool preferred_closed_ = false;
  CurveCreationKind creation_kind_ = CurveCreationKind::Draw;
  double circle_radius_ = kCurvePresetDefaultRadius;
  double oval_radius_ = kCurvePresetDefaultRadius;
  double oval_length_ = kCurveOvalDefaultLength;
  bool procedural_draft_ = false;
  std::optional<CurvePresetPlacementFrame> procedural_frame_;
  std::optional<CurvePresetPlacementFrame> preset_hover_frame_;
  std::optional<modeling::CurveDocument> preset_document_cache_;
  EntityId entity_ = kInvalidEntityId;
  int modeling_object_id_ = -1;
  std::string source_uuid_;
  modeling::CurveDocument draft_;
  std::optional<Drag> drag_;
  CurveControlSelection control_selection_;
  std::optional<EditorSubSelectionTarget> sub_element_hover_;
  std::optional<QVec3> hover_world_;
  std::uint64_t revision_ = 0;
  std::uint64_t settings_revision_ = 0;
  std::uint64_t control_selection_revision_ = 0;
  std::uint64_t observed_scene_revision_ = 0;
  std::uint64_t observed_modeling_revision_ = 0;
  std::uint64_t observed_asset_revision_ = 0;
  std::uint64_t observed_selection_revision_ = 0;
  std::uint64_t observed_modeling_selection_revision_ = 0;
  mutable std::uint64_t cached_scene_revision_ = 0;
  mutable std::uint64_t cached_modeling_revision_ = 0;
  mutable std::uint64_t cached_asset_revision_ = 0;
  mutable std::uint64_t cached_tool_revision_ = 0;
  mutable std::uint64_t preview_revision_ = 0;
  mutable CurveToolPreviewState cached_preview_;
  mutable std::uint64_t cached_control_preview_revision_ = 0;
  mutable std::uint64_t cached_control_selection_revision_ = 0;
  mutable std::optional<QVec3> cached_controls_world_pivot_;
};

} // namespace quader
