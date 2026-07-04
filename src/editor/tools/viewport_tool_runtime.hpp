////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/viewport_tool.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"

#include <span>
#include <string_view>

namespace quader {

/**
 * Owns Viewport Tool Registry lookup and registration for editor-owned viewport tool interactions.
 */
class ViewportToolRegistry {
public:
    [[nodiscard]] std::span<const ViewportTool* const> tools() const;
    [[nodiscard]] const ViewportTool* find(ViewportToolId tool) const;
    [[nodiscard]] const ViewportTool* find(std::string_view id) const;
    [[nodiscard]] bool available(ViewportToolId tool) const;
};

/**
 * Owns Viewport Tool Host behavior within editor-owned viewport tool interactions.
 */
class ViewportToolHost {
public:
    [[nodiscard]] bool available(const ViewportTool& tool) const;
    [[nodiscard]] bool owns_viewport(const ViewportTool &tool) const;
    [[nodiscard]] bool
    blocks_viewport_focus_shortcuts(const ViewportTool &tool) const;
    [[nodiscard]] bool
    allows_selection_fallback(const ViewportTool &tool) const;
    ModelingCommandResult activate_tool(const ViewportTool &tool,
                                        ModelingSessionService &modeling) const;
};

/**
 * Builds Viewport Tool Event Builder data for editor-owned viewport tool interactions.
 */
class ViewportToolEventBuilder {
public:
  [[nodiscard]] ViewportToolEvent
  activation_event(ViewportToolId tool,
                   const ViewportShortcut *shortcut = nullptr) const;
};

/**
 * Coordinates Viewport Tool Controller state transitions for editor-owned viewport tool interactions.
 */
class ViewportToolController {
public:
  bool activate_tool(const ViewportToolRegistry &registry,
                     const ViewportToolHost &host, ViewportToolState &state,
                     ViewportToolId tool, const ViewportTool *&active_tool,
                     ViewportInputResult *result = nullptr) const;
};

/**
 * Coordinates Viewport Modal Tool Controller state transitions for editor-owned viewport tool interactions.
 */
class ViewportModalToolController {
public:
    [[nodiscard]] bool handles(const ViewportTool& tool) const;
    [[nodiscard]] bool owns_viewport(const ViewportToolHost &host,
                                     const ViewportTool &tool) const;
    [[nodiscard]] bool
    blocks_viewport_focus_shortcuts(const ViewportToolHost &host,
                                    const ViewportTool &tool) const;
};

/**
 * Coordinates Viewport Transform Tool Controller state transitions for editor-owned viewport tool interactions.
 */
class ViewportTransformToolController {
public:
    [[nodiscard]] bool handles(const ViewportTool& tool) const;
    [[nodiscard]] ModelingTransformTool
    transform_tool(const ViewportTool &tool) const;
};

/**
 * Owns Viewport Tool Runtime behavior within editor-owned viewport tool interactions.
 */
class ViewportToolRuntime {
public:
    ViewportToolRuntime();

    [[nodiscard]] const ViewportTool *active_tool() const {
      return active_tool_;
    }
    [[nodiscard]] const ViewportToolRegistry& registry() const { return registry_; }
    [[nodiscard]] bool active_tool_owns_viewport() const;
    [[nodiscard]] bool active_tool_blocks_viewport_focus_shortcuts() const;
    [[nodiscard]] bool tool_available(ViewportToolId tool) const;
    [[nodiscard]] bool
    tool_allows_selection_fallback(ViewportToolId tool) const;
    [[nodiscard]] ModelingTransformTool
    transform_tool(ViewportToolId tool) const;
    [[nodiscard]] bool
    has_active_modal_preview(const ViewportToolPreviewPort &preview,
                             bool snap_enabled = true) const;
    [[nodiscard]] ModelingBoxPreview
    box_preview(const ViewportToolPreviewPort &preview,
                bool snap_enabled = true) const;

    bool set_active_tool(ViewportToolState &state, ViewportToolId tool,
                         ViewportInputResult *result = nullptr);
    void sync_active_tool(ViewportToolId tool);
    void clear_modal_tool_state();
    void clear_modal_drag_state();
    bool handle_shortcut(const ViewportShortcut &shortcut,
                         ViewportToolState &state,
                         ViewportCameraController &camera,
                         ViewportInputResult &result);
    ViewportToolResult handle_active_tool_event(ViewportToolState &state,
                                                ViewportToolContext &context,
                                                const ViewportToolEvent &event);
    ModelingCommandResult
    activate_active_tool(ModelingSessionService &modeling) const;

  private:
    ViewportToolRegistry registry_;
    ViewportToolHost host_;
    ViewportToolEventBuilder event_builder_;
    ViewportToolController controller_;
    ViewportModalToolController modal_controller_;
    ViewportTransformToolController transform_controller_;
    ViewportModalToolState modal_state_;
    const ViewportTool *active_tool_ = nullptr;
};

} // namespace quader
