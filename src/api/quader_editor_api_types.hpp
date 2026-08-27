////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_viewport_layout_types.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Enumerates QuaderShadingMode values used by the api layer.
 */
enum class QuaderShadingMode {
    Material,
    Solid,
    Wireframe,
    XRay,
    DiffuseOnly,
    SpecularOnly,
    Albedo,
    Roughness,
    Metalness,
    Normal,
};

/**
 * Enumerates QuaderNotificationLevel values used by the api layer.
 */
enum class QuaderNotificationLevel {
    Info,
    Warning,
    Error,
};

/**
 * Represents a Quader Project Metadata value used by the public Quader API surface.
 */
struct QuaderProjectMetadata {
    std::uint64_t id = 0;
    std::string uuid;
    std::string name;
};

/// Describes one recent project entry for the Home surface.
struct QuaderRecentProject final {
  std::string uuid;
  std::string name;
  std::filesystem::path manifest_path;
  bool available = false;
  std::optional<std::uint64_t> manifest_last_modified_unix_ms;
  std::optional<std::uintmax_t> manifest_size_bytes;
};

/**
 * Stores the OBJ export result returned by the project API.
 */
struct QuaderObjExportResult {
  std::vector<std::filesystem::path> paths;
};

/**
 * Selects how the native shell resolves dirty content before replacing the
 * current project document.
 */
enum class QuaderDirtyProjectDecision {
  Save,
  Discard,
  Cancel,
};

/**
 * Represents Quader Project Runtime Hooks value used by the native shell.
 */
struct QuaderProjectRuntimeHooks {
  std::function<std::optional<std::filesystem::path>()> choose_create_root;
  std::function<std::optional<std::filesystem::path>()>
      choose_open_manifest_path;
  std::function<std::optional<std::filesystem::path>()> choose_open_path;
  std::function<std::optional<std::filesystem::path>()> choose_export_obj_path;
  std::function<QuaderDirtyProjectDecision(std::string_view action_id)>
      confirm_dirty_project_replacement;
  /// Resolves a retained material-editor draft before project replacement.
  /// Returning false cancels the project command without discarding content.
  std::function<bool(std::string_view action_id)>
      prepare_material_document_close;
};

/**
 * Stores the Quader Command Descriptor data contract used by the public Quader API surface.
 */
struct QuaderCommandDescriptor {
    std::string id;
    std::string label;
    std::string shortcut;
    bool enabled = true;
};

/**
 * Enumerates Quader Tool Setting Control Kind values used by the public tools API.
 */
enum class QuaderToolSettingControlKind {
  Toggle,
  Number,
  Choice,
  Action,
};

/**
 * Represents a selectable value for a tool setting choice control.
 */
struct QuaderToolSettingChoiceOption {
  std::string id;
  std::string label;
  double value = 0.0;
};

/**
 * Represents one editable or executable tool setting control.
 */
struct QuaderToolSettingControl {
  QuaderToolSettingControlKind kind = QuaderToolSettingControlKind::Toggle;
  std::string id;
  std::string label;
  bool bool_value = false;
  double number_value = 0.0;
  double min_value = 0.0;
  double max_value = 1.0;
  double step = 1.0;
  std::string choice_value;
  std::vector<QuaderToolSettingChoiceOption> choices;
};

/**
 * Represents the active tool settings target exposed through the public tools API.
 */
struct QuaderToolSettingsTarget {
  bool active = false;
  bool preview_active = false;
  std::string tool_id;
  std::string title;
  std::string description;
  std::vector<QuaderToolSettingControl> controls;
  bool can_reset = false;
  bool apply_visible = false;
  bool can_apply = false;
  bool can_cancel = false;
};

/**
 * Represents a Quader Notification value used by the public Quader API surface.
 */
struct QuaderNotification {
    QuaderNotificationLevel level = QuaderNotificationLevel::Info;
    std::string message;
};

/**
 * Represents a Quader Viewport Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderViewportRuntimeHooks {
  std::function<QuaderApiVoidResult(ViewportPaneId)> frame_all;
  std::function<QuaderApiVoidResult(ViewportPaneId)> frame_selection;
  std::function<QuaderApiVoidResult(ViewportPaneId)> focus_origin;
  std::function<QuaderApiVoidResult()> spawn_window;
  std::function<QuaderApiVoidResult(ViewportPaneId,
                                    const std::filesystem::path &)>
      capture_screenshot;
};

/**
 * Represents a Quader Tools Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderToolsRuntimeHooks {
    std::function<QuaderApiVoidResult(std::string_view)> activate;
    std::function<QuaderApiVoidResult()> cancel_modal_tool;
    std::function<QuaderApiVoidResult()> commit_active_tool_preview;
    std::function<QuaderApiVoidResult()> cancel_active_tool_preview;
    std::function<double()> grid_world_size;
};

/**
 * Represents a Quader Preferences Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderPreferencesRuntimeHooks {
    std::function<std::string()> locale;
    std::function<QuaderApiVoidResult(std::string)> set_locale;
    std::function<QuaderApiVoidResult()> save;
    std::function<bool()> snapping_enabled;
    std::function<QuaderApiVoidResult(bool)> set_snapping_enabled;
    std::function<bool()> backface_selection_enabled;
    std::function<QuaderApiVoidResult(bool)> set_backface_selection_enabled;
    std::function<bool()> grid_visible;
    std::function<QuaderApiVoidResult(bool)> set_grid_visible;
    std::function<bool()> gizmos_visible;
    std::function<QuaderApiVoidResult(bool)> set_gizmos_visible;
    std::function<bool()> mesh_grid_visible;
    std::function<QuaderApiVoidResult(bool)> set_mesh_grid_visible;
    std::function<bool()> overlays_visible;
    std::function<QuaderApiVoidResult(bool)> set_overlays_visible;
    std::function<bool()> texture_lock_enabled;
    std::function<QuaderApiVoidResult(bool)> set_texture_lock_enabled;
    std::function<bool()> texture_lock_scale_enabled;
    std::function<QuaderApiVoidResult(bool)> set_texture_lock_scale_enabled;
    std::function<bool()> grid_snap_enabled;
    std::function<QuaderApiVoidResult(bool)> set_grid_snap_enabled;
    std::function<bool()> vertex_snap_enabled;
    std::function<QuaderApiVoidResult(bool)> set_vertex_snap_enabled;
    std::function<bool()> rotation_snap_enabled;
    std::function<QuaderApiVoidResult(bool)> set_rotation_snap_enabled;
    std::function<double()> rotation_snap_degrees;
    std::function<QuaderApiVoidResult(double)> set_rotation_snap_degrees;
    std::function<bool()> navigation_gizmo_visible;
    std::function<QuaderApiVoidResult(bool)> set_navigation_gizmo_visible;
};

/**
 * Represents a Quader UI Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderUIRuntimeHooks {
  std::function<bool(std::string_view)> is_panel_visible;
  std::function<QuaderApiVoidResult(std::string_view, bool)> set_panel_visible;
  std::function<QuaderApiVoidResult(std::string_view)> focus_panel;
  std::function<QuaderApiVoidResult(QuaderNotification)> notify;
};

} // namespace quader
