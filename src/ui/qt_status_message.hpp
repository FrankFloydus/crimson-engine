////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QLabel>

#include <functional>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace quader {

/// Retains status text and error emphasis; the caller decides when to announce.
class QtStatusMessage final : public QLabel {
public:
  explicit QtStatusMessage(QWidget *parent = nullptr);

  void present(const QString &text, bool error, bool announce = false);
  [[nodiscard]] bool is_error() const noexcept { return error_; }

  /// Holds the current message for five seconds, then fades without moving it.
  void restart_dismissal(std::function<void()> dismissed);
  void cancel_dismissal();
  [[nodiscard]] bool dismissal_active() const noexcept;

  /// Replaces the platform beep; an empty callback silences announcements.
  void set_error_sound_callback(std::function<void()> callback);

private:
  std::function<void()> error_sound_callback_;
  std::function<void()> dismissed_callback_;
  QTimer *dismissal_timer_ = nullptr;
  QGraphicsOpacityEffect *opacity_effect_ = nullptr;
  QPropertyAnimation *fade_animation_ = nullptr;
  bool error_ = false;
};

} // namespace quader
