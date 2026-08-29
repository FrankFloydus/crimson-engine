////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "editor/modeling/runtime/selection_types.hpp"
#include "viewport/curve_tool_preview.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class ModelingSessionService;
class SceneDocument;

inline constexpr std::string_view kCurveToolId = "curve";
inline constexpr std::string_view kCurveClosedSetting = "curve.closed";
inline constexpr std::string_view kCurveNewAction = "curve.new";

/**
 * Owns one cubic Bezier drawing/editing gesture stream. Authored edits and history
 * are delegated to the same portable modeling session used by polygon tools.
 */
class CurveToolService final {
public:
  CurveToolService(SceneDocument &scene, AssetRegistry &assets,
                   EditorHistoryService &history, ModelingSessionService &modeling);

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool has_session() const { return active_; }
  [[nodiscard]] bool can_activate() const { return true; }
  [[nodiscard]] bool drawing() const { return active_ && drawing_; }
  /**
   * Reports whether the retained Curve control layer is editable.
   *
   * This is deliberately independent from active(): the Curve creation tool
   * owns active(), while one selected Curve in Object/Mesh mode exposes its
   * controls through the normal selection lifecycle.
   */
  [[nodiscard]] bool controls_editable() const {
    return (active_ && drawing_) ||
           (!drawing_ && entity_ != kInvalidEntityId);
  }
  [[nodiscard]] bool dragging() const { return drag_.has_value(); }
  [[nodiscard]] bool point_construction_dragging() const {
    return drag_.has_value() && drag_->appended_point;
  }
  [[nodiscard]] bool can_apply() const;
  [[nodiscard]] bool closed() const;
  [[nodiscard]] std::size_t point_count() const;
  [[nodiscard]] bool owns_editing_entity(EntityId entity) const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const;
  [[nodiscard]] const CurveToolPreviewState &preview_state() const;

  CommandResult activate();
  CommandResult refresh();
  CommandResult cancel();
  CommandResult apply();
  CommandResult new_curve();
  CommandResult set_closed(bool closed);
  CommandResult toggle_point_smoothness(modeling::CurvePointId point);
  CommandResult set_hover(std::optional<QVec3> world_position);
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
  };

  [[nodiscard]] bool editable_source(EntityId entity) const;
  [[nodiscard]] std::optional<EntityId> selected_curve() const;
  [[nodiscard]] CurveControlSelection valid_control_selection(
      const CurveControlSelection &selection,
      const modeling::CurveDocument &document) const;
  bool set_control_selection(CurveControlSelection selection);
  void attach_source(EntityId entity);
  void detach_source();
  void invalidate(bool settings_changed = false);
  void observe_owner_revisions();

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  bool active_ = false;
  bool drawing_ = true;
  bool preferred_closed_ = false;
  EntityId entity_ = kInvalidEntityId;
  int modeling_object_id_ = -1;
  std::string source_uuid_;
  modeling::CurveDocument draft_;
  std::optional<Drag> drag_;
  CurveControlSelection control_selection_;
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
