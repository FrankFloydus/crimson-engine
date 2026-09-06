////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QLabel>

#include <functional>

namespace quader {

/// Retains status text and error emphasis; the caller decides when to announce.
class QtStatusMessage final : public QLabel {
public:
  explicit QtStatusMessage(QWidget *parent = nullptr);

  void present(const QString &text, bool error, bool announce = false);
  [[nodiscard]] bool is_error() const noexcept { return error_; }

  /// Replaces the platform beep; an empty callback silences announcements.
  void set_error_sound_callback(std::function<void()> callback);

private:
  std::function<void()> error_sound_callback_;
  bool error_ = false;
};

} // namespace quader
