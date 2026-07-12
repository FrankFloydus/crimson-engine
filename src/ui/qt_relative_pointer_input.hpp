////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "platform/raw_mouse_input.hpp"

#include <QAbstractNativeEventFilter>

#include <cstdint>
#include <functional>
#include <utility>

namespace quader {

/**
 * Accumulates every relative pointer sample until one input frame drains it.
 */
class QtRelativePointerDeltaAccumulator final {
public:
  void add(QVec2 delta) noexcept { pending_ = pending_ + delta; }

  [[nodiscard]] QVec2 take() noexcept {
    const QVec2 result = pending_;
    pending_ = {};
    return result;
  }

  void clear() noexcept { pending_ = {}; }
  [[nodiscard]] QVec2 pending() const noexcept { return pending_; }

private:
  QVec2 pending_ = {};
};

/**
 * Selects the Qt or raw source for the next translated pointer packet.
 */
enum class QtRelativePointerPacketMode : std::uint8_t {
  Undetermined,
  RelativeRaw,
  QtFallback,
};

/**
 * Routes each raw packet and its following legacy Qt move without counting a
 * relative packet twice. Genuine raw and absolute-device Qt motion are both
 * retained and summed until the next frame drains them.
 */
class QtRelativePointerStreamAccumulator final {
public:
  void add_qt_delta(QVec2 delta) noexcept;
  void note_relative_raw_delta(QVec2 delta) noexcept;
  void note_absolute_raw_motion() noexcept;
  [[nodiscard]] QVec2 take() noexcept;
  void reset() noexcept;

  [[nodiscard]] QtRelativePointerPacketMode packet_mode() const noexcept {
    return packet_mode_;
  }

private:
  QtRelativePointerDeltaAccumulator raw_deltas_;
  QtRelativePointerDeltaAccumulator qt_deltas_;
  QtRelativePointerPacketMode packet_mode_ =
      QtRelativePointerPacketMode::Undetermined;
};

/**
 * Reports the logical result of applying one pointer-capture transition.
 */
enum class QtPointerCaptureTransition : std::uint8_t {
  NoChange,
  Enabled,
  EnableFailed,
  Disabled,
  DisableFailed,
};

/**
 * Pure state policy for applying a fallible native mouse-grab transition.
 * Enable failure never advertises capture; disable failure always clears the
 * logical state so focus loss cannot leave the editor stuck in fly mode.
 */
class QtPointerCapturePolicy final {
public:
  [[nodiscard]] QtPointerCaptureTransition
  apply(bool requested, bool platform_transition_succeeded) noexcept;

  [[nodiscard]] bool captured() const noexcept { return captured_; }

private:
  bool captured_ = false;
};

enum class QtPointerCaptureFailure : std::uint8_t { Enable, Disable };

/**
 * Issues at most one warning per failure class until that class succeeds.
 */
class QtPointerCaptureWarningGate final {
public:
  [[nodiscard]] bool should_warn(QtPointerCaptureFailure failure) noexcept;
  void note_success(QtPointerCaptureFailure failure) noexcept;

private:
  bool enable_warning_issued_ = false;
  bool disable_warning_issued_ = false;
};

/**
 * Adapts native relative mouse messages into one Qt viewport input stream.
 * Windows uses raw input; unsupported or failed native registration leaves the
 * adapter inactive so the owning surface can feed its portable Qt fallback.
 */
class QtRelativePointerInput final : public QAbstractNativeEventFilter {
public:
  using CaptureLostCallback = std::function<void()>;

  QtRelativePointerInput() = default;
  ~QtRelativePointerInput() override;

  QtRelativePointerInput(const QtRelativePointerInput &) = delete;
  QtRelativePointerInput &operator=(const QtRelativePointerInput &) = delete;

  [[nodiscard]] bool start_raw_input(void *native_window);
  [[nodiscard]] bool stop_raw_input();
  [[nodiscard]] bool raw_input_active() const noexcept {
    return platform_input_.active();
  }
  [[nodiscard]] std::uint32_t last_native_error() const noexcept {
    return platform_input_.last_native_error();
  }

  void add_fallback_delta(QVec2 delta) noexcept {
    stream_deltas_.add_qt_delta(delta);
  }
  [[nodiscard]] QVec2 take_delta() noexcept { return stream_deltas_.take(); }
  void clear() noexcept { stream_deltas_.reset(); }
  void set_capture_lost_callback(CaptureLostCallback callback) {
    capture_lost_callback_ = std::move(callback);
  }

  bool nativeEventFilter(const QByteArray &event_type, void *message,
                         qintptr *result) override;

private:
  PlatformRawMouseInput platform_input_;
  QtRelativePointerStreamAccumulator stream_deltas_;
  CaptureLostCallback capture_lost_callback_;
  bool native_filter_installed_ = false;
  bool capture_loss_notification_pending_ = false;
};

} // namespace quader
