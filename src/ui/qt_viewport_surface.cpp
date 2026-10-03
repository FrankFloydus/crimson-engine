////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_surface.hpp"

#include "viewport/viewport_host_interaction_policy.hpp"

#include "core/logger.hpp"
#include "platform/window_placement.hpp"
#include "ui/qt_application_cursor.hpp"
#include "ui/qt_input_adapter.hpp"
#include "viewport/viewport_layout.hpp"

#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QExposeEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QPointF>
#include <QResizeEvent>
#include <QScreen>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

ViewportModifiers viewport_modifiers(const QKeyEvent &event) {
  Qt::KeyboardModifiers modifiers = event.modifiers();
  const bool pressed = event.type() == QEvent::KeyPress;
  switch (event.key()) {
  case Qt::Key_Control: modifiers.setFlag(Qt::ControlModifier, pressed); break;
  case Qt::Key_Meta: modifiers.setFlag(Qt::MetaModifier, pressed); break;
  case Qt::Key_Shift: modifiers.setFlag(Qt::ShiftModifier, pressed); break;
  case Qt::Key_Alt: modifiers.setFlag(Qt::AltModifier, pressed); break;
  default: break;
  }
  return viewport_modifiers(modifiers);
}

bool is_native_window_close_shortcut(const QKeyEvent &event) {
  return event.key() == Qt::Key_F4 &&
         event.modifiers().testFlag(Qt::AltModifier);
}

bool accept_viewport_shortcut_override(QEvent *event) {
  if (event == nullptr || event->type() != QEvent::ShortcutOverride) {
    return false;
  }
  auto &key = *static_cast<QKeyEvent *>(event);
  if (is_native_window_close_shortcut(key) ||
      !quader_key_code_from_qt_key(key.key(), key.modifiers()).has_value()) {
    return false;
  }
  // Native QWindow focus and its QWidget container must reserve the same
  // physical keys before Qt menu mnemonics can bypass the canonical resolver.
  key.accept();
  return true;
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

QtApplicationCursorVariant
application_cursor_variant(QtViewportCursorPresentation presentation) {
  switch (presentation) {
  case QtViewportCursorPresentation::SelectionAdd:
    return QtApplicationCursorVariant::SelectionAdd;
  case QtViewportCursorPresentation::SelectionSubtract:
    return QtApplicationCursorVariant::SelectionSubtract;
  case QtViewportCursorPresentation::SelectionDefault:
  default:
    return QtApplicationCursorVariant::Default;
  }
}

} // namespace

QtViewportCursorPresentation
qt_viewport_cursor_presentation(ViewportPointerCursor requested,
                                bool hovered, bool captured) {
  if (captured) {
    return QtViewportCursorPresentation::Hidden;
  }
  if (!hovered) {
    return QtViewportCursorPresentation::Default;
  }
  switch (requested) {
  case ViewportPointerCursor::SelectionDefault:
    return QtViewportCursorPresentation::SelectionDefault;
  case ViewportPointerCursor::SelectionAdd:
    return QtViewportCursorPresentation::SelectionAdd;
  case ViewportPointerCursor::SelectionSubtract:
    return QtViewportCursorPresentation::SelectionSubtract;
  case ViewportPointerCursor::PrecisionCross:
    return QtViewportCursorPresentation::PrecisionCross;
  case ViewportPointerCursor::OpenHand:
    return QtViewportCursorPresentation::OpenHand;
  case ViewportPointerCursor::ClosedHand:
    return QtViewportCursorPresentation::ClosedHand;
  case ViewportPointerCursor::Default:
    return QtViewportCursorPresentation::Default;
  }
  return QtViewportCursorPresentation::Default;
}

/// Collects transient Qt input state until the panel consumes one frame.
struct QtViewportSurface::InputAccumulator {
  /// Retains one physical press until release proves it was a click rather
  /// than pointer motion. The chord deliberately keeps press-time modifiers.
  struct ClickCandidate {
    QuaderInputChord chord;
    QPointF press_local;
    QPointF last_local;
    qreal captured_distance = 0.0;
    bool eligible = true;
  };

  bool hovered = false;
  bool focused = false;
  bool pointer_interaction_canceled = false;
  bool suppress_warp_event = false;
  bool warp_motion_overtook_target = false;
  QPoint warp_target_global;
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
  std::uint64_t pointer_event_sequence = 0u;
  std::uint64_t left_pointer_press_sequence = 0u;
  ViewportModifiers modifiers = {};
  bool has_event_modifiers = false;
  std::vector<QuaderInputChord> pressed;
  std::vector<QuaderInputChord> released;
  std::vector<ViewportPointerButtonEdge> pointer_pressed_edges;
  std::vector<ViewportPointerButtonEdge> pointer_released_edges;
  std::vector<ViewportPointerButtonEdge> pointer_drag_started_edges;
  std::vector<ViewportPointerMotionSample> pointer_motion_samples;
  std::vector<QuaderWheelInputEdge> wheel;
  QtWheelInputAccumulator wheel_accumulator;
  std::unordered_map<int, QuaderInputChord> active_keys;
  std::unordered_set<int> abandoned_key_releases;
  std::unordered_set<int> abandoned_pointer_releases;
  std::optional<QuaderInputChord> middle_drag;
  std::optional<QuaderInputChord> right_drag;
  std::unordered_map<int, ClickCandidate> click_candidates;
};

/// Caches native pixel mapping until its Qt geometry and DPI inputs change.
struct QtViewportSurface::PresentationGeometry {
  QRect surface_geometry;
  QRect parent_geometry;
  QRect host_geometry;
  QSize visible_size;
  const QWindow *surface_parent = nullptr;
  qreal device_pixel_ratio = 0.0;
  WId host_id = 0u;
  WId surface_id = 0u;
  QRect visible_pixel_bounds;
  QSize backing_pixel_size;
  std::uint64_t native_query_count = 0u;
  bool valid = false;
};

void QtViewportSurface::synchronize_interaction_cursor() {
  const QtViewportCursorPresentation next = qt_viewport_cursor_presentation(
      interaction_cursor_, input_ != nullptr && input_->hovered,
      pointer_captured());
  const bool custom_selection_cursor =
      next == QtViewportCursorPresentation::SelectionDefault ||
      next == QtViewportCursorPresentation::SelectionAdd ||
      next == QtViewportCursorPresentation::SelectionSubtract;
  const qreal next_device_pixel_ratio =
      custom_selection_cursor ? std::max<qreal>(1.0, input_device_pixel_ratio()) : 0.0;
  if (presented_cursor_ == next &&
      qFuzzyCompare(presented_cursor_device_pixel_ratio_ + 1.0,
                    next_device_pixel_ratio + 1.0)) {
    return;
  }

  const auto apply_cursor = [this](const QCursor &cursor) {
    if (composed_ && visible_host_ != nullptr) {
      visible_host_->setCursor(cursor);
    } else {
      setCursor(cursor);
    }
  };
  switch (next) {
  case QtViewportCursorPresentation::Default:
    if (composed_ && visible_host_ != nullptr) {
      visible_host_->unsetCursor();
    } else {
      unsetCursor();
    }
    break;
  case QtViewportCursorPresentation::SelectionDefault:
  case QtViewportCursorPresentation::SelectionAdd:
  case QtViewportCursorPresentation::SelectionSubtract:
    apply_cursor(qt_application_cursor(application_cursor_variant(next),
                                    next_device_pixel_ratio));
    break;
  case QtViewportCursorPresentation::PrecisionCross:
    apply_cursor(QCursor(Qt::CrossCursor));
    break;
  case QtViewportCursorPresentation::OpenHand:
    apply_cursor(QCursor(Qt::OpenHandCursor));
    break;
  case QtViewportCursorPresentation::ClosedHand:
    apply_cursor(QCursor(Qt::ClosedHandCursor));
    break;
  case QtViewportCursorPresentation::Hidden:
    apply_cursor(QCursor(Qt::BlankCursor));
    break;
  }
  presented_cursor_ = next;
  presented_cursor_device_pixel_ratio_ = next_device_pixel_ratio;
  ++pointer_cursor_presentation_revision_;
}

void QtViewportSurface::set_interaction_cursor(
    ViewportPointerCursor cursor) {
  interaction_cursor_ = cursor;
  synchronize_interaction_cursor();
}

QtViewportSurface::QtViewportSurface(ViewportPaneId pane_id, bool composed)
    : pane_id_(pane_id), input_(std::make_unique<InputAccumulator>()),
      presentation_geometry_(std::make_unique<PresentationGeometry>()),
      composed_(composed) {
  setObjectName(QStringLiteral("viewportSurface_%1")
                    .arg(QString::fromUtf8(viewport_pane_stable_id(pane_id_)
                                               .data())));
  setSurfaceType(QSurface::RasterSurface);
  relative_pointer_input_.set_capture_lost_callback([this]() {
    native_capture_loss_pending_ = true;
    const std::uint64_t capture_generation = pointer_capture_generation_;
    QMetaObject::invokeMethod(
        this,
        [this, capture_generation]() {
          if (pointer_capture_generation_ == capture_generation) {
            record_native_pointer_capture_lost();
          }
        },
        Qt::QueuedConnection);
  });
}

QtViewportSurface::~QtViewportSurface() { shutdown(); }

bool QtViewportSurface::initialize(RenderSystem &render_system,
                                   std::uint8_t grid_layer_mask) {
  if (!render_system.initialized()) {
    return false;
  }
  if (composed_ && !render_system.uses_composited_vulkan()) {
    return false;
  }
  if (initialized_) {
    if (composed_) {
      create_composed_surface();
      synchronize_render_size();
      return static_cast<bool>(swap_chain_);
    }
    native_surface_available_ = handle() != nullptr;
    if (!swap_chain_) {
      recreate_swap_chain(true);
    }
    synchronize_render_size();
    return static_cast<bool>(swap_chain_);
  }

  render_system_ = &render_system;
  render_system_->initialize_viewport_renderer(
      renderer_, grid_layer_mask, ViewportRenderSurface::SrgbSwapchain);
  initialized_ = true;

  if (composed_) {
    create_composed_surface();
    synchronize_render_size();
    return static_cast<bool>(swap_chain_);
  }

  // winId() deliberately creates a stable native child before Filament creates
  // its swapchain. The QWindow remains parented through createWindowContainer.
  (void)winId();
  native_surface_available_ = handle() != nullptr;
  if (!swap_chain_) {
    recreate_swap_chain(true);
  }
  synchronize_render_size();
  return static_cast<bool>(swap_chain_);
}

void QtViewportSurface::shutdown() {
  set_interaction_cursor(ViewportPointerCursor::Default);
  if (input_ != nullptr) {
    input_->wheel_accumulator = {};
  }
  if (composed_) {
    set_pointer_capture(false);
  }
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
  swap_chain_creation_failed_ = false;
}

bool QtViewportSurface::render_frame() {
  synchronize_render_size();
  if (composed_) {
    if (ready_to_render()) {
      visible_host_->update();
    }
    // QWidget::update requests a paint; it has not submitted a GPU frame.
    return false;
  }
  if (!swap_chain_ && native_surface_available_ && isVisible() &&
      isExposed()) {
    recreate_swap_chain();
  }
  if (!ready_to_render()) {
    return false;
  }

  const bool diagnostics_enabled = viewport_surface_diagnostics_enabled();
  const ViewportSurfaceProbeClock::time_point begin_start =
      diagnostics_enabled ? ViewportSurfaceProbeClock::now()
                          : ViewportSurfaceProbeClock::time_point{};
  if (!render_system_->begin_frame(swap_chain_)) {
    return false;
  }
  const ViewportSurfaceProbeClock::time_point render_start =
      diagnostics_enabled ? ViewportSurfaceProbeClock::now()
                          : ViewportSurfaceProbeClock::time_point{};
  render_system_->render_viewport(renderer_, swap_chain_);
  const ViewportSurfaceProbeClock::time_point end_start =
      diagnostics_enabled ? ViewportSurfaceProbeClock::now()
                          : ViewportSurfaceProbeClock::time_point{};
  render_system_->end_frame(swap_chain_);
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

bool QtViewportSurface::render_composed_frame(
    const RenderVulkanCompositionTarget &target) {
  if (!composed_ || !ready_to_render()) {
    return false;
  }
  // Geometry and camera preparation precede the Qt paint callback. Changing
  // the backing extent here would invalidate that accepted projection.
  return render_system_->render_composited_viewport(renderer_, swap_chain_, target);
}

void QtViewportSurface::release_composed_surface() {
  if (composed_) {
    swap_chain_.reset();
    presentation_geometry_->valid = false;
  }
}

void QtViewportSurface::create_composed_surface() {
  if (!composed_ || !initialized_ || render_system_ == nullptr ||
      !render_system_->initialized() || visible_host_ == nullptr) {
    return;
  }
  const qreal scale = input_device_pixel_ratio();
  QSize extent = visible_host_->size();
  if (const QScreen *screen = visible_host_->screen()) {
    extent = extent.expandedTo(screen->geometry().size());
  }
  const auto capacity = swap_chain_.capacity();
  const auto width = static_cast<std::uint32_t>(
      std::max(1, qRound(extent.width() * scale)));
  const auto height = static_cast<std::uint32_t>(
      std::max(1, qRound(extent.height() * scale)));
  if (swap_chain_ && capacity.width >= width && capacity.height >= height) {
    return;
  }
  swap_chain_ = render_system_->create_composited_swap_chain(
      std::max(width, capacity.width), std::max(height, capacity.height));
  presentation_geometry_->valid = false;
}

bool QtViewportSurface::ready_to_render() const {
  const QVec2 size = render_size_pixels();
  if (composed_) {
    return initialized_ && render_system_ != nullptr &&
           render_system_->initialized() && static_cast<bool>(swap_chain_) &&
           visible_host_ != nullptr && visible_host_->isVisible() &&
           input_host_exposed() && size.x >= 1.0f && size.y >= 1.0f;
  }
  return initialized_ && render_system_ != nullptr &&
         render_system_->initialized() && static_cast<bool>(swap_chain_) &&
         native_surface_available_ && isExposed() && isVisible() &&
         (visible_host_ == nullptr || visible_host_->isVisible()) &&
         size.x >= 1.0f && size.y >= 1.0f;
}

void QtViewportSurface::set_visible_host(QWidget &host) {
  if (visible_host_ == &host) {
    return;
  }
  if (visible_host_ != nullptr) {
    visible_host_->removeEventFilter(this);
  }
  visible_host_ = &host;
  visible_host_->installEventFilter(this);
  presentation_geometry_->valid = false;
  synchronize_render_size();
}

QPoint QtViewportSurface::visible_local_to_global(QPoint local) const {
  return visible_host_ != nullptr ? visible_host_->mapToGlobal(local)
                                  : mapToGlobal(local);
}

qreal QtViewportSurface::input_device_pixel_ratio() const {
  return composed_ && visible_host_ != nullptr
             ? visible_host_->devicePixelRatioF()
             : devicePixelRatio();
}

QPoint QtViewportSurface::input_local_from_global(QPoint global) const {
  return composed_ && visible_host_ != nullptr
             ? visible_host_->mapFromGlobal(global)
             : mapFromGlobal(global);
}

bool QtViewportSurface::input_host_exposed() const {
  if (!composed_) {
    return isExposed();
  }
  const QWidget *host_window =
      visible_host_ != nullptr ? visible_host_->window() : nullptr;
  const QWindow *native_window =
      host_window != nullptr ? host_window->windowHandle() : nullptr;
  return native_window != nullptr && native_window->isExposed();
}

std::uint64_t QtViewportSurface::presentation_geometry_query_count() const {
  return presentation_geometry_->native_query_count;
}

const QtViewportSurface::PresentationGeometry &
QtViewportSurface::presentation_geometry() const {
  auto &presentation = *presentation_geometry_;
  if (composed_) {
    const QSize visible_size = visible_host_ != nullptr
                                   ? visible_host_->size() : QSize{1, 1};
    const qreal scale = input_device_pixel_ratio();
    const QSize visible_pixels{
        std::max(1, qRound(visible_size.width() * scale)),
        std::max(1, qRound(visible_size.height() * scale))};
    const auto capacity = swap_chain_.capacity();
    presentation.visible_pixel_bounds = QRect(QPoint{}, visible_pixels);
    presentation.backing_pixel_size = {
        std::max(visible_pixels.width(), static_cast<int>(capacity.width)),
        std::max(visible_pixels.height(), static_cast<int>(capacity.height))};
    presentation.device_pixel_ratio = scale;
    presentation.visible_size = visible_size;
    presentation.valid = true;
    return presentation;
  }
  const QWindow *surface_parent = parent();
  const QRect parent_geometry =
      surface_parent != nullptr ? surface_parent->geometry() : QRect{};
  const QRect host_geometry =
      visible_host_ != nullptr ? visible_host_->geometry() : QRect{};
  const QSize visible_size =
      visible_host_ != nullptr ? visible_host_->size() : size();
  const qreal scale = visible_host_ != nullptr
                          ? visible_host_->devicePixelRatioF()
                          : devicePixelRatio();
  const WId host_id =
      visible_host_ != nullptr ? visible_host_->internalWinId() : 0u;
  const WId surface_id = handle() != nullptr
                             ? const_cast<QtViewportSurface *>(this)->winId()
                             : 0u;
  if (presentation.valid && presentation.surface_geometry == geometry() &&
      presentation.parent_geometry == parent_geometry &&
      presentation.host_geometry == host_geometry &&
      presentation.visible_size == visible_size &&
      presentation.surface_parent == surface_parent &&
      presentation.device_pixel_ratio == scale &&
      presentation.host_id == host_id && presentation.surface_id == surface_id) {
    return presentation;
  }
  presentation.surface_geometry = geometry();
  presentation.parent_geometry = parent_geometry;
  presentation.host_geometry = host_geometry;
  presentation.visible_size = visible_size;
  presentation.surface_parent = surface_parent;
  presentation.device_pixel_ratio = scale;
  presentation.host_id = host_id;
  presentation.surface_id = surface_id;
  presentation.backing_pixel_size = {
      std::max(1, qRound(width() * scale)),
      std::max(1, qRound(height() * scale)),
  };
  const auto surface_handle =
      reinterpret_cast<void *>(static_cast<std::uintptr_t>(surface_id));
  PlatformWindowClientExtent native_extent;
  if (surface_id != 0u) {
    ++presentation.native_query_count;
    native_extent = platform_window_client_extent(surface_handle);
  }
  if (native_extent.width > 0 && native_extent.height > 0) {
    presentation.backing_pixel_size = {native_extent.width, native_extent.height};
  }
  PlatformWindowClientBounds native_bounds;
  if (host_id != 0u && surface_id != 0u) {
    ++presentation.native_query_count;
    native_bounds = platform_window_client_bounds_in_window(
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(host_id)),
        surface_handle);
  }
  if (native_bounds.width > 0 && native_bounds.height > 0) {
    presentation.visible_pixel_bounds =
        {native_bounds.x, native_bounds.y, native_bounds.width, native_bounds.height};
  } else {
    const QPoint origin = visible_host_ != nullptr
                              ? mapFromGlobal(
                                    visible_host_->mapToGlobal(QPoint{0, 0}))
                              : QPoint{};
    presentation.visible_pixel_bounds = {
        qRound(origin.x() * scale), qRound(origin.y() * scale),
        std::max(1, qRound(visible_size.width() * scale)),
        std::max(1, qRound(visible_size.height() * scale)),
    };
  }
  presentation.valid = true;
  return presentation;
}

QVec2 QtViewportSurface::render_size_pixels() const {
  const QRect bounds = presentation_geometry().visible_pixel_bounds;
  return {
      static_cast<float>(std::max(1, bounds.width())),
      static_cast<float>(std::max(1, bounds.height())),
  };
}

QPointF QtViewportSurface::visible_local_position(QPointF surface_local) const {
  const auto &presentation = presentation_geometry();
  const qreal scale = std::max<qreal>(0.01, presentation.device_pixel_ratio);
  return surface_local -
         QPointF(presentation.visible_pixel_bounds.x() / scale,
                 presentation.visible_pixel_bounds.y() / scale);
}

QVec2 QtViewportSurface::visible_position_pixels(QPointF surface_local) const {
  const auto &presentation = presentation_geometry();
  return {
      static_cast<float>(surface_local.x() * presentation.device_pixel_ratio -
                         presentation.visible_pixel_bounds.x()),
      static_cast<float>(surface_local.y() * presentation.device_pixel_ratio -
                         presentation.visible_pixel_bounds.y()),
  };
}

bool QtViewportSurface::visible_position_contains(QPointF surface_local) const {
  const QVec2 local = visible_position_pixels(surface_local);
  const QVec2 visible_size = render_size_pixels();
  return local.x >= 0.0f && local.y >= 0.0f && local.x < visible_size.x &&
         local.y < visible_size.y;
}

QtViewportInputFrame
QtViewportSurface::take_input_frame(float delta_seconds,
                                    bool route_active_navigation) {
  QtViewportInputFrame frame;
  if (input_ == nullptr) {
    return frame;
  }
  if (composed_ && pointer_captured() &&
      QWidget::mouseGrabber() != visible_host_) {
    native_capture_loss_pending_ = true;
    record_native_pointer_capture_lost();
  }

  // Exposed native surfaces need the current held state even when the key
  // event went to another widget. Hidden/synthetic hosts retain their last
  // explicit snapshot rather than replacing a held key on every idle frame.
  if (input_host_exposed() || !input_->has_event_modifiers) {
    input_->modifiers = viewport_modifiers(input_host_exposed()
        ? QGuiApplication::queryKeyboardModifiers()
        : QGuiApplication::keyboardModifiers());
    input_->has_event_modifiers = true;
  }
  frame.input.viewport_hovered = input_->hovered || route_active_navigation;
  frame.input.viewport_focused = input_->focused || route_active_navigation;
  frame.input.pointer_interaction_canceled = input_->pointer_interaction_canceled;
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
  frame.input.mouse_delta = pointer_captured()
                                ? relative_pointer_input_.take_delta()
                                : input_->mouse_delta;
  frame.input.mouse_local = input_->mouse_local;
  frame.input.viewport_size = render_size_pixels();
  frame.input.device_pixel_ratio =
      static_cast<float>(std::max<qreal>(
          presentation_geometry().device_pixel_ratio, 0.01));
  frame.input.modifiers = input_->modifiers;
  frame.input.pressed_input_chords = std::move(input_->pressed);
  frame.input.released_input_chords = std::move(input_->released);
  frame.input.pointer_pressed_edges = std::move(input_->pointer_pressed_edges);
  frame.input.pointer_released_edges = std::move(input_->pointer_released_edges);
  frame.input.pointer_drag_started_edges =
      std::move(input_->pointer_drag_started_edges);
  frame.input.pointer_motion_samples = std::move(input_->pointer_motion_samples);
  frame.input.wheel_input_edges = std::move(input_->wheel);
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
  input_->pointer_interaction_canceled = false;
  input_->middle_pressed = false;
  input_->middle_released = false;
  input_->right_pressed = false;
  input_->right_released = false;
  input_->mouse_wheel = 0.0f;
  input_->mouse_delta = {};
  input_->mouse_move_events = 0u;
  input_->pressed.clear();
  input_->released.clear();
  input_->pointer_pressed_edges.clear();
  input_->pointer_released_edges.clear();
  input_->pointer_drag_started_edges.clear();
  input_->pointer_motion_samples.clear();
  input_->wheel.clear();
  if (pointer_captured()) {
    recenter_captured_pointer();
  }
  return frame;
}

void QtViewportSurface::abandon_input() {
  if (input_ == nullptr) {
    return;
  }

  std::unordered_set<int> abandoned_key_releases =
      std::move(input_->abandoned_key_releases);
  for (const auto &[key, chord] : input_->active_keys) {
    (void)chord;
    abandoned_key_releases.insert(key);
  }
  std::unordered_set<int> abandoned_pointer_releases =
      std::move(input_->abandoned_pointer_releases);
  const auto suppress_pointer_release = [&](QuaderMouseButton button,
                                            bool down) {
    if (down || input_->click_candidates.contains(static_cast<int>(button))) {
      abandoned_pointer_releases.insert(static_cast<int>(button));
    }
  };
  suppress_pointer_release(QuaderMouseButton::Left, input_->left_down);
  suppress_pointer_release(QuaderMouseButton::Middle, input_->middle_down);
  suppress_pointer_release(QuaderMouseButton::Right, input_->right_down);

  set_pointer_capture(false);
  ++pointer_capture_generation_;
  native_capture_loss_pending_ = false;
  relative_pointer_input_.clear();

  InputAccumulator replacement;
  replacement.hovered = input_->hovered;
  replacement.focused = input_->focused;
  replacement.suppress_warp_event = input_->suppress_warp_event;
  replacement.warp_motion_overtook_target =
      input_->warp_motion_overtook_target;
  replacement.warp_target_global = input_->warp_target_global;
  replacement.mouse_screen = input_->mouse_screen;
  replacement.mouse_local = input_->mouse_local;
  replacement.has_mouse_position = input_->has_mouse_position;
  replacement.modifiers = input_->modifiers;
  replacement.has_event_modifiers = input_->has_event_modifiers;
  replacement.abandoned_key_releases =
      std::move(abandoned_key_releases);
  replacement.abandoned_pointer_releases =
      std::move(abandoned_pointer_releases);
  *input_ = std::move(replacement);
  set_interaction_cursor(ViewportPointerCursor::Default);
}

void QtViewportSurface::set_pointer_capture(bool captured) {
  if (input_ == nullptr || pointer_capture_transition_active_ ||
      pointer_capture_policy_.captured() == captured) {
    return;
  }
  /**
   * Prevents synchronous Qt focus/grab notifications from re-entering a
   * partially applied capture transition.
   */
  struct CaptureTransitionGuard {
    explicit CaptureTransitionGuard(bool &active_state)
        : active(active_state) {
      active = true;
    }
    ~CaptureTransitionGuard() { active = false; }
    bool &active;
  } transition_guard(pointer_capture_transition_active_);

  if (captured) {
    native_capture_loss_pending_ = false;
    input_->capture_restore_global = QCursor::pos();
    const bool grab_succeeded =
        apply_native_mouse_grab(true) && input_->focused;
    const QtPointerCaptureTransition transition =
        pointer_capture_policy_.apply(true, grab_succeeded);
    if (transition == QtPointerCaptureTransition::EnableFailed) {
      if (pointer_capture_warning_gate_.should_warn(
              QtPointerCaptureFailure::Enable)) {
        LOG_WARN("Unable to enable Qt mouse grab for viewport pane {}",
                 viewport_pane_stable_id(pane_id_));
      }
      // A synchronous focus loss can make the combined enable predicate fail
      // after Qt accepted the grab. Always clean up the platform state while
      // the transition guard prevents focus notifications from re-entering.
      const bool cleanup_succeeded = apply_native_mouse_grab(false);
      if (!cleanup_succeeded && pointer_capture_warning_gate_.should_warn(
                                    QtPointerCaptureFailure::Disable)) {
        LOG_WARN("Unable to disable Qt mouse grab for viewport pane {}",
                 viewport_pane_stable_id(pane_id_));
      } else if (cleanup_succeeded) {
        pointer_capture_warning_gate_.note_success(
            QtPointerCaptureFailure::Disable);
      }
      relative_pointer_input_.clear();
      synchronize_interaction_cursor();
      return;
    }
    pointer_capture_warning_gate_.note_success(
        QtPointerCaptureFailure::Enable);
    ++pointer_capture_generation_;

    input_->suppress_warp_event = false;
    input_->warp_motion_overtook_target = false;
    input_->mouse_delta = {};
    input_->pointer_motion_samples.clear();
    input_->left_pointer_press_sequence = 0u;
    const bool raw_input_active =
        relative_pointer_input_.start_raw_input(native_window_handle());
    if (PlatformRawMouseInput::supported() && !raw_input_active) {
      if (!raw_input_warning_issued_) {
        LOG_WARN(
            "Unable to enable raw relative mouse input for viewport pane {} "
            "(native error {}); using the Qt pointer-delta fallback",
            viewport_pane_stable_id(pane_id_),
            relative_pointer_input_.last_native_error());
        raw_input_warning_issued_ = true;
      }
    } else if (raw_input_active) {
      raw_input_warning_issued_ = false;
    }
    synchronize_interaction_cursor();
    recenter_captured_pointer();
  } else {
    const bool restore_pointer =
        !composed_ || QWidget::mouseGrabber() == visible_host_;
    (void)relative_pointer_input_.stop_raw_input();
    const bool release_succeeded = apply_native_mouse_grab(false);
    const QtPointerCaptureTransition transition =
        pointer_capture_policy_.apply(false, release_succeeded);
    native_capture_loss_pending_ = false;
    ++pointer_capture_generation_;
    if (transition == QtPointerCaptureTransition::DisableFailed) {
      if (pointer_capture_warning_gate_.should_warn(
              QtPointerCaptureFailure::Disable)) {
        LOG_WARN("Unable to disable Qt mouse grab for viewport pane {}",
                 viewport_pane_stable_id(pane_id_));
      }
    } else {
      pointer_capture_warning_gate_.note_success(
          QtPointerCaptureFailure::Disable);
    }
    synchronize_interaction_cursor();
    if (!restore_pointer) {
      input_->suppress_warp_event = false;
      input_->warp_motion_overtook_target = false;
      return;
    }
    const QPoint restore = input_->capture_restore_global;
    const QPoint local = input_local_from_global(restore);
    const qreal scale = input_device_pixel_ratio();
    input_->warp_target_global = restore;
    input_->suppress_warp_event = true;
    input_->warp_motion_overtook_target = false;
    input_->mouse_local = visible_position_pixels(local);
    input_->mouse_screen = {
        static_cast<float>(restore.x() * scale),
        static_cast<float>(restore.y() * scale),
    };
    input_->has_mouse_position = true;
    QCursor::setPos(restore);
    input_->hovered = visible_position_contains(local);
  }
}

bool QtViewportSurface::pointer_captured() const {
  return input_ != nullptr && pointer_capture_policy_.captured();
}

bool QtViewportSurface::event(QEvent *event) {
  if (accept_viewport_shortcut_override(event)) {
    return true;
  }
  if (!composed_ && event != nullptr && event->type() == QEvent::PlatformSurface) {
    presentation_geometry_->valid = false;
    auto *surface_event = static_cast<QPlatformSurfaceEvent *>(event);
    if (surface_event->surfaceEventType() ==
        QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
      native_surface_available_ = false;
      swap_chain_.reset();
      swap_chain_native_id_ = 0u;
      swap_chain_creation_failed_ = false;
    } else if (surface_event->surfaceEventType() ==
               QPlatformSurfaceEvent::SurfaceCreated) {
      native_surface_available_ = true;
      recreate_swap_chain(true);
      synchronize_render_size();
    }
  } else if (event != nullptr &&
             (event->type() == QEvent::DevicePixelRatioChange ||
              event->type() == QEvent::ScreenChangeInternal ||
              event->type() == QEvent::WinIdChange)) {
    presentation_geometry_->valid = false;
    if (event->type() == QEvent::WinIdChange) {
      recreate_swap_chain(true);
    }
    if (event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::ScreenChangeInternal) {
      rebase_pointer_after_scale_change();
    }
    synchronize_render_size();
  } else if (event != nullptr && event->type() == QEvent::Move) {
    presentation_geometry_->valid = false;
    synchronize_render_size();
    if (surface_changed_callback_) {
      surface_changed_callback_();
    }
  } else if (event != nullptr && event->type() == QEvent::Enter) {
    record_pointer_enter(*static_cast<QEnterEvent *>(event));
  } else if (event != nullptr && event->type() == QEvent::Leave) {
    input_->hovered = pointer_captured();
    synchronize_interaction_cursor();
  }
  return QWindow::event(event);
}

void QtViewportSurface::rebase_pointer_after_scale_change() {
  // Physical local coordinates change units with the screen DPR. Rebase
  // the accumulator at the current cursor so the next MMB event cannot
  // turn that unit change into a screen-sized navigation delta.
  const QPoint global = QCursor::pos();
  const QPoint local = input_local_from_global(global);
  const qreal scale = input_device_pixel_ratio();
  input_->mouse_local = visible_position_pixels(local);
  input_->mouse_screen = {
      static_cast<float>(global.x() * scale),
      static_cast<float>(global.y() * scale),
  };
  input_->mouse_delta = {};
  input_->has_mouse_position = true;
  // Queued edge coordinates were sampled in the previous DPR units. Do
  // not let a delayed press/release subtraction mix those units with the
  // newly rebased pointer. Held navigation resumes from the next motion.
  input_->pointer_pressed_edges.clear();
  input_->pointer_released_edges.clear();
  input_->pointer_drag_started_edges.clear();
  input_->pointer_motion_samples.clear();
  if (input_->left_pointer_press_sequence != 0u) {
    input_->pointer_interaction_canceled = true;
    input_->left_pointer_press_sequence = 0u;
  }
  relative_pointer_input_.clear();
}

void QtViewportSurface::record_pointer_enter(QEnterEvent &event) {
  const qreal scale = input_device_pixel_ratio();
  input_->mouse_local = visible_position_pixels(event.position());
  input_->mouse_screen = {
      static_cast<float>(event.globalPosition().x() * scale),
      static_cast<float>(event.globalPosition().y() * scale),
  };
  input_->has_mouse_position = true;
  input_->hovered = pointer_captured() || visible_host_ == nullptr ||
                    visible_position_contains(event.position());
  if (!input_->has_event_modifiers) {
    input_->modifiers = viewport_modifiers(QGuiApplication::keyboardModifiers());
    input_->has_event_modifiers = true;
  }
  if (focus_request_callback_) {
    focus_request_callback_(pane_id_, ViewportPaneFocusTrigger::Hover);
  }
  synchronize_interaction_cursor();
}

bool QtViewportSurface::eventFilter(QObject *watched, QEvent *event) {
  if (composed_ && watched == visible_host_.data() && event != nullptr) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
      mousePressEvent(static_cast<QMouseEvent *>(event));
      return true;
    case QEvent::MouseButtonRelease:
      mouseReleaseEvent(static_cast<QMouseEvent *>(event));
      return true;
    case QEvent::MouseButtonDblClick:
      mouseDoubleClickEvent(static_cast<QMouseEvent *>(event));
      return true;
    case QEvent::MouseMove:
      mouseMoveEvent(static_cast<QMouseEvent *>(event));
      return true;
    case QEvent::Wheel:
      wheelEvent(static_cast<QWheelEvent *>(event));
      return true;
    case QEvent::Enter:
      record_pointer_enter(*static_cast<QEnterEvent *>(event));
      break;
    case QEvent::Leave:
      input_->hovered = pointer_captured();
      synchronize_interaction_cursor();
      break;
    case QEvent::Hide:
      record_focus_out();
      break;
    case QEvent::DevicePixelRatioChange:
    case QEvent::ScreenChangeInternal:
      rebase_pointer_after_scale_change();
      break;
    default:
      break;
    }
  }
  if (watched == visible_host_.data() && event != nullptr &&
      (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
       event->type() == QEvent::Show ||
       event->type() == QEvent::DevicePixelRatioChange ||
       event->type() == QEvent::ScreenChangeInternal ||
       event->type() == QEvent::WinIdChange)) {
    presentation_geometry_->valid = false;
    synchronize_render_size();
    if (surface_changed_callback_) {
      surface_changed_callback_();
    }
  }
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
      if (accept_viewport_shortcut_override(event)) {
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
  native_surface_available_ = handle() != nullptr;
  if (isExposed()) {
    recreate_swap_chain(true);
  }
  synchronize_render_size();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtViewportSurface::resizeEvent(QResizeEvent *event) {
  QWindow::resizeEvent(event);
  presentation_geometry_->valid = false;
  synchronize_render_size();
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

void QtViewportSurface::record_focus_in() {
  input_->focused = true;
  synchronize_interaction_cursor();
}

void QtViewportSurface::record_focus_out() {
  input_->focused = false;
  input_->modifiers = {};
  input_->has_event_modifiers = true;
  input_->wheel_accumulator = {};
  input_->pointer_motion_samples.clear();
  input_->left_pointer_press_sequence = 0u;
  input_->pointer_interaction_canceled =
      input_->pointer_interaction_canceled || input_->left_down ||
      input_->middle_down || input_->right_down ||
      !input_->click_candidates.empty();
  // Native grabs can be dropped by the window manager before the next app
  // frame. Release Quader's logical capture immediately so the cursor cannot
  // remain hidden or be re-centered after focus moves elsewhere.
  const bool capture_was_stolen = native_capture_loss_pending_;
  set_pointer_capture(false);
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
  for (const auto &[button, candidate] : input_->click_candidates) {
    (void)candidate;
    input_->pointer_released_edges.push_back({
        .button = static_cast<QuaderMouseButton>(button),
        .local = input_->mouse_local,
    });
  }
  input_->click_candidates.clear();
  input_->left_released = input_->left_released || input_->left_down;
  input_->middle_released = input_->middle_released || input_->middle_down;
  input_->right_released =
      input_->right_released || (input_->right_down && !capture_was_stolen);
  input_->left_down = false;
  input_->middle_down = false;
  input_->right_down = false;
  synchronize_interaction_cursor();
}

void QtViewportSurface::record_key_press(QKeyEvent &event) {
  if (event.isAutoRepeat()) {
    return;
  }
  // Modifier-only keys have no command chord, but still change live tool UI.
  input_->modifiers = viewport_modifiers(event);
  input_->has_event_modifiers = true;
  const std::optional<QuaderInputChord> chord =
      quader_pressed_input_chord_from_qt_event(event);
  if (!chord.has_value()) {
    return;
  }
  input_->abandoned_key_releases.erase(event.key());
  input_->pressed.push_back(*chord);
  input_->active_keys.insert_or_assign(event.key(), *chord);
}

void QtViewportSurface::record_key_release(QKeyEvent &event) {
  if (event.isAutoRepeat()) {
    return;
  }
  input_->modifiers = viewport_modifiers(event);
  input_->has_event_modifiers = true;
  if (input_->abandoned_key_releases.erase(event.key()) > 0u) {
    return;
  }
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
  input_->has_event_modifiers = true;
  record_mouse_move(event);
  input_->hovered = true;
  input_->focused = true;
  synchronize_interaction_cursor();
  input_->modifiers = viewport_modifiers(event.modifiers());
  const std::optional<QuaderMouseButton> button =
      quader_mouse_button_from_qt_button(event.button());
  if (!button.has_value()) {
    return;
  }
  input_->abandoned_pointer_releases.erase(static_cast<int>(*button));
  input_->pointer_pressed_edges.push_back({
      .button = *button,
      .local = input_->mouse_local,
      .sequence = ++input_->pointer_event_sequence,
      .modifiers = input_->modifiers,
  });

  if (double_click) {
    input_->click_candidates.erase(static_cast<int>(*button));
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
      QuaderInputChord click = *chord;
      click.gesture = QuaderInputGesture::Click;
      input_->click_candidates.insert_or_assign(
          static_cast<int>(*button),
          InputAccumulator::ClickCandidate{
              .chord = click,
              .press_local = visible_local_position(event.position()),
              .last_local = visible_local_position(event.position()),
          });
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
    input_->left_pointer_press_sequence =
        pointer_captured() ? 0u : input_->pointer_event_sequence;
    if (input_->left_pointer_press_sequence != 0u) {
      input_->pointer_motion_samples.push_back({
          .button = QuaderMouseButton::Left,
          .local = input_->mouse_local,
          .sequence = input_->pointer_event_sequence,
          .press_sequence = input_->left_pointer_press_sequence,
      });
    }
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
  input_->has_event_modifiers = true;
  record_mouse_move(event);
  input_->modifiers = viewport_modifiers(event.modifiers());
  const std::optional<QuaderMouseButton> button =
      quader_mouse_button_from_qt_button(event.button());
  if (!button.has_value()) {
    return;
  }
  if (input_->abandoned_pointer_releases.erase(
          static_cast<int>(*button)) > 0u) {
    return;
  }
  ViewportPointerHeldInputState held_input;
  held_input.active_input_chords.reserve(input_->active_keys.size());
  for (const auto &[key, chord] : input_->active_keys) {
    (void)key;
    held_input.active_input_chords.push_back(chord);
  }
  input_->pointer_released_edges.push_back({
      .button = *button,
      .local = input_->mouse_local,
      .sequence = ++input_->pointer_event_sequence,
      .modifiers = input_->modifiers,
      .held_input = std::move(held_input),
  });
  const auto click =
      input_->click_candidates.find(static_cast<int>(*button));
  if (click != input_->click_candidates.end()) {
    if (click->second.eligible) {
      input_->pressed.push_back(click->second.chord);
    }
    input_->click_candidates.erase(click);
  }
  switch (*button) {
  case QuaderMouseButton::Left:
    if (input_->left_pointer_press_sequence != 0u) {
      input_->pointer_motion_samples.push_back({
          .button = QuaderMouseButton::Left,
          .local = input_->mouse_local,
          .sequence = input_->pointer_event_sequence,
          .press_sequence = input_->left_pointer_press_sequence,
      });
      input_->left_pointer_press_sequence = 0u;
    }
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
  input_->has_event_modifiers = true;
  if (viewport_surface_diagnostics_enabled()) {
    ++input_->mouse_move_events;
  }
  const qreal scale = input_device_pixel_ratio();
  const QVec2 local = visible_position_pixels(event.position());
  const QPointF visible_local = visible_local_position(event.position());
  const QVec2 screen{
      static_cast<float>(event.globalPosition().x() * scale),
      static_cast<float>(event.globalPosition().y() * scale),
  };
  if (input_->suppress_warp_event) {
    const QPoint delta = event.globalPosition().toPoint() -
                         input_->warp_target_global;
    if (std::abs(delta.x()) <= 2 && std::abs(delta.y()) <= 2) {
      if (!input_->warp_motion_overtook_target) {
        input_->mouse_local = local;
        input_->mouse_screen = screen;
        input_->has_mouse_position = true;
        input_->hovered = pointer_captured() || visible_host_ == nullptr ||
                          visible_position_contains(event.position());
        synchronize_interaction_cursor();
      }
      input_->modifiers = viewport_modifiers(event.modifiers());
      for (auto &[button, candidate] : input_->click_candidates) {
        (void)button;
        candidate.last_local = visible_local;
      }
      input_->suppress_warp_event = false;
      input_->warp_motion_overtook_target = false;
      return;
    }
    // Physical motion may be delivered before the queued synthetic center
    // event. Keep waiting for the target while processing this genuine move
    // against the last physical baseline; otherwise the later warp becomes a
    // cursor-sized fallback delta.
    input_->warp_motion_overtook_target = true;
  }
  const int start_drag_distance =
      QGuiApplication::styleHints() != nullptr
          ? QGuiApplication::styleHints()->startDragDistance()
          : 10;
  for (auto &[button, candidate] : input_->click_candidates) {
    if (candidate.eligible) {
      qreal displacement = 0.0;
      if (pointer_captured()) {
        const QPointF delta = visible_local - candidate.last_local;
        candidate.captured_distance +=
            std::abs(delta.x()) + std::abs(delta.y());
        displacement = candidate.captured_distance;
      } else {
        const QPointF delta = visible_local - candidate.press_local;
        displacement = std::abs(delta.x()) + std::abs(delta.y());
      }
      if (displacement >= static_cast<qreal>(start_drag_distance)) {
        candidate.eligible = false;
        input_->pointer_drag_started_edges.push_back({
            .button = static_cast<QuaderMouseButton>(button),
            .local = local,
            .modifiers = viewport_modifiers(event.modifiers()),
        });
      }
    }
    candidate.last_local = visible_local;
  }
  if (input_->has_mouse_position) {
    const QVec2 absolute_delta = local - input_->mouse_local;
    if (pointer_captured()) {
      // Raw input is the captured navigation source on Windows. The portable
      // fallback uses accepted absolute Qt motion but never warp-generated
      // events, which returned through the suppression branch above.
      relative_pointer_input_.add_fallback_delta(absolute_delta);
    } else {
      input_->mouse_delta = input_->mouse_delta + absolute_delta;
    }
  }
  input_->mouse_local = local;
  input_->mouse_screen = screen;
  input_->has_mouse_position = true;
  if (event.type() == QEvent::MouseMove &&
      input_->left_pointer_press_sequence != 0u && !pointer_captured()) {
    input_->pointer_motion_samples.push_back({
        .button = QuaderMouseButton::Left,
        .local = local,
        .sequence = ++input_->pointer_event_sequence,
        .press_sequence = input_->left_pointer_press_sequence,
    });
  }
  input_->hovered = pointer_captured() || visible_host_ == nullptr ||
                    visible_position_contains(event.position());
  input_->modifiers = viewport_modifiers(event.modifiers());
  synchronize_interaction_cursor();
}

void QtViewportSurface::record_native_pointer_capture_lost() {
  if (!pointer_captured()) {
    native_capture_loss_pending_ = false;
    return;
  }
  const bool capture_was_stolen = native_capture_loss_pending_;
  if (capture_was_stolen) {
    input_->wheel_accumulator = {};
    input_->pointer_motion_samples.clear();
    input_->left_pointer_press_sequence = 0u;
  }
  input_->pointer_interaction_canceled =
      input_->pointer_interaction_canceled ||
      (capture_was_stolen && (input_->left_down || input_->middle_down ||
                             input_->right_down ||
                             !input_->click_candidates.empty()));
  set_pointer_capture(false);
  // Capture theft cancels fly navigation, but it is not a physical button-up
  // and must not synthesize a click that can open the viewport context menu.
  if (capture_was_stolen) {
    if (input_->middle_drag.has_value()) {
      input_->released.push_back(*input_->middle_drag);
    }
    if (input_->right_drag.has_value()) {
      input_->released.push_back(*input_->right_drag);
    }
    input_->middle_drag.reset();
    input_->right_drag.reset();

    const auto release_button_state =
        [&](QuaderMouseButton button, bool &down, bool &released) {
      const int button_id = static_cast<int>(button);
      if (!down && !input_->click_candidates.contains(button_id)) {
        return;
      }
      // The eventual native button-up belongs to the canceled owner. Record
      // one semantic endpoint now, then suppress that trailing physical edge.
      input_->abandoned_pointer_releases.insert(button_id);
      input_->pointer_released_edges.push_back({
          .button = button,
          .local = input_->mouse_local,
          .sequence = ++input_->pointer_event_sequence,
      });
      released = released || down;
      down = false;
    };
    release_button_state(QuaderMouseButton::Left, input_->left_down,
                         input_->left_released);
    release_button_state(QuaderMouseButton::Middle, input_->middle_down,
                         input_->middle_released);
    release_button_state(QuaderMouseButton::Right, input_->right_down,
                         input_->right_released);

    input_->left_pressed = false;
    input_->middle_pressed = false;
    input_->right_pressed = false;
    input_->click_candidates.clear();
    relative_pointer_input_.clear();
  }
}

bool QtViewportSurface::apply_native_mouse_grab(bool captured) {
  if (composed_) {
    if (visible_host_ == nullptr) {
      return !captured;
    }
    if (!captured) {
      if (QWidget::mouseGrabber() == visible_host_) {
        visible_host_->releaseMouse();
      }
      // Another pane can retain capture through the same native ancestor.
      return QWidget::mouseGrabber() != visible_host_;
    }
    visible_host_->grabMouse();
    const bool widget_grabbed = QWidget::mouseGrabber() == visible_host_;
    const auto postcondition =
        platform_pointer_capture_postcondition(native_window_handle(), true);
    return widget_grabbed && platform_pointer_capture_transition_achieved(
                                 widget_grabbed, postcondition);
  }
  const bool qt_result = setMouseGrabEnabled(captured);
  const PlatformPointerCapturePostcondition postcondition =
      platform_pointer_capture_postcondition(native_window_handle(), captured);
  return platform_pointer_capture_transition_achieved(qt_result,
                                                      postcondition);
}

void QtViewportSurface::recenter_captured_pointer() {
  const QSize visible_size = visible_host_ != nullptr ? visible_host_->size()
                                                      : size();
  if (input_ == nullptr || !pointer_captured() || visible_size.width() <= 0 ||
      visible_size.height() <= 0 ||
      !relative_pointer_input_.capture_requires_recenter()) {
    return;
  }
  if (input_->suppress_warp_event) {
    // Do not queue another indistinguishable center event while the previous
    // one can still arrive after genuine physical motion.
    return;
  }
  const QPoint visible_local(visible_size.width() / 2, visible_size.height() / 2);
  const QPoint global = visible_local_to_global(visible_local);
  const QPoint local = input_local_from_global(global);
  const qreal scale = input_device_pixel_ratio();
  // SetCursorPos changes the platform cursor before Qt necessarily delivers
  // the translated center event. Use the target as the fallback baseline now
  // so a physical move that overtakes that event contributes only its delta.
  input_->mouse_local = visible_position_pixels(local);
  input_->mouse_screen = {
      static_cast<float>(global.x() * scale),
      static_cast<float>(global.y() * scale),
  };
  input_->has_mouse_position = true;
  if (QCursor::pos() == global) {
    return;
  }
  input_->warp_target_global = global;
  input_->suppress_warp_event = true;
  input_->warp_motion_overtook_target = false;
  QCursor::setPos(global);
}

void QtViewportSurface::record_wheel(QWheelEvent &event) {
  input_->has_event_modifiers = true;
  const qreal scale = input_device_pixel_ratio();
  input_->mouse_local = visible_position_pixels(event.position());
  input_->mouse_screen = {
      static_cast<float>(event.globalPosition().x() * scale),
      static_cast<float>(event.globalPosition().y() * scale),
  };
  input_->has_mouse_position = true;
  input_->hovered = pointer_captured() || visible_host_ == nullptr ||
                    visible_position_contains(event.position());
  input_->modifiers = viewport_modifiers(event.modifiers());
  synchronize_interaction_cursor();
  QtWheelInputTranslation translation =
      quader_wheel_input_from_qt_event(
          event, input_->wheel_accumulator, static_cast<double>(scale));
  for (QuaderWheelInputEdge &edge : translation.wheel_input_edges) {
    if (edge.has_local_position) {
      edge.local = {input_->mouse_local.x, input_->mouse_local.y};
    }
  }
  input_->mouse_wheel += translation.mouse_wheel;
  input_->wheel.insert(input_->wheel.end(), translation.wheel_input_edges.begin(),
                       translation.wheel_input_edges.end());
}

void QtViewportSurface::recreate_swap_chain(bool force_retry) {
  if (composed_ || !initialized_ || render_system_ == nullptr ||
      !render_system_->initialized() || !native_surface_available_) {
    return;
  }
  if (swap_chain_creation_failed_ && !force_retry) {
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
    if (!swap_chain_creation_failed_) {
      LOG_ERROR("Unable to create Filament swapchain for Qt viewport pane {}",
                viewport_pane_stable_id(pane_id_));
    }
    swap_chain_creation_failed_ = true;
  } else {
    swap_chain_creation_failed_ = false;
    swap_chain_native_id_ = native_id;
  }
}

void QtViewportSurface::synchronize_render_size() {
  if (!initialized_) {
    return;
  }
  create_composed_surface();
  const QVec2 size = render_size_pixels();
  const std::uint32_t width =
      static_cast<std::uint32_t>(std::max(1.0f, size.x));
  const std::uint32_t height =
      static_cast<std::uint32_t>(std::max(1.0f, size.y));
  if (renderer_.width() != width || renderer_.height() != height) {
    if (composed_) {
      render_system_->mark_shared_commands_pending();
    }
    renderer_.resize(width, height);
  }
  const auto &presentation = presentation_geometry();
  renderer_.set_presentation_bounds(
      static_cast<std::uint32_t>(presentation.backing_pixel_size.width()),
      static_cast<std::uint32_t>(presentation.backing_pixel_size.height()),
      static_cast<std::uint32_t>(std::max(0, presentation.visible_pixel_bounds.x())),
      static_cast<std::uint32_t>(std::max(0, presentation.visible_pixel_bounds.y())));
}

void *QtViewportSurface::native_window_handle() const {
  if (composed_) {
    const WId id = visible_host_ != nullptr ? visible_host_->effectiveWinId() : 0u;
    return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
  }
  const WId id = const_cast<QtViewportSurface *>(this)->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

} // namespace quader
