////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QCloseEvent>
#include <QMainWindow>

#include <functional>
#include <utility>

namespace quader {

/**
 * Owns the one native-window close boundary used by title-bar and menu exits.
 */
class QtMainWindow final : public QMainWindow {
public:
  using CloseRequestHandler = std::function<bool()>;

  using QMainWindow::QMainWindow;

  void set_close_request_handler(CloseRequestHandler handler) {
    close_request_handler_ = std::move(handler);
  }

protected:
  void closeEvent(QCloseEvent *event) override {
    if (close_request_handler_ && !close_request_handler_()) {
      event->ignore();
      return;
    }
    QMainWindow::closeEvent(event);
  }

private:
  CloseRequestHandler close_request_handler_;
};

} // namespace quader
