////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_relative_pointer_input.hpp"

#include <QCoreApplication>

namespace quader {

void QtRelativePointerStreamAccumulator::add_qt_delta(QVec2 delta) noexcept {
  if (delta.x == 0.0f && delta.y == 0.0f) {
    return;
  }
  if (packet_mode_ == QtRelativePointerPacketMode::RelativeRaw) {
    // Legacy mouse moves are coalesced independently from WM_INPUT. Mixing
    // their absolute deltas into the raw stream can inject a cursor-sized jump,
    // especially when camera translation increases frame/event latency.
    return;
  }
  qt_deltas_.add(delta);
}

void QtRelativePointerStreamAccumulator::note_relative_raw_delta(
    QVec2 delta) noexcept {
  if (delta.x == 0.0f && delta.y == 0.0f) {
    // Button-only raw packets do not prove that this device supplies motion.
    return;
  }
  // A translated move can reach Qt before its WM_INPUT classification. Once
  // relative raw motion is confirmed, discard any fallback delta queued during
  // that ordering window instead of summing two representations of one move.
  qt_deltas_.clear();
  packet_mode_ = QtRelativePointerPacketMode::RelativeRaw;
  raw_deltas_.add(delta);
}

void QtRelativePointerStreamAccumulator::note_absolute_raw_motion() noexcept {
  // Absolute raw coordinates cannot provide a relative delta here. Select Qt's
  // translated motion until a relative raw packet selects the raw source again.
  raw_deltas_.clear();
  packet_mode_ = QtRelativePointerPacketMode::QtFallback;
}

QVec2 QtRelativePointerStreamAccumulator::take() noexcept {
  if (packet_mode_ == QtRelativePointerPacketMode::RelativeRaw) {
    qt_deltas_.clear();
    return raw_deltas_.take();
  }
  raw_deltas_.clear();
  return qt_deltas_.take();
}

void QtRelativePointerStreamAccumulator::reset() noexcept {
  raw_deltas_.clear();
  qt_deltas_.clear();
  packet_mode_ = QtRelativePointerPacketMode::Undetermined;
}

QtPointerCaptureTransition
QtPointerCapturePolicy::apply(bool requested,
                              bool platform_transition_succeeded) noexcept {
  if (requested == captured_) {
    return QtPointerCaptureTransition::NoChange;
  }
  if (requested) {
    if (!platform_transition_succeeded) {
      return QtPointerCaptureTransition::EnableFailed;
    }
    captured_ = true;
    return QtPointerCaptureTransition::Enabled;
  }

  captured_ = false;
  return platform_transition_succeeded
             ? QtPointerCaptureTransition::Disabled
             : QtPointerCaptureTransition::DisableFailed;
}

bool QtPointerCaptureWarningGate::should_warn(
    QtPointerCaptureFailure failure) noexcept {
  bool &issued = failure == QtPointerCaptureFailure::Enable
                     ? enable_warning_issued_
                     : disable_warning_issued_;
  if (issued) {
    return false;
  }
  issued = true;
  return true;
}

void QtPointerCaptureWarningGate::note_success(
    QtPointerCaptureFailure failure) noexcept {
  bool &issued = failure == QtPointerCaptureFailure::Enable
                     ? enable_warning_issued_
                     : disable_warning_issued_;
  issued = false;
}

QtRelativePointerInput::~QtRelativePointerInput() { (void)stop_raw_input(); }

bool QtRelativePointerInput::start_raw_input(void *native_window) {
  clear();
  capture_loss_notification_pending_ = false;
  if (platform_input_.active()) {
    return true;
  }
  if (!PlatformRawMouseInput::supported()) {
    return false;
  }

  QCoreApplication *application = QCoreApplication::instance();
  if (application == nullptr) {
    return false;
  }
  application->installNativeEventFilter(this);
  native_filter_installed_ = true;
  // Keep the filter installed even when raw registration fails. The platform
  // target still classifies WM_CAPTURECHANGED while Qt deltas provide motion.
  return platform_input_.start(native_window);
}

bool QtRelativePointerInput::stop_raw_input() {
  const bool stopped = platform_input_.stop();
  if (native_filter_installed_) {
    if (QCoreApplication *application = QCoreApplication::instance()) {
      application->removeNativeEventFilter(this);
    }
    native_filter_installed_ = false;
  }
  capture_loss_notification_pending_ = false;
  clear();
  return stopped;
}

bool QtRelativePointerInput::nativeEventFilter(const QByteArray &event_type,
                                               void *message,
                                               qintptr *result) {
  (void)event_type;
  (void)result;
  const PlatformRawMouseMessage classified =
      platform_input_.classify_native_message(message);
  switch (classified.kind) {
  case PlatformRawMouseMessageKind::RelativeMotion:
    stream_deltas_.note_relative_raw_delta(
        {static_cast<float>(classified.relative_delta_x),
         static_cast<float>(classified.relative_delta_y)});
    break;
  case PlatformRawMouseMessageKind::AbsoluteMotion:
    stream_deltas_.note_absolute_raw_motion();
    break;
  case PlatformRawMouseMessageKind::CaptureLost:
    if (!capture_loss_notification_pending_) {
      capture_loss_notification_pending_ = true;
      if (capture_lost_callback_) {
        capture_lost_callback_();
      }
    }
    break;
  case PlatformRawMouseMessageKind::None:
    break;
  }
  // Raw input supplements Qt's legacy messages; Qt must retain ownership of
  // button, focus, hover, and physical local-coordinate delivery.
  return false;
}

} // namespace quader
