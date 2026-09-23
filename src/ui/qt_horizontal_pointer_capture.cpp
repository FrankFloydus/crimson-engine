////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_horizontal_pointer_capture.hpp"

#include "platform/raw_mouse_input.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

QRect capture_bounds(QWidget &widget, QPoint global_position) {
  QWidget *window = widget.window();
  QRect bounds(window->mapToGlobal(QPoint()), window->size());
  QScreen *screen = QGuiApplication::screenAt(global_position);
  if (!screen) {
    screen = window->screen();
  }
  if (screen) {
    bounds = bounds.intersected(screen->geometry());
  }
  // Keep the hidden pointer comfortably inside the client and monitor edges.
  // Tiny popups still retain a usable interior instead of an inverted rectangle.
  const int inset = std::max(1, std::min(bounds.width(), bounds.height()) / 10);
  if (bounds.width() > inset * 2 + 2 &&
      bounds.height() > inset * 2 + 2) {
    bounds.adjust(inset, inset, -inset, -inset);
  }
  return bounds;
}

} // namespace

void QtHorizontalPointerWrap::start(QPointF global_position, QRect bounds) {
  last_position_ = global_position;
  bounds_ = bounds;
  pending_warp_.reset();
  awaiting_warp_motion_ = false;
}

QtHorizontalPointerMotion
QtHorizontalPointerWrap::move(QPointF global_position) {
  if (pending_warp_ &&
      std::abs(global_position.x() - pending_warp_->x()) <= 0.5 &&
      std::abs(global_position.y() - pending_warp_->y()) <= 0.5) {
    // A real move may overtake this queued echo. The baseline already moved
    // to the warp target before SetCursorPos, then advanced with that move;
    // rewinding it here would count the same physical travel a second time.
    pending_warp_.reset();
    awaiting_warp_motion_ = false;
    return {};
  }

  const bool outside_bounds =
      !bounds_.isEmpty() &&
      (global_position.x() <= bounds_.left() ||
       global_position.x() >= bounds_.right() ||
       global_position.y() <= bounds_.top() ||
       global_position.y() >= bounds_.bottom());
  if (awaiting_warp_motion_ && outside_bounds) {
    // Queued pre-warp edge samples must not repeatedly add the distance from
    // the center. Resume only once an echo or genuine interior motion proves
    // that the cursor has moved away from the old boundary.
    return {};
  }
  awaiting_warp_motion_ = false;

  QtHorizontalPointerMotion result;
  result.delta_x = global_position.x() - last_position_.x();
  last_position_ = global_position;
  if (outside_bounds) {
    // A stable center target allows another wrap even if Qt coalesced the
    // previous target echo with physical motion. Vertical movement recenters
    // too, although only horizontal motion contributes to the numeric value.
    result.warp_target = bounds_.center();
    pending_warp_ = result.warp_target;
    awaiting_warp_motion_ = true;
    last_position_ = QPointF(*result.warp_target);
  }
  return result;
}

void QtHorizontalPointerWrap::clear() {
  last_position_ = {};
  bounds_ = {};
  pending_warp_.reset();
  awaiting_warp_motion_ = false;
}

QtHorizontalPointerCapture::~QtHorizontalPointerCapture() { stop(); }

bool QtHorizontalPointerCapture::start(QWidget &widget,
                                       QPointF global_position,
                                       QPointF restore_position) {
  const QPointer<QtHorizontalPointerCapture> guard(this);
  const QPointer<QWidget> receiver(&widget);
  stop();
  if (!guard || !receiver) {
    return false;
  }
  const std::uint64_t generation = generation_;
  widget_ = &widget;
  restore_position_ = restore_position.toPoint();
  const bool synthetic =
      widget.testAttribute(Qt::WA_DontShowOnScreen) ||
      widget.window()->testAttribute(Qt::WA_DontShowOnScreen);
  if (!synthetic && !widget.isVisible()) {
    widget_.clear();
    return false;
  }
  motion_.start(global_position,
                synthetic ? QRect()
                          : capture_bounds(widget, global_position.toPoint()));
  if (synthetic) {
    // Hidden fixture widgets exercise gesture semantics without touching the
    // user's desktop. Wrapping itself is covered by the pure motion policy.
    active_ = true;
    return true;
  }

  widget.grabMouse(QCursor(Qt::BlankCursor));
  if (!guard) {
    if (receiver && QWidget::mouseGrabber() == receiver) {
      QCursor::setPos(restore_position.toPoint());
      receiver->releaseMouse();
    }
    return false;
  }
  if (!receiver) {
    widget_.clear();
    motion_.clear();
    return false;
  }
  if (generation_ != generation) {
    // A nested focus/capture event can end this gesture while grabMouse is
    // still in progress. Do not resurrect it when the native call returns.
    if (!native_capture_ && QWidget::mouseGrabber() == receiver) {
      QCursor::setPos(restore_position.toPoint());
      receiver->releaseMouse();
    }
    return false;
  }
  const bool toolkit_grab = QWidget::mouseGrabber() == receiver;
  const auto postcondition = platform_pointer_capture_postcondition(
      reinterpret_cast<void *>(receiver->effectiveWinId()), true);
  if (!toolkit_grab ||
      !platform_pointer_capture_transition_achieved(toolkit_grab,
                                                   postcondition)) {
    widget_.clear();
    motion_.clear();
    if (toolkit_grab) {
      receiver->releaseMouse();
    }
    return false;
  }
  active_ = true;
  native_capture_ = true;
  return true;
}

double QtHorizontalPointerCapture::delta(QPointF global_position) {
  if (!active_ || !widget_) {
    return 0.0;
  }
  const QtHorizontalPointerMotion accepted = motion_.move(global_position);
  if (native_capture_ && accepted.warp_target) {
    QCursor::setPos(*accepted.warp_target);
  }
  return accepted.delta_x;
}

void QtHorizontalPointerCapture::stop() {
  const QPointer<QWidget> receiver = widget_;
  const QPoint restore = restore_position_;
  const bool native_capture = native_capture_;
  ++generation_;
  active_ = false;
  native_capture_ = false;
  widget_.clear();
  motion_.clear();
  if (!native_capture) {
    return;
  }
  // Clear the logical state before either platform call can deliver nested
  // events. Restore before releasing, so a newly acquired grab cannot have its
  // cursor moved by this old capture's cleanup.
  QCursor::setPos(restore);
  if (receiver && QWidget::mouseGrabber() == receiver) {
    receiver->releaseMouse();
  }
}

} // namespace quader
