////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/tools/primitive_creation_interaction.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_pointer_gesture.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace quader {

class ModelingSessionService;

/**
 * Enumerates ViewportToolEventKind values used by editor-owned viewport tool interactions.
 */
enum class ViewportToolEventKind {
    Activation,
    Frame,
    Cancel,
    Wheel,
};

/**
 * Stores the Viewport Pointer Tool Event data contract used by editor-owned viewport tool interactions.
 */
struct ViewportPointerToolEvent {
    bool hovered = false;
    bool left_clicked = false;
    bool left_down = false;
    bool left_released = false;
    bool can_interact = false;
    bool finalize_pressed = false;
    bool mode_cycle_pressed = false;
    bool suppress_hover_work = false;
    bool gizmo_consumed = false;
    bool navigation_active = false;
    bool text_input_active = false;
    bool viewport_focused = true;
    bool pointer_interaction_canceled = false;
    QVec2 mouse_local = {};
    QVec2 mouse_screen = {};
    QVec2 left_click_local = {};
    QVec2 viewport_size = {};
    ViewportModifiers modifiers = {};
    std::span<const ViewportPointerButtonEdge> pointer_pressed_edges;
    std::span<const ViewportPointerButtonEdge> pointer_released_edges;
    float pointer_drag_threshold_pixels = 4.0f;
    float viewport_pixel_scale = 1.0f;
    double monotonic_time_seconds = 0.0;
    bool camera_changed = false;
    std::span<ViewportNavigationWheelEvent> wheel_events;
};

/**
 * Stores the Viewport Tool Event data contract used by editor-owned viewport tool interactions.
 */
struct ViewportToolEvent {
    ViewportToolEventKind kind = ViewportToolEventKind::Activation;
    ViewportToolId tool = ViewportToolId::Select;
    ViewportShortcut shortcut = {};
    bool from_shortcut = false;
    bool exit_active_modal_tool = false;
    ViewportPointerToolEvent pointer = {};
};

/**
 * Stores the Viewport Modal Tool State data contract used by editor-owned viewport tool interactions.
 */
struct ViewportModalToolState {
  bool insert_edge_loop_drag_active = false;
  bool cut_drag_active = false;
  bool cut_line_drag_active = false;
  bool mirror_drag_active = false;
  bool plane_endpoint_hovered = false;
  bool plane_body_hovered = false;
  bool stroke_close_hovered = false;
  std::optional<ViewportPointerGesture> primary_pointer_gesture;
  ViewportPointerReleaseSuppression primary_release_suppression;
  PrimitiveCreationInteraction primitive_creation;
  bool primitive_creation_drag_started = false;
};

/**
 * Stores the Viewport Tool Result data contract used by editor-owned viewport tool interactions.
 */
struct ViewportToolResult {
    bool handled = false;
    bool consumed = false;
    bool modal_state_changed = false;
    bool active_tool_changed = false;
    bool sync_active_tool_from_modeling = false;
    bool selection_mode_changed = false;
    bool settings_operation_started = false;
};

/**
 * Stores the Box Tool Context data contract used by box-shaped viewport tool interactions.
 */
struct ViewportBoxToolContext {
  AssetId default_material = kInvalidAssetId;
  std::function<ModelingBoxToolEvent(QVec2)> make_event_at;
  std::function<ModelingCommandResult(std::string, AssetId, bool)>
      accept_preview;
  std::function<ModelingCommandResult()> apply_session;
  std::function<ModelingCommandResult()> cancel_session;
  std::function<bool()> session_active;
  std::function<std::string()> next_mesh_name;
  std::function<std::optional<ViewportPrimitiveCreationState>()> live_primitive;
  std::function<ModelingCommandResult(ViewportPrimitiveResizeControl, QVec3)>
      begin_resize;
  std::function<ModelingCommandResult(double)> update_resize;
  std::function<ModelingCommandResult()> end_resize;
  std::function<ModelingCommandResult()> cancel_resize;
  std::function<ModelingCommandResult(int, int)>
      set_segments;
  float raster_scale = 1.0f;
  bool primitive_controls_visible = true;
};

/**
 * Stores the Point Tool Context data contract used by point/ray viewport tool interactions.
 */
struct ViewportPointToolContext {
  std::function<ModelingToolRayEvent(QVec2, bool)> make_event_at;
  std::function<std::optional<int>(const std::array<QVec3, 3> &, int)>
      hit_drawn_plane_captured_point;
  std::function<bool(const std::array<QVec3, 3> &, int, bool)>
      hit_drawn_plane_line;
};

/**
 * Stores the Viewport Tool Context data contract used by shared editor-owned viewport tool interactions.
 */
struct ViewportToolContext {
    ViewportToolState& state;
    ViewportCameraController& camera;
    ViewportToolPorts ports;
    SceneDocument* document = nullptr;
    const ViewportSettings* settings = nullptr;
    ViewportModalToolState *modal_state = nullptr;
    ViewportBoxToolContext* box = nullptr;
    ViewportPointToolContext* point = nullptr;

    std::function<void(ViewportToolId)> set_active_tool;
    ViewportRuntimeBindingToken runtime_binding;
};

/**
 * Implements Tool behavior for editor-owned viewport tool interactions.
 */
class Tool {
public:
    explicit Tool(ToolDescriptor descriptor);
    virtual ~Tool();

    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;
    Tool(Tool&&) = delete;
    Tool& operator=(Tool&&) = delete;

    [[nodiscard]] const ToolDescriptor& descriptor() const;
    [[nodiscard]] ViewportToolId viewport_tool() const;
    [[nodiscard]] ModelingViewportTool modeling_tool() const;
    [[nodiscard]] std::string_view id() const;
    [[nodiscard]] std::string_view label() const;
    [[nodiscard]] std::string_view shortcut() const;
    [[nodiscard]] ToolKind kind() const;
    [[nodiscard]] ToolMenuGroup menu_group() const;
    [[nodiscard]] virtual bool available() const;
    [[nodiscard]] virtual bool owns_viewport() const;
    [[nodiscard]] virtual bool blocks_viewport_focus_shortcuts() const;
    [[nodiscard]] virtual ModelingTransformTool transform_tool() const;
    [[nodiscard]] virtual bool supports_transform_gizmo() const;

    virtual ModelingCommandResult activate(ModelingSessionService& modeling) const;

private:
    ToolDescriptor descriptor_;
};

/**
 * Implements Viewport Tool behavior for editor-owned viewport tool interactions.
 */
class ViewportTool : public Tool {
public:
  static constexpr ViewportToolId kSelect = ViewportToolId::Select;
  static constexpr ViewportToolId kLassoSelect = ViewportToolId::LassoSelect;
  static constexpr ViewportToolId kPaintSelect = ViewportToolId::PaintSelect;
  static constexpr ViewportToolId kMove = ViewportToolId::Move;
  static constexpr ViewportToolId kExtrude = ViewportToolId::Extrude;
  static constexpr ViewportToolId kRotate = ViewportToolId::Rotate;
  static constexpr ViewportToolId kScale = ViewportToolId::Scale;
  static constexpr ViewportToolId kCreatePrimitive =
      ViewportToolId::CreatePrimitive;
  static constexpr ViewportToolId kInsertEdgeLoop =
      ViewportToolId::InsertEdgeLoop;
  static constexpr ViewportToolId kKnife = ViewportToolId::Knife;
  static constexpr ViewportToolId kCut = ViewportToolId::Cut;
  static constexpr ViewportToolId kPoly = ViewportToolId::Poly;
  static constexpr ViewportToolId kMirror = ViewportToolId::Mirror;
  static constexpr ViewportToolId kUvPeel = ViewportToolId::UvPeel;
  static constexpr ViewportToolId kPivot = ViewportToolId::Pivot;
  static constexpr ViewportToolId kLinearArray = ViewportToolId::LinearArray;
  static constexpr ViewportToolId kRadialArray = ViewportToolId::RadialArray;
  static constexpr ViewportToolId kPathArray = ViewportToolId::PathArray;
  static constexpr ViewportToolId kAlign = ViewportToolId::Align;

  explicit ViewportTool(ToolDescriptor descriptor);
  ViewportTool(ViewportToolId tool);

  [[nodiscard]] operator ViewportToolId() const;
  [[nodiscard]] virtual bool allows_selection_fallback() const;
  [[nodiscard]] virtual ViewportPointerCursor pointer_cursor() const;
  /// Opts into resolved wheel packets before camera navigation consumes them.
  [[nodiscard]] virtual bool handles_wheel_events() const { return false; }
  [[nodiscard]] virtual ViewportToolResult
  handle_event(ViewportToolContext &context,
               const ViewportToolEvent &event) const;

protected:
  [[nodiscard]] ViewportToolResult activate_by_id(ViewportToolContext &context,
                                                  ViewportToolId tool) const;
};

} // namespace quader
