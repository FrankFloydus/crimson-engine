////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_surface.hpp"

#include "ui/viewport_host_interaction_policy.hpp"

#include "core/logger.hpp"
#include "ui/qt_input_adapter.hpp"
#include "viewport/viewport_layout.hpp"

#include <QCursor>
#include <QEvent>
#include <QExposeEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <vector>

namespace quader {
namespace {

ViewportModifiers viewport_modifiers(Qt::KeyboardModifiers modifiers) {
  return {
      .primary = modifiers.testFlag(Qt::ControlModifier) ||
                 modifiers.testFlag(Qt::MetaModifier),
      .shift = modifiers.testFlag(Qt::ShiftModifier),
      .alt = modifiers.testFlag(Qt::AltModifier),
  };
}

bool is_native_window_close_shortcut(const QKeyEvent &event) {
  return event.key() == Qt::Key_F4 &&
         event.modifiers().testFlag(Qt::AltModifier);
}

using ViewportSurfaceProbeClock = std::chrono::steady_clock;

/// Accumulates opt-in timings for direct native-surface render submission.
struct ViewportSurfaceProbe {
  ViewportSurfaceProbeClock::time_point window_start =
      ViewportSurfaceProbeClock::now();
  std::uint64_t frames = 0u;
  double begin_ms = 0.0;
  double render_ms = 0.0;
  double end_ms = 0.0;
};

ViewportSurfaceProbe g_viewport_surface_probe;

double probe_milliseconds(ViewportSurfaceProbeClock::time_point begin,
                          ViewportSurfaceProbeClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

bool viewport_surface_diagnostics_enabled() {
  static const bool kEnabled =
      std::getenv("QUADER_FRAME_DIAGNOSTICS") != nullptr;
  return kEnabled;
}

} // namespace

/// Collects transient Qt input state until the panel consumes one frame.
struct QtViewportSurface::InputAccumulator {
  bool hovered = false;
  bool focused = false;
  bool pointer_captured = false;
  bool suppress_warp_event = false;
  QPoint warp_target_local;
  QPoint capture_restore_global;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
  int left_click_count = 1;
  QVec2 left_click_local = {};
  bool middle_pressed = false;
  bool middle_down = false;
  bool middle_released = false;
  bool right_pressed = false;
  bool right_down = false;
  bool right_released = false;
  int right_click_count = 1;
  QVec2 right_click_local = {};
  float mouse_wheel = 0.0f;
  QVec2 mouse_screen = {};
  QVec2 mouse_local = {};
  QVec2 mouse_delta = {};
  bool has_mouse_position = false;
  std::uint64_t mouse_move_events = 0u;
  ViewportModifiers modifiers = {};
  std::vector<QuaderInputChord> pressed;
  std::vector<QuaderInputChord> released;
  std::vector<QuaderInputChord> wheel;
  std::unordered_map<int, QuaderInputChord> active_keys;
  std::optional<QuaderInputChord> middle_drag;
  std::optional<QuaderInputChord> right_drag;
};

QtViewportSurface::QtViewportSurface(ViewportPaneId pane_id)
    : pane_id_(pane_id), input_(std::make_unique<InputAccumulator>()) {
  setObjectName(QStringLiteral("viewportSurface_%1")
                    .arg(QString::fromUtf8(viewport_pane_stable_id(pane_id_)
                                               .data())));
  setSurfaceType(QSurface::RasterSurface);
}

QtViewportSurface::~QtViewportSurface() { shutdown(); }

bool QtViewportSurface::initialize(RenderSystem &render_system,
                                   std::uint8_t grid_layer_mask) {
  if (initialized_) {
    return true;
  }

  render_system_ = &render_system;
  render_system_->initialize_viewport_renderer(
      renderer_, grid_layer_mask, ViewportRenderSurface::Swapchain);
  initialized_ = true;

  // winId() deliberately creates a stable native child before Filament creates
  // its swapchain. The QWindow remains parented through createWindowContainer.
  (void)winId();
  native_surface_available_ = handle() != nullptr;
  if (!swap_chain_) {
    recreate_swap_chain();
  }
  resize_renderer_to_surface();
  return static_cast<bool>(swap_chain_);
}

void QtViewportSurface::shutdown() {
  if (!initialized_) {
    return;
  }

  set_pointer_capture(false);
  swap_chain_.reset();
  swap_chain_native_id_ = 0u;
  renderer_.shutdown();
  render_system_ = nullptr;
  initialized_ = false;
  native_surface_available_ = false;
}

bool QtViewportSurface::render_frame() {
  if (!ready_to_render()) {
    return false;
  }

  const bool diagnostics_enabled = viewport_surface_diagnostics_enabled();
  const ViewportSurfaceProbeClock::time_point begin_start =
      diagnostics_enabled ? ViewportSurfaceProbeClock::now()
                          : ViewportSurfaceProbeClock::time_point{};
  if (!render_system_->begin_frame(swap_chain_.get())) {
    return false;
  }
  const ViewportSurfaceProbeClock::time_point render_start =
      diagnostics_enabled ? ViewportSurfaceProbeClock::now()
                          : ViewportSurfaceProbeClock::time_point{};
  render_system_->render_viewport(renderer_);
  const ViewportSurfaceProbeClock::time_point end_start =
      diagnostics_enabled ? ViewportSurfaceProbeClock::now()
                          : ViewportSurfaceProbeClock::time_point{};
  render_system_->end_frame();
  if (diagnostics_enabled) {
    const ViewportSurfaceProbeClock::time_point now =
        ViewportSurfaceProbeClock::now();
    ++g_viewport_surface_probe.frames;
    g_viewport_surface_probe.begin_ms +=
        probe_milliseconds(begin_start, render_start);
    g_viewport_surface_probe.render_ms +=
        probe_milliseconds(render_start, end_start);
    g_viewport_surface_probe.end_ms += probe_milliseconds(end_start, now);
    const double window_seconds = std::chrono::duration<double>(
                                      now - g_viewport_surface_probe.window_start)
                                      .count();
    if (window_seconds >= 2.0 && g_viewport_surface_probe.frames > 0u) {
      const double frames =
          static_cast<double>(g_viewport_surface_probe.frames);
      LOG_INFO(
          "Qt viewport surface stages fps={:.1f} avg_begin_ms={:.3f} avg_render_ms={:.3f} avg_end_ms={:.3f}",
          frames / window_seconds, g_viewport_surface_probe.begin_ms / frames,
          g_viewport_surface_probe.render_ms / frames,
          g_viewport_surface_probe.end_ms / frames);
      g_viewport_surface_probe = ViewportSurfaceProbe{};
    }
  }
  return true;
}

bool QtViewportSurface::ready_to_render() const {
  const QVec2 size = render_size_pixels();
  return initialized_ && render_system_ != nullptr &&
         render_system_->initialized() && static_cast<bool>(swap_chain_) &&
         native_surface_available_ && isExposed() && isVisible() &&
         size.x >= 1.0f && size.y >= 1.0f;
}

QVec2 QtViewportSurface::render_size_pixels() const {
  const qreal scale = devicePixelRatio();
  return {
      static_cast<float>(std::max<qreal>(1.0, std::round(width() * scale))),
      static_cast<float>(std::max<qreal>(1.0, std::round(height() * scale))),
  };
}

QtViewportInputFrame
QtViewportSurface::take_input_frame(float delta_seconds,
                                    bool route_active_navigation) {
  QtViewportInputFrame frame;
  if (input_ == nullptr) {
    return frame;
  }

  input_->modifiers =
      viewport_modifiers(QGuiApplication::keyboardModifiers());
  frame.input.viewport_hovered = input_->hovered || route_active_navigation;
  frame.input.viewport_focused = input_->focused || route_active_navigation;
  frame.input.text_input_active = false;
  frame.input.middle_pressed =
      (input_->hovered || route_active_navigation) && input_->middle_pressed;
  frame.input.middle_down = input_->middle_down;
  frame.input.middle_released = input_->middle_released;
  frame.input.right_pressed =
      (input_->hovered || route_active_navigation) && input_->right_pressed;
  frame.input.right_down = input_->right_down;
  frame.input.right_released = input_->right_released;
  frame.input.mouse_wheel =
      (input_->hovered || route_active_navigation) ? input_->mouse_wheel : 0.0f;
  frame.input.delta_seconds = std::max(delta_seconds, 0.0f);
  frame.input.mouse_screen = input_->mouse_screen;
  frame.input.mouse_delta = input_->mouse_delta;
  frame.input.mouse_local = input_->mouse_local;
  frame.input.viewport_size = render_size_pixels();
  frame.input.modifiers = input_->modifiers;
  frame.input.pressed_input_chords = std::move(input_->pressed);
  frame.input.released_input_chords = std::move(input_->released);
  frame.input.wheel_input_chords = std::move(input_->wheel);
  frame.input.active_input_chords.reserve(input_->active_keys.size() + 2u);
  for (const auto &[key, chord] : input_->active_keys) {
    (void)key;
    frame.input.active_input_chords.push_back(chord);
  }
  if (input_->middle_down && input_->middle_drag.has_value()) {
    frame.input.active_input_chords.push_back(*input_->middle_drag);
  }
  if (input_->right_down && input_->right_drag.has_value()) {
    frame.input.active_input_chords.push_back(*input_->right_drag);
  }

  frame.left_pressed = input_->left_pressed;
  frame.left_down = input_->left_down;
  frame.left_released = input_->left_released;
  frame.left_click_count = input_->left_click_count;
  frame.left_click_local = input_->left_click_local;
  frame.right_pressed = input_->right_pressed;
  frame.right_down = input_->right_down;
  frame.right_released = input_->right_released;
  frame.right_click_count = input_->right_click_count;
  frame.right_click_local = input_->right_click_local;
  frame.mouse_move_events = input_->mouse_move_events;

  input_->left_pressed = false;
  input_->left_released = false;
  input_->middle_pressed = false;
  input_->middle_released = false;
  input_->right_pressed = false;
  input_->right_released = false;
  input_->mouse_wheel = 0.0f;
  input_->mouse_delta = {};
  input_->mouse_move_events = 0u;
  input_->pressed.clear();
  input_->released.clear();
  input_->wheel.clear();
  if (input_->pointer_captured) {
    recenter_captured_pointer();
  }
  return frame;
}

void QtViewportSurface::set_pointer_capture(bool captured) {
  if (input_ == nullptr || input_->pointer_captured == captured) {
    return;
  }
  input_->pointer_captured = captured;
  if (captured) {
    input_->capture_restore_global = QCursor::pos();
    setCursor(QCursor(Qt::BlankCursor));
    (void)setMouseGrabEnabled(true);
    recenter_captured_pointer();
  } else {
    (void)setMouseGrabEnabled(false);
    unsetCursor();
    const QPoint restore = input_->capture_restore_global;
    input_->warp_target_local = mapFromGlobal(restore);
    input_->suppress_warp_event = true;
    QCursor::setPos(restore);
    const QPoint local = mapFromGlobal(restore);
    input_->hovered =
        local.x() >= 0 && local.y() >= 0 && local.x() < width() &&
        local.y() < height();
  }
}

bool QtViewportSurface::pointer_captured() const {
  return input_ != nullptr && input_->pointer_captured;
}

bool QtViewportSurface::event(QEvent *event) {
  if (event != nullptr && event->type() == QEvent::PlatformSurface) {
    auto *surface_event = static_cast<QPlatformSurfaceEvent *>(event);
    if (surface_event->surfaceEventType() ==
        QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
      native_surface_available_ = false;
      swap_chain_.reset();
      swap_chain_native_id_ = 0u;
    } else if (surface_event->surfaceEventType() ==
               QPlatformSurfaceEvent::SurfaceCreated) {
      native_surface_available_ = true;
      recreate_swap_chain();
      resize_renderer_to_surface();
    }
  } else if (event != nullptr &&
             (event->type() == QEvent::DevicePixelRatioChange ||
              event->type() == QEvent::ScreenChangeInternal ||
              event->type() == QEvent::WinIdChange)) {
    if (event->type() == QEvent::WinIdChange) {
      recreate_swap_chain();
    }
    resize_renderer_to_surface();
  } else if (event != nullptr && event->type() == QEvent::Enter) {
    input_->hovered = true;
    if (focus_request_callback_) {
      focus_request_callback_(pane_id_, ViewportPaneFocusTrigger::Hover);
    }
  } else if (event != nullptr && event->type() == QEvent::Leave) {
    input_->hovered = input_->pointer_captured;
  }
  return QWindow::event(event);
}

bool QtViewportSurface::eventFilter(QObject *watched, QEvent *event) {
  if (watched != this && event != nullptr) {
    switch (event->type()) {
    case QEvent::FocusIn:
      record_focus_in();
      if (focus_request_callback_) {
        focus_request_callback_(pane_id_, ViewportPaneFocusTrigger::FocusIn);
      }
      break;
    case QEvent::FocusOut:
      record_focus_out();
      break;
    case QEvent::KeyPress:
      if (is_native_window_close_shortcut(
              *static_cast<QKeyEvent *>(event))) {
        break;
      }
      record_key_press(*static_cast<QKeyEvent *>(event));
      return true;
    case QEvent::KeyRelease:
      record_key_release(*static_cast<QKeyEvent *>(event));
      return true;
    case QEvent::ShortcutOverride: {
      auto &key = *static_cast<QKeyEvent *>(event);
      if (!is_native_window_close_shortcut(key) &&
          quader_key_code_from_qt_key(key.key(), key.modifiers()).has_value()) {
        key.accept();
        return true;
      }
      break;
    }
    default:
      break;
    }
  }
  return QWindow::eventFilter(watched, event);
}

void QtViewportSurface::exposeEvent(QExposeEvent *event) {
  QWindow::exposeEvent(event);
  resize_renderer_to_surface();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtViewportSurface::resizeEvent(QResizeEvent *event) {
  QWindow::resizeEvent(event);
  resize_renderer_to_surface();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtViewportSurface::focusInEvent(QFocusEvent *event) {
  record_focus_in();
  if (focus_request_callback_) {
    focus_request_callback_(pane_id_, ViewportPaneFocusTrigger::FocusIn);
  }
  QWindow::focusInEvent(event);
}

void QtViewportSurface::focusOutEvent(QFocusEvent *event) {
  record_focus_out();
  QWindow::focusOutEvent(event);
}

void QtViewportSurface::keyPressEvent(QKeyEvent *event) {
  if (event != nullptr) {
    if (is_native_window_close_shortcut(*event)) {
      QWindow::keyPressEvent(event);
      return;
    }
    record_key_press(*event);
    event->accept();
  }
}

void QtViewportSurface::keyReleaseEvent(QKeyEvent *event) {
  if (event != nullptr) {
    record_key_release(*event);
    event->accept();
  }
}

void QtViewportSurface::mousePressEvent(QMouseEvent *event) {
  if (event != nullptr) {
    if (focus_request_callback_) {
      focus_request_callback_(pane_id_,
                              ViewportPaneFocusTrigger::PointerPress);
    }
    requestActivate();
    record_mouse_press(*event, false);
    event->accept();
  }
}

void QtViewportSurface::mouseReleaseEvent(QMouseEvent *event) {
  if (event != nullptr) {
    record_mouse_release(*event);
    event->accept();
  }
}

void QtViewportSurface::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event != nullptr) {
    if (focus_request_callback_) {
      focus_request_callback_(pane_id_,
                              ViewportPaneFocusTrigger::PointerPress);
    }
    requestActivate();
    record_mouse_press(*event, true);
    event->accept();
  }
}

void QtViewportSurface::mouseMoveEvent(QMouseEvent *event) {
  if (event != nullptr) {
    record_mouse_move(*event);
    event->accept();
  }
}

void QtViewportSurface::wheelEvent(QWheelEvent *event) {
  if (event != nullptr) {
    if (focus_request_callback_) {
      focus_request_callback_(pane_id_, ViewportPaneFocusTrigger::Wheel);
    }
    record_wheel(*event);
    event->accept();
  }
}

void QtViewportSurface::record_focus_in() { input_->focused = true; }

void QtViewportSurface::record_focus_out() {
  input_->focused = false;
  for (const auto &[key, chord] : input_->active_keys) {
    (void)key;
    input_->released.push_back(chord);
  }
  input_->active_keys.clear();
  if (input_->middle_drag.has_value()) {
    input_->released.push_back(*input_->middle_drag);
  }
  if (input_->right_drag.has_value()) {
    input_->released.push_back(*input_->right_drag);
  }
  input_->middle_drag.reset();
  input_->right_drag.reset();
  input_->left_released = input_->left_down;
  input_->middle_released = input_->middle_down;
  input_->right_released = input_->right_down;
  input_->left_down = false;
  input_->middle_down = false;
  input_->right_down = false;
}

void QtViewportSurface::record_key_press(QKeyEvent &event) {
  if (event.isAutoRepeat()) {
    return;
  }
  const std::optional<QuaderInputChord> chord =
      quader_pressed_input_chord_from_qt_event(event);
  if (!chord.has_value()) {
    return;
  }
  input_->modifiers = viewport_modifiers(event.modifiers());
  input_->pressed.push_back(*chord);
  input_->active_keys.insert_or_assign(event.key(), *chord);
}

void QtViewportSurface::record_key_release(QKeyEvent &event) {
  if (event.isAutoRepeat()) {
    return;
  }
  input_->modifiers = viewport_modifiers(event.modifiers());
  const auto found = input_->active_keys.find(event.key());
  if (found != input_->active_keys.end()) {
    input_->released.push_back(found->second);
    input_->active_keys.erase(found);
    return;
  }
  const QuaderInputChord chord =
      quader_input_chord_from_qt_key(event.key(), event.modifiers());
  if (!chord.empty()) {
    input_->released.push_back(chord);
  }
}

void QtViewportSurface::record_mouse_press(QMouseEvent &event,
                                           bool double_click) {
  record_mouse_move(event);
  input_->hovered = true;
  input_->focused = true;
  input_->modifiers = viewport_modifiers(event.modifiers());
  const std::optional<QuaderMouseButton> button =
      quader_mouse_button_from_qt_button(event.button());
  if (!button.has_value()) {
    return;
  }

  if (double_click) {
    if (const std::optional<QuaderInputChord> chord =
            quader_mouse_input_chord_from_qt_event(
                event, QuaderInputGesture::DoubleClick)) {
      input_->pressed.push_back(*chord);
    }
  } else {
    if (const std::optional<QuaderInputChord> chord =
            quader_mouse_input_chord_from_qt_event(
                event, QuaderInputGesture::Press)) {
      input_->pressed.push_back(*chord);
    }
  }

  const std::optional<QuaderInputChord> drag =
      !double_click ? quader_mouse_input_chord_from_qt_event(
                          event, QuaderInputGesture::Drag)
                    : std::nullopt;
  switch (*button) {
  case QuaderMouseButton::Left:
    input_->left_pressed = true;
    input_->left_down = true;
    input_->left_click_count = double_click ? 2 : 1;
    input_->left_click_local = input_->mouse_local;
    break;
  case QuaderMouseButton::Middle:
    input_->middle_pressed = true;
    input_->middle_down = true;
    if (drag.has_value()) {
      input_->middle_drag = *drag;
      input_->pressed.push_back(*drag);
    }
    break;
  case QuaderMouseButton::Right:
    input_->right_pressed = true;
    input_->right_down = true;
    input_->right_click_count = double_click ? 2 : 1;
    input_->right_click_local = input_->mouse_local;
    if (drag.has_value()) {
      input_->right_drag = *drag;
      input_->pressed.push_back(*drag);
    }
    break;
  default:
    break;
  }
}

void QtViewportSurface::record_mouse_release(QMouseEvent &event) {
  record_mouse_move(event);
  input_->modifiers = viewport_modifiers(event.modifiers());
  const std::optional<QuaderMouseButton> button =
      quader_mouse_button_from_qt_button(event.button());
  if (!button.has_value()) {
    return;
  }
  switch (*button) {
  case QuaderMouseButton::Left:
    input_->left_down = false;
    input_->left_released = true;
    break;
  case QuaderMouseButton::Middle:
    input_->middle_down = false;
    input_->middle_released = true;
    if (input_->middle_drag.has_value()) {
      input_->released.push_back(*input_->middle_drag);
      input_->middle_drag.reset();
    }
    break;
  case QuaderMouseButton::Right:
    input_->right_down = false;
    input_->right_released = true;
    if (input_->right_drag.has_value()) {
      input_->released.push_back(*input_->right_drag);
      input_->right_drag.reset();
    }
    break;
  default:
    break;
  }
}

void QtViewportSurface::record_mouse_move(QMouseEvent &event) {
  if (viewport_surface_diagnostics_enabled()) {
    ++input_->mouse_move_events;
  }
  const qreal scale = devicePixelRatio();
  const QVec2 local{
      static_cast<float>(event.position().x() * scale),
      static_cast<float>(event.position().y() * scale),
  };
  const QVec2 screen{
      static_cast<float>(event.globalPosition().x() * scale),
      static_cast<float>(event.globalPosition().y() * scale),
  };
  if (input_->suppress_warp_event) {
    const QPoint event_local = event.position().toPoint();
    const QPoint delta = event_local - input_->warp_target_local;
    if (std::abs(delta.x()) <= 2 && std::abs(delta.y()) <= 2) {
      input_->mouse_local = local;
      input_->mouse_screen = screen;
      input_->has_mouse_position = true;
      input_->hovered = input_->pointer_captured ||
                        (event_local.x() >= 0 && event_local.y() >= 0 &&
                         event_local.x() < width() &&
                         event_local.y() < height());
      input_->modifiers = viewport_modifiers(event.modifiers());
      input_->suppress_warp_event = false;
      return;
    }
    input_->suppress_warp_event = false;
  }
  if (input_->has_mouse_position) {
    input_->mouse_delta = input_->mouse_delta + (local - input_->mouse_local);
  }
  input_->mouse_local = local;
  input_->mouse_screen = screen;
  input_->has_mouse_position = true;
  input_->hovered = true;
  input_->modifiers = viewport_modifiers(event.modifiers());
}

void QtViewportSurface::recenter_captured_pointer() {
  if (input_ == nullptr || !input_->pointer_captured || width() <= 0 ||
      height() <= 0) {
    return;
  }
  const QPoint local(width() / 2, height() / 2);
  const QPoint global = mapToGlobal(local);
  const qreal scale = devicePixelRatio();
  input_->mouse_local = {
      static_cast<float>(local.x() * scale),
      static_cast<float>(local.y() * scale),
  };
  input_->mouse_screen = {
      static_cast<float>(global.x() * scale),
      static_cast<float>(global.y() * scale),
  };
  input_->has_mouse_position = true;
  if (QCursor::pos() == global) {
    return;
  }
  input_->warp_target_local = local;
  input_->suppress_warp_event = true;
  QCursor::setPos(global);
}

void QtViewportSurface::record_wheel(QWheelEvent &event) {
  const qreal scale = devicePixelRatio();
  input_->mouse_local = {
      static_cast<float>(event.position().x() * scale),
      static_cast<float>(event.position().y() * scale),
  };
  input_->mouse_screen = {
      static_cast<float>(event.globalPosition().x() * scale),
      static_cast<float>(event.globalPosition().y() * scale),
  };
  input_->has_mouse_position = true;
  input_->hovered = true;
  input_->modifiers = viewport_modifiers(event.modifiers());
  const QtWheelInputTranslation translation =
      quader_wheel_input_from_qt_event(event);
  input_->mouse_wheel += translation.mouse_wheel;
  input_->wheel.insert(input_->wheel.end(),
                       translation.wheel_input_chords.begin(),
                       translation.wheel_input_chords.end());
}

void QtViewportSurface::recreate_swap_chain() {
  if (!initialized_ || render_system_ == nullptr ||
      !render_system_->initialized() || !native_surface_available_) {
    return;
  }

  void *native = native_window_handle();
  if (native == nullptr) {
    return;
  }
  const std::uintptr_t native_id = reinterpret_cast<std::uintptr_t>(native);
  if (swap_chain_ && swap_chain_native_id_ == native_id) {
    return;
  }
  swap_chain_.reset();
  swap_chain_native_id_ = 0u;
  swap_chain_ = render_system_->create_additional_swap_chain(
      native, SwapChainPresentationColorSpace::Srgb);
  if (!swap_chain_) {
    LOG_ERROR("Unable to create Filament swapchain for Qt viewport pane {}",
              viewport_pane_stable_id(pane_id_));
  } else {
    swap_chain_native_id_ = native_id;
  }
}

void QtViewportSurface::resize_renderer_to_surface() {
  if (!initialized_) {
    return;
  }
  const QVec2 size = render_size_pixels();
  renderer_.resize(static_cast<std::uint32_t>(std::max(1.0f, size.x)),
                   static_cast<std::uint32_t>(std::max(1.0f, size.y)));
}

void *QtViewportSurface::native_window_handle() const {
  const WId id = const_cast<QtViewportSurface *>(this)->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

} // namespace quader
