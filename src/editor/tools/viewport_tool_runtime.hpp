////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/viewport_tool.hpp"
#include "core/command_result.hpp"
#include "core/types.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/** Identifies the history domain mutated by a native viewport tool commit. */
enum class ViewportNativeToolCommitDomain {
  Modeling,
  Document,
};

/** Describes the history label and mutation domain of a native tool commit. */
struct ViewportNativeToolCommitMetadata final {
  std::string_view label;
  ViewportNativeToolCommitDomain domain =
      ViewportNativeToolCommitDomain::Modeling;
};

/**
 * Adapts one editor-owned native tool to the same lifecycle runtime used by
 * the canonical viewport tool catalog. Tool-specific services retain their
 * preview data and behavior; this contract owns the common activation,
 * cancellation, commit, mutation-blocking, and active-tool policy.
 */
struct ViewportNativeToolLifecycleAdapter final {
  ViewportToolId tool = ViewportToolId::Select;
  /// Active-tool presentation precedence can differ from transaction order
  /// while a failed cancellation leaves more than one service active.
  std::size_t active_priority = 0;
  std::string unavailable_message;
  std::string commit_label;
  ViewportNativeToolCommitDomain commit_domain =
      ViewportNativeToolCommitDomain::Modeling;
  std::function<bool()> can_activate;
  std::function<bool()> active;
  std::function<bool()> pending;
  std::function<bool()> pending_history_preview;
  std::function<bool()> blocks_modeling_content_mutation;
  std::function<bool(EntityId)> blocks_document_mutation;
  std::function<CommandResult(double)> activate;
  std::function<CommandResult()> commit;
  std::function<CommandResult()> cancel;
  std::function<void()> reset;
};

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
    [[nodiscard]] bool
    tool_uses_editor_native_lifecycle(ViewportToolId tool) const;
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

    /// Registers one catalog-declared native lifecycle in commit/cancel order;
    /// active_priority independently preserves presentation precedence.
    [[nodiscard]] bool register_native_lifecycle(
        ViewportNativeToolLifecycleAdapter adapter);
    [[nodiscard]] bool native_lifecycle_registered(ViewportToolId tool) const;
    [[nodiscard]] bool native_lifecycle_catalog_complete() const;
    [[nodiscard]] std::size_t native_lifecycle_count() const {
      return native_lifecycles_.size();
    }
    [[nodiscard]] bool handles_native_lifecycle(std::string_view tool_id) const;
    [[nodiscard]] bool native_lifecycle_active(std::string_view tool_id) const;
    [[nodiscard]] bool native_lifecycle_pending(std::string_view tool_id) const;
    [[nodiscard]] bool
    can_activate_native_lifecycle(std::string_view tool_id) const;
    [[nodiscard]] bool has_pending_native_lifecycle() const;
    [[nodiscard]] bool has_pending_native_history_preview() const;
    [[nodiscard]] bool native_lifecycle_blocks_modeling_content_mutation() const;
    [[nodiscard]] bool
    native_lifecycle_blocks_document_mutation(EntityId entity) const;
    [[nodiscard]] std::string_view active_native_lifecycle_id() const;
    [[nodiscard]] std::optional<ViewportNativeToolCommitMetadata>
    active_native_lifecycle_commit_metadata() const;
    [[nodiscard]] CommandResult activate_native_lifecycle(
        std::string_view tool_id, double grid_world_size,
        const std::function<CommandResult()> &cancel_competing_state);
    [[nodiscard]] CommandResult commit_active_native_lifecycle();
    [[nodiscard]] CommandResult cancel_native_lifecycles();
    void reset_native_lifecycles();

  private:
    [[nodiscard]] ViewportNativeToolLifecycleAdapter *
    find_native_lifecycle(std::string_view tool_id);
    [[nodiscard]] const ViewportNativeToolLifecycleAdapter *
    find_native_lifecycle(std::string_view tool_id) const;
    [[nodiscard]] const ViewportNativeToolLifecycleAdapter *
    find_native_lifecycle(ViewportToolId tool) const;

    ViewportToolRegistry registry_;
    ViewportToolHost host_;
    ViewportToolEventBuilder event_builder_;
    ViewportToolController controller_;
    ViewportModalToolController modal_controller_;
    ViewportTransformToolController transform_controller_;
    ViewportModalToolState modal_state_;
    const ViewportTool *active_tool_ = nullptr;
    std::vector<ViewportNativeToolLifecycleAdapter> native_lifecycles_;
};

} // namespace quader
