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
#include "viewport/viewport_input_types.hpp"

#include <QWindow>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace quader {

enum class ViewportPaneFocusTrigger;

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
           input.middle_pressed || input.middle_released ||
           input.mouse_wheel != 0.0f || input.mouse_delta.x != 0.0f ||
           input.mouse_delta.y != 0.0f;
  }
};

/**
 * Owns one Qt-native child surface and the Filament swapchain that presents
 * into it. Qt owns window composition; Filament owns only the child surface.
 */
class QtViewportSurface final : public QWindow {
public:
  explicit QtViewportSurface(ViewportPaneId pane_id);
  ~QtViewportSurface() override;

  QtViewportSurface(const QtViewportSurface &) = delete;
  QtViewportSurface &operator=(const QtViewportSurface &) = delete;

  bool initialize(RenderSystem &render_system, std::uint8_t grid_layer_mask);
  void shutdown();

  [[nodiscard]] bool render_frame();
  [[nodiscard]] bool ready_to_render() const;
  [[nodiscard]] ViewportPaneId pane_id() const { return pane_id_; }
  [[nodiscard]] ViewportRenderer &renderer() { return renderer_; }
  [[nodiscard]] const ViewportRenderer &renderer() const { return renderer_; }
  [[nodiscard]] QVec2 render_size_pixels() const;

  /**
   * Consumes edge-triggered events accumulated since the previous app frame.
   * Held keys/buttons remain present in active_input_chords until released.
   */
  [[nodiscard]] QtViewportInputFrame
  take_input_frame(float delta_seconds, bool route_active_navigation);
  void set_pointer_capture(bool captured);
  [[nodiscard]] bool pointer_captured() const;

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
  void recreate_swap_chain();
  void resize_renderer_to_surface();
  [[nodiscard]] void *native_window_handle() const;
  void record_focus_in();
  void record_focus_out();
  void record_key_press(QKeyEvent &event);
  void record_key_release(QKeyEvent &event);
  void record_mouse_press(QMouseEvent &event, bool double_click);
  void record_mouse_release(QMouseEvent &event);
  void record_mouse_move(QMouseEvent &event);
  void record_wheel(QWheelEvent &event);
  void recenter_captured_pointer();

  struct InputAccumulator;

  ViewportPaneId pane_id_ = ViewportPaneId::Perspective;
  RenderSystem *render_system_ = nullptr;
  RenderAdditionalSwapChain swap_chain_;
  ViewportRenderer renderer_;
  std::unique_ptr<InputAccumulator> input_;
  std::function<void()> surface_changed_callback_;
  std::function<void(ViewportPaneId, ViewportPaneFocusTrigger)>
      focus_request_callback_;
  bool initialized_ = false;
  bool native_surface_available_ = false;
  std::uintptr_t swap_chain_native_id_ = 0u;
};

} // namespace quader
