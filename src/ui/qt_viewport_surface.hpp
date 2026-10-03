////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"
#include "math/types.hpp"
#include "render/render_system.hpp"
#include "render/viewport_renderer.hpp"
#include "ui/qt_relative_pointer_input.hpp"
#include "viewport/viewport_input_types.hpp"

#include <QPointer>
#include <QWindow>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

class QEnterEvent;
class QWidget;

namespace quader {

struct RenderVulkanCompositionTarget;

enum class ViewportPaneFocusTrigger;

/** Resolved Qt presentation after hover and navigation-capture precedence. */
enum class QtViewportCursorPresentation {
  Default,
  SelectionDefault,
  SelectionAdd,
  SelectionSubtract,
  PrecisionCross,
  OpenHand,
  ClosedHand,
  Hidden,
};

[[nodiscard]] QtViewportCursorPresentation
qt_viewport_cursor_presentation(ViewportPointerCursor requested,
                                bool hovered, bool captured);

/**
 * One frame of toolkit input captured by a Qt viewport surface. Raw Qt events
 * are converted to Quader's canonical input vocabulary at this boundary so
 * the viewport router and editor interaction controllers remain toolkit
 * neutral.
 */
struct QtViewportInputFrame {
  ViewportInputFrame input;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
  int left_click_count = 1;
  QVec2 left_click_local = {};
  bool right_pressed = false;
  bool right_down = false;
  bool right_released = false;
  int right_click_count = 1;
  QVec2 right_click_local = {};
  std::uint64_t mouse_move_events = 0u;

  [[nodiscard]] bool pointer_activity() const {
    return left_pressed || left_released || right_pressed || right_released ||
           input.pointer_interaction_canceled ||
           !input.pointer_pressed_edges.empty() ||
           !input.pointer_released_edges.empty() ||
           !input.pointer_drag_started_edges.empty() ||
           !input.pointer_motion_samples.empty() ||
           input.middle_pressed || input.middle_released ||
           input.mouse_wheel != 0.0f || input.mouse_delta.x != 0.0f ||
           input.mouse_delta.y != 0.0f;
  }
};

/**
 * Owns one pane's semantic input and Filament presentation resources. Native
 * hosts use this QWindow; composed hosts retain it without creating a window.
 */
class QtViewportSurface final : public QWindow {
public:
  explicit QtViewportSurface(ViewportPaneId pane_id, bool composed = false);
  ~QtViewportSurface() override;

  QtViewportSurface(const QtViewportSurface &) = delete;
  QtViewportSurface &operator=(const QtViewportSurface &) = delete;

  bool initialize(RenderSystem &render_system, std::uint8_t grid_layer_mask);
  void shutdown();

  [[nodiscard]] bool render_frame();
  [[nodiscard]] bool
  render_composed_frame(const RenderVulkanCompositionTarget &target);
  void release_composed_surface();
  [[nodiscard]] bool ready_to_render() const;
  [[nodiscard]] ViewportPaneId pane_id() const { return pane_id_; }
  [[nodiscard]] ViewportRenderer &renderer() { return renderer_; }
  [[nodiscard]] const ViewportRenderer &renderer() const { return renderer_; }
  /// Uses the visible QWidget host for camera and input dimensions.
  void set_visible_host(QWidget &host);
  [[nodiscard]] QPoint visible_local_to_global(QPoint local) const;
  [[nodiscard]] qreal input_device_pixel_ratio() const;
  /// Increments only when changed geometry requires a native rectangle query.
  [[nodiscard]] std::uint64_t presentation_geometry_query_count() const;
  [[nodiscard]] QVec2 render_size_pixels() const;
  void synchronize_render_size();

  /**
   * Consumes edge-triggered events accumulated since the previous app frame.
   * Held keys/buttons remain present in active_input_chords until released.
   */
  [[nodiscard]] QtViewportInputFrame
  take_input_frame(float delta_seconds, bool route_active_navigation);
  // Discards queued and held input from a released runtime binding. Focus,
  // hover, and the last pointer position remain available to the next owner.
  void abandon_input();
  void set_pointer_capture(bool captured);
  [[nodiscard]] bool pointer_captured() const;
  void set_interaction_cursor(ViewportPointerCursor cursor);
  [[nodiscard]] ViewportPointerCursor interaction_cursor() const {
    return interaction_cursor_;
  }
  /// Increments only when Qt receives a materially different cursor update.
  [[nodiscard]] std::uint64_t pointer_cursor_presentation_revision() const {
    return pointer_cursor_presentation_revision_;
  }

  void set_surface_changed_callback(std::function<void()> callback) {
    surface_changed_callback_ = std::move(callback);
  }
  void set_focus_request_callback(
      std::function<void(ViewportPaneId, ViewportPaneFocusTrigger)> callback) {
    focus_request_callback_ = std::move(callback);
  }

protected:
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void exposeEvent(QExposeEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  void recreate_swap_chain(bool force_retry = false);
  void create_composed_surface();
  [[nodiscard]] void *native_window_handle() const;
  [[nodiscard]] QPoint input_local_from_global(QPoint global) const;
  [[nodiscard]] bool input_host_exposed() const;
  void record_focus_in();
  void record_focus_out();
  void record_key_press(QKeyEvent &event);
  void record_key_release(QKeyEvent &event);
  void record_mouse_press(QMouseEvent &event, bool double_click);
  void record_mouse_release(QMouseEvent &event);
  void record_mouse_move(QMouseEvent &event);
  void record_wheel(QWheelEvent &event);
  void rebase_pointer_after_scale_change();
  void record_pointer_enter(QEnterEvent &event);
  void record_native_pointer_capture_lost();
  [[nodiscard]] bool apply_native_mouse_grab(bool captured);
  void recenter_captured_pointer();
  void synchronize_interaction_cursor();
  [[nodiscard]] QPointF visible_local_position(QPointF surface_local) const;
  [[nodiscard]] QVec2 visible_position_pixels(QPointF surface_local) const;
  [[nodiscard]] bool visible_position_contains(QPointF surface_local) const;

  struct InputAccumulator;
  struct PresentationGeometry;
  [[nodiscard]] const PresentationGeometry &presentation_geometry() const;

  ViewportPaneId pane_id_ = ViewportPaneId::Perspective;
  RenderSystem *render_system_ = nullptr;
  RenderAdditionalSwapChain swap_chain_;
  ViewportRenderer renderer_;
  std::unique_ptr<InputAccumulator> input_;
  QPointer<QWidget> visible_host_;
  std::unique_ptr<PresentationGeometry> presentation_geometry_;
  std::function<void()> surface_changed_callback_;
  std::function<void(ViewportPaneId, ViewportPaneFocusTrigger)>
      focus_request_callback_;
  QtPointerCapturePolicy pointer_capture_policy_;
  QtPointerCaptureWarningGate pointer_capture_warning_gate_;
  QtRelativePointerInput relative_pointer_input_;
  bool raw_input_warning_issued_ = false;
  bool pointer_capture_transition_active_ = false;
  bool native_capture_loss_pending_ = false;
  std::uint64_t pointer_capture_generation_ = 0u;
  ViewportPointerCursor interaction_cursor_ = ViewportPointerCursor::Default;
  QtViewportCursorPresentation presented_cursor_ =
      QtViewportCursorPresentation::Default;
  qreal presented_cursor_device_pixel_ratio_ = 0.0;
  std::uint64_t pointer_cursor_presentation_revision_ = 0u;
  bool initialized_ = false;
  bool composed_ = false;
  bool native_surface_available_ = false;
  bool swap_chain_creation_failed_ = false;
  std::uintptr_t swap_chain_native_id_ = 0u;
};

} // namespace quader
