////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "viewport/curve_tool_preview.hpp"

#include <cstdint>
#include <optional>

namespace quader {

/** Summarizes the lifecycle facts that govern curve-control gizmo participation. */
struct ViewportCurveControlGizmoPolicy final {
  bool native_lifecycle_active = false;
  bool selection_fallback_allowed = false;
  bool active_tool_owns_viewport = false;
  bool modal_tool_active = false;
};

/**
 * Narrow editor-owned Curve control contract used by viewport interaction.
 * Concrete Curve lifecycle and storage remain private to the native tool
 * coordinator.
 */
class ViewportCurveInteractionPort {
public:
  virtual ~ViewportCurveInteractionPort() = default;

  [[nodiscard]] virtual bool active() const = 0;
  [[nodiscard]] virtual bool controls_editable() const = 0;
  [[nodiscard]] virtual bool dragging() const = 0;
  [[nodiscard]] virtual bool point_construction_dragging() const = 0;
  [[nodiscard]] virtual const CurveToolPreviewState &preview_state() const = 0;
  [[nodiscard]] virtual const CurveControlSelection &control_selection() const = 0;
  [[nodiscard]] virtual std::uint64_t control_selection_revision() const = 0;
  [[nodiscard]] virtual std::optional<QVec3>
  selected_controls_world_pivot() const = 0;

  [[nodiscard]] bool has_selected_controls() const {
    return controls_editable() && !control_selection().empty();
  }

  /**
   * Decides whether the Curve-control gizmo may start without exposing the
   * concrete Curve tool id to the interaction controller.
   */
  [[nodiscard]] bool allows_control_gizmo_start(
      const ViewportCurveControlGizmoPolicy &policy) const {
    return (policy.native_lifecycle_active && active()) ||
           (policy.selection_fallback_allowed &&
            !policy.active_tool_owns_viewport && !policy.modal_tool_active);
  }

  virtual CommandResult refresh() = 0;
  virtual CommandResult begin_selected_control_drag() = 0;
  virtual CommandResult update_drag(QVec3 world_position) = 0;
  virtual CommandResult commit_drag() = 0;
  virtual CommandResult cancel_drag() = 0;
};

} // namespace quader
