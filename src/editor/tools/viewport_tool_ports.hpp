////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_input_types.hpp"

#include <functional>
#include <string_view>

namespace quader {

class ModelingSessionService;

/**
 * Narrow hover port used by viewport tools without exposing the full modeling session facade.
 */
class ViewportToolHoverPort {
public:
  virtual ~ViewportToolHoverPort() = default;

  [[nodiscard]] virtual bool claim(ModelingBoxDragState drag_state) = 0;
  virtual void invalidate() = 0;
  [[nodiscard]] virtual ModelingBoxPreview
  update_box_hover(const ModelingBoxToolEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  update_insert_edge_loop_hover(const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  update_knife_hover(const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  update_poly_hover(const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  update_drawn_plane_hover(ModelingViewportTool tool,
                           const ModelingToolRayEvent &event) = 0;
};

/**
 * Narrow preview port used by modal viewport tools for transient runtime state.
 */
class ViewportToolPreviewPort {
public:
  virtual ~ViewportToolPreviewPort() = default;

  [[nodiscard]] virtual ModelingToolStatus tool_status() const = 0;
  [[nodiscard]] virtual ModelingBoxPreview
  box_preview(bool snap_enabled = true) const = 0;
  [[nodiscard]] virtual ModelingBoxPreview
  begin_box_footprint(const ModelingBoxToolEvent &event) = 0;
  [[nodiscard]] virtual ModelingBoxPreview
  update_box_footprint(const ModelingBoxToolEvent &event) = 0;
  [[nodiscard]] virtual ModelingBoxPreview
  begin_box_height(const ModelingBoxToolEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  begin_insert_edge_loop_drag(const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  update_insert_edge_loop_drag(const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  accept_knife_point(const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult accept_poly_point() = 0;
  [[nodiscard]] virtual ModelingCommandResult
  begin_drawn_plane_point_drag(ModelingViewportTool tool, int point_index) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  begin_drawn_plane_line_drag(ModelingViewportTool tool,
                              const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  click_drawn_plane_point(ModelingViewportTool tool,
                          const ModelingToolRayEvent &event) = 0;
  [[nodiscard]] virtual ModelingCommandResult
  end_drawn_plane_drag(ModelingViewportTool tool) = 0;
  [[nodiscard]] virtual ModelingCommandResult cycle_cut_mode() = 0;
};

/**
 * Narrow commit port used by viewport tools when transient runtime previews become commands.
 */
class ViewportToolCommitPort {
public:
  virtual ~ViewportToolCommitPort() = default;

  [[nodiscard]] virtual ModelingCommandResult
  commit_insert_edge_loop_drag() = 0;
  [[nodiscard]] virtual ModelingCommandResult commit_knife_tool() = 0;
  [[nodiscard]] virtual ModelingCommandResult commit_poly_tool() = 0;
  [[nodiscard]] virtual ModelingCommandResult
  finalize_drawn_plane_tool(ModelingViewportTool tool) = 0;
};

/**
 * Narrow cancel port used by viewport tools to unwind active modal runtime state.
 */
class ViewportToolCancelPort {
public:
  virtual ~ViewportToolCancelPort() = default;

  [[nodiscard]] virtual ModelingCommandResult cancel_box() = 0;
  [[nodiscard]] virtual ModelingCommandResult cancel_active_modal_tool() = 0;
};

/**
 * Narrow selection-mode port used to synchronize viewport and app selection modes.
 */
class ViewportToolSelectionModePort {
public:
  virtual ~ViewportToolSelectionModePort() = default;

  [[nodiscard]] virtual ModelingSelectionSummary selection_summary() const = 0;
  virtual void set_viewport_selection_mode(ViewportSelectionMode mode) = 0;
  virtual void set_app_selection_mode(ModelingSelectionMode mode) = 0;
};

/**
 * Narrow result port used by viewport tools to report command outcomes and dirty overlays.
 */
class ViewportToolCommandResultPort {
public:
  virtual ~ViewportToolCommandResultPort() = default;

  virtual void handle_tool_command_result(std::string_view label,
                                          const ModelingCommandResult &result) = 0;
  virtual void mark_overlay_dirty() = 0;
};

/**
 * Bundles narrow viewport tool ports without exposing the broad modeling session facade.
 */
struct ViewportToolPorts {
  ViewportToolHoverPort *hover = nullptr;
  ViewportToolPreviewPort *preview = nullptr;
  ViewportToolCommitPort *commit = nullptr;
  ViewportToolCancelPort *cancel = nullptr;
  ViewportToolSelectionModePort *selection_mode = nullptr;
  ViewportToolCommandResultPort *command_result = nullptr;
};

/**
 * Optional callbacks supplied by viewport/UI owners while the modeling adapter preserves behavior.
 */
struct ViewportModelingToolPortCallbacks {
  std::function<bool(ModelingBoxDragState)> claim_hover;
  std::function<void()> invalidate_hover;
  std::function<void()> mark_overlay_dirty;
  std::function<void(ViewportSelectionMode)> set_selection_mode;
  std::function<void(ModelingSelectionMode)> set_app_selection_mode;
  std::function<void(std::string_view, const ModelingCommandResult &)>
      handle_tool_command_result;
};

/**
 * Adapts the current modeling session facade to the narrow viewport tool ports.
 */
class ViewportModelingToolPortAdapter final
    : public ViewportToolHoverPort,
      public ViewportToolPreviewPort,
      public ViewportToolCommitPort,
      public ViewportToolCancelPort,
      public ViewportToolSelectionModePort,
      public ViewportToolCommandResultPort {
public:
  explicit ViewportModelingToolPortAdapter(
      ModelingSessionService &modeling,
      ViewportModelingToolPortCallbacks callbacks = {});

  [[nodiscard]] ViewportToolPorts ports();

  [[nodiscard]] bool claim(ModelingBoxDragState drag_state) override;
  void invalidate() override;
  [[nodiscard]] ModelingBoxPreview
  update_box_hover(const ModelingBoxToolEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  update_insert_edge_loop_hover(const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  update_knife_hover(const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  update_poly_hover(const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  update_drawn_plane_hover(ModelingViewportTool tool,
                           const ModelingToolRayEvent &event) override;

  [[nodiscard]] ModelingToolStatus tool_status() const override;
  [[nodiscard]] ModelingBoxPreview
  box_preview(bool snap_enabled = true) const override;
  [[nodiscard]] ModelingBoxPreview
  begin_box_footprint(const ModelingBoxToolEvent &event) override;
  [[nodiscard]] ModelingBoxPreview
  update_box_footprint(const ModelingBoxToolEvent &event) override;
  [[nodiscard]] ModelingBoxPreview
  begin_box_height(const ModelingBoxToolEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  begin_insert_edge_loop_drag(const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  update_insert_edge_loop_drag(const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  accept_knife_point(const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult accept_poly_point() override;
  [[nodiscard]] ModelingCommandResult
  begin_drawn_plane_point_drag(ModelingViewportTool tool,
                               int point_index) override;
  [[nodiscard]] ModelingCommandResult
  begin_drawn_plane_line_drag(ModelingViewportTool tool,
                              const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  click_drawn_plane_point(ModelingViewportTool tool,
                          const ModelingToolRayEvent &event) override;
  [[nodiscard]] ModelingCommandResult
  end_drawn_plane_drag(ModelingViewportTool tool) override;
  [[nodiscard]] ModelingCommandResult cycle_cut_mode() override;

  [[nodiscard]] ModelingCommandResult
  commit_insert_edge_loop_drag() override;
  [[nodiscard]] ModelingCommandResult commit_knife_tool() override;
  [[nodiscard]] ModelingCommandResult commit_poly_tool() override;
  [[nodiscard]] ModelingCommandResult
  finalize_drawn_plane_tool(ModelingViewportTool tool) override;

  [[nodiscard]] ModelingCommandResult cancel_box() override;
  [[nodiscard]] ModelingCommandResult cancel_active_modal_tool() override;

  [[nodiscard]] ModelingSelectionSummary selection_summary() const override;
  void set_viewport_selection_mode(ViewportSelectionMode mode) override;
  void set_app_selection_mode(ModelingSelectionMode mode) override;

  void handle_tool_command_result(std::string_view label,
                                  const ModelingCommandResult &result) override;
  void mark_overlay_dirty() override;

private:
  ModelingSessionService &modeling_;
  ViewportModelingToolPortCallbacks callbacks_;
};

} // namespace quader
